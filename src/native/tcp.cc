#include "lwip/tcp.h"

#include "ZeroTierSockets.h"
#include "lwip-util.h"
#include "lwip/tcpip.h"
#include "macros.h"

#include <napi.h>

namespace TCP {

/* #########################################
 * ###############  SOCKET  ################
 * ######################################### */

CLASS(Socket)
{
  public:
    static Napi::FunctionReference* constructor;

    CLASS_INIT_DECL();

    CONSTRUCTOR(Socket) {};

    void set_pcb(tcp_pcb * pcb)
    {
        this->pcb = pcb;
    }

    Napi::ThreadSafeFunction emit;

    void emit_close();

  private:
    tcp_pcb* pcb = nullptr;

    VOID_METHOD(connect);
    VOID_METHOD(init);
    METHOD(send);
    VOID_METHOD(ack);
    VOID_METHOD(shutdown_wr);
};

Napi::FunctionReference* Socket::constructor = nullptr;

CLASS_INIT_IMPL(Socket)
{
    auto SocketClass = CLASS_DEFINE(
        Socket,
        { CLASS_INSTANCE_METHOD(Socket, init),
          CLASS_INSTANCE_METHOD(Socket, connect),
          CLASS_INSTANCE_METHOD(Socket, send),
          CLASS_INSTANCE_METHOD(Socket, ack),
          CLASS_INSTANCE_METHOD(Socket, shutdown_wr) });

    CLASS_SET_CONSTRUCTOR(SocketClass);

    EXPORT(Socket, SocketClass);
    return exports;
}

void Socket::emit_close()
{
    emit.BlockingCall([](TSFN_ARGS) { jsCallback.Call({ STRING("close") }); });
    emit.Release();
}

err_t tcp_receive_cb(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    if (thiz->emit.Acquire() != napi_ok) {
        if (p)
            ts_pbuf_free(p);
        return ERR_OK;   // TODO: other return code? ack the buf immediately? will we ever get here? (if destroy
                         // implemented maybe)
    }
    thiz->emit.BlockingCall([p](TSFN_ARGS) {
        if (! p) {
            jsCallback.Call({ STRING("data"), UNDEFINED });
        }
        else {
            auto data = Napi::Buffer<char>::NewOrCopy(
                env,
                reinterpret_cast<char*>(p->payload),
                p->len,
                [p](Napi::Env env, char* data) { ts_pbuf_free(p); });
            jsCallback.Call({ STRING("data"), data });
        }
    });
    thiz->emit.Release();
    if (tpcb->state == TIME_WAIT) {
        // tx shutdown and FIN received
        thiz->emit_close();
    }
    return ERR_OK;
}

err_t tcp_sent_cb(void* arg, struct tcp_pcb* tpcb, u16_t len)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    thiz->emit.BlockingCall([len](TSFN_ARGS) { jsCallback.Call({ STRING("sent"), NUMBER(len) }); });
    return ERR_OK;
}

void tcp_err_cb(void* arg, err_t err)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    thiz->set_pcb(nullptr);   // TODO: cleanup tsfn properly

    if (err == ERR_CLSD) {
        thiz->emit_close();
        return;
    }

    thiz->emit.BlockingCall([err](TSFN_ARGS) {
        jsCallback.Call({ STRING("error"), MAKE_ERROR("TCP error", ERR_FIELD("code", NUMBER(err))).Value() });
    });
    thiz->emit.Release();
}

VOID_METHOD(Socket::init)
{
    NB_ARGS(1);
    auto emit = ARG_FUNC(0);
    this->emit = Napi::ThreadSafeFunction::New(env, emit, "tcpEventEmitter", 0, 1);

    typed_tcpip_callback([this]() {
        if (! this->pcb)
            this->pcb = tcp_new();
        tcp_arg(this->pcb, this);

        tcp_recv(this->pcb, tcp_receive_cb);

        tcp_sent(this->pcb, tcp_sent_cb);

        tcp_err(this->pcb, tcp_err_cb);
    });
}
VOID_METHOD(Socket::connect)
{
    NB_ARGS(2);
    int port = ARG_NUMBER(0);
    std::string address = ARG_STRING(1);

    ip_addr_t ip_addr;
    ipaddr_aton(address.c_str(), &ip_addr);

    typed_tcpip_callback([pcb = this->pcb, ip_addr, port]() {
        tcp_connect(pcb, &ip_addr, port, [](void* arg, struct tcp_pcb* tpcb, err_t err) -> err_t {
            auto thiz = reinterpret_cast<Socket*>(arg);
            thiz->emit.BlockingCall([](TSFN_ARGS) { jsCallback.Call({ STRING("connect") }); });
            return ERR_OK;
        });
    });
}

METHOD(Socket::send)
{
    NB_ARGS(1);
    auto data = ARG_UINT8ARRAY(0);

    return async_run(env, [&](auto promise) {
        typed_tcpip_callback(tsfn_once<u16_t>(
            env,
            "Socket::send",
            [this, bufLength = data.ByteLength(), buffer = data.Data()]() {
                auto sndbuf = tcp_sndbuf(this->pcb);

                u16_t len = (sndbuf < bufLength) ? sndbuf : bufLength;
                tcp_write(this->pcb, buffer, len, TCP_WRITE_FLAG_COPY);

                return len;
            },
            [dataRef = ref_uint8array(data), promise](TSFN_ARGS, auto len) -> void {
                dataRef->Reset();
                promise->Resolve(NUMBER(len));
            }));
    });
}

VOID_METHOD(Socket::ack)
{
    NB_ARGS(1);
    int length = ARG_NUMBER(0);

    typed_tcpip_callback([pcb = this->pcb, length]() { tcp_recved(pcb, length); });
}

VOID_METHOD(Socket::shutdown_wr)
{
    typed_tcpip_callback([pcb = this->pcb]() { tcp_shutdown(pcb, 0, 1); });
}

/* #########################################
 * ###############  SERVER  ################
 * ######################################### */

CLASS(Server)
{
  public:
    static Napi::FunctionReference* constructor;

    CLASS_INIT_DECL();
    CONSTRUCTOR(Server)
    {
    }

    /**
     * onConnection is created in createServer if bind works with refCount 2
     *
     * Server::close releases lwip thread
     * Server::Finalize releases main thread
     * (this.internalServer.close() should preceed this.internalServer = undefined in net.ts)
     *
     * In this normal scenario onConnection is always valid in accept_cb()
     *
     * Alternatively, onConnection can be aborted after calling unref.
     * accept_cb() detects this and stops/frees listening pcb.
     * Calling Server::close from main thread would be double free, but unref causing abort signifies that the main
     * thread has stopped executing because the process is exiting, so this can not happen.
     *
     */
    Napi::ThreadSafeFunction* onConnection;
    void Finalize(Napi::Env env);

  private:
    tcp_pcb* pcb;

    static METHOD(createServer);
    METHOD(address);

    Napi::Value _close(Napi::Env env);

    METHOD(close);

    VOID_METHOD(ref)
    {
        NO_ARGS();
        onConnection->Ref(env);
    }
    VOID_METHOD(unref)
    {
        NO_ARGS();
        onConnection->Unref(env);
    }
};

Napi::FunctionReference* Server::constructor = new Napi::FunctionReference;

CLASS_INIT_IMPL(Server)
{
    auto ServerClass = CLASS_DEFINE(
        Server,
        {
            CLASS_STATIC_METHOD(Server, createServer),
            CLASS_INSTANCE_METHOD(Server, address),
            CLASS_INSTANCE_METHOD(Server, close),
            CLASS_INSTANCE_METHOD(Server, ref),
            CLASS_INSTANCE_METHOD(Server, unref),
        });

    *constructor = Napi::Persistent(ServerClass);

    EXPORT(Server, ServerClass);
    return exports;
}

void Server::Finalize(Napi::Env env)
{
    onConnection->Release();
}

err_t accept_cb(void* arg, tcp_pcb* new_pcb, err_t err)
{
    auto onConnection = reinterpret_cast<Napi::ThreadSafeFunction*>(arg);

    // delay accepting connection until callback has been set up.
    tcp_backlog_delayed(new_pcb);
    int tsfnErr = onConnection->BlockingCall([err, new_pcb](TSFN_ARGS) {
        if (err < 0) {
            jsCallback.Call({ ERROR("Accept error", err).Value() });
            return;
        }

        auto socket = Socket::constructor->New({});
        Socket::Unwrap(socket)->set_pcb(new_pcb);

        jsCallback.Call({ UNDEFINED, socket });

        // event handlers set in callback so now accept connection
        typed_tcpip_callback([new_pcb]() { tcp_backlog_accepted(new_pcb); });
    });
    if (tsfnErr != napi_ok) {
        tcp_close((tcp_pcb*)new_pcb->listener);
        tcp_abort(new_pcb);
        return ERR_ABRT;
    }

    return ERR_OK;
}

METHOD(Server::createServer)
{
    NB_ARGS(3);
    int port = ARG_NUMBER(0);
    std::string address = ARG_STRING(1);
    auto onConnection = ARG_FUNC(2);

    ip_addr_t ip_addr;
    if (address.size() == 0)
        ip_addr = ip_addr_any_type;
    else
        ipaddr_aton(address.c_str(), &ip_addr);

    auto onConnectionTsfn = TSFN_ONCE(onConnection, "TCP::onConnection");

    return async_run(env, [&](DeferredPromise promise) {
        typed_tcpip_callback(tsfn_once<std::tuple<err_t, tcp_pcb*> >(
            env,
            "Server::listen",
            [port, ip_addr, onConnectionTsfn]() -> std::tuple<err_t, tcp_pcb*> {
                auto pcb = tcp_new();

                auto err = tcp_bind(pcb, &ip_addr, port);
                if (err != ERR_OK) {
                    tcp_close(pcb);
                    onConnectionTsfn->Release();
                    return { err, nullptr };
                }
                // ok, can only be released by Server::Finalize, but we have ref to server
                onConnectionTsfn->Acquire();
                tcp_arg(pcb, onConnectionTsfn);

                // only if binding succeeded assign to server
                pcb = tcp_listen(pcb);

                tcp_accept(pcb, accept_cb);

                return { static_cast<err_t>(ERR_OK), pcb };
            },
            [promise, onConnectionTsfn](TSFN_ARGS, std::tuple<err_t, tcp_pcb*> res) {
                auto [err, pcb] = res;
                // pcb, onConnectionTsfn are only valid if no err

                if (err != ERR_OK) {
                    return promise->Reject(ERROR("failed to bind", err).Value());
                }
                else {
                    auto serverObj = Server::constructor->New({});
                    auto server = Server::Unwrap(serverObj);
                    server->pcb = pcb;
                    server->onConnection = onConnectionTsfn;
                    return promise->Resolve(serverObj);
                }
            }));
    });
}

METHOD(Server::address)
{
    NO_ARGS();

    char addr[ZTS_IP_MAX_STR_LEN];
    ipaddr_ntoa_r(&pcb->local_ip, addr, ZTS_IP_MAX_STR_LEN);

    return OBJECT({
        ADD_FIELD("address", STRING(addr));
        ADD_FIELD("port", NUMBER(pcb->local_port));
        ADD_FIELD("family", IP_IS_V6(&pcb->local_ip) ? STRING("IPv6") : STRING("IPv4"))
    });
}

METHOD(Server::close)
{
    NO_ARGS();

    return async_run(env, [&](DeferredPromise promise) {
        if (! pcb) {
            return promise->Resolve(UNDEFINED);
        }

        auto pcb = this->pcb;
        this->pcb = nullptr;

        auto onConnection = this->onConnection;

        typed_tcpip_callback(tsfn_once_void(
            env,
            "Server::close",
            [pcb, onConnection]() {
                tcp_close(pcb);
                onConnection->Release();
            },
            [promise, onConnection](TSFN_ARGS) { promise->Resolve(UNDEFINED); }));
    });
}

}   // namespace TCP