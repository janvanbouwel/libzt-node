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

    Napi::ThreadSafeFunction* emit;

    void emit_close();

    // in lwip tcpip thread
    void init(tcp_pcb * pcb);

  private:
    tcp_pcb* pcb = nullptr;

    VOID_METHOD(connect);
    VOID_METHOD(setEmitter);
    METHOD(send);
    VOID_METHOD(ack);
    VOID_METHOD(shutdown_wr);

    VOID_METHOD(ref)
    {
        NO_ARGS();
        if (emit)
            emit->Ref(env);
    }
    VOID_METHOD(unref)
    {
        NO_ARGS();
        if (emit)
            emit->Unref(env);
    }

    VOID_METHOD(nagle)
    {
        NB_ARGS(1);
        bool enable = ARG_BOOLEAN(0);

        typed_tcpip_callback([pcb = this->pcb, enable]() {
            if (enable)
                tcp_nagle_enable(pcb);
            else
                tcp_nagle_disable(pcb);
        });
    }
};

Napi::FunctionReference* Socket::constructor = nullptr;

CLASS_INIT_IMPL(Socket)
{
    auto SocketClass = CLASS_DEFINE(
        Socket,
        { CLASS_INSTANCE_METHOD(Socket, setEmitter),
          CLASS_INSTANCE_METHOD(Socket, connect),
          CLASS_INSTANCE_METHOD(Socket, send),
          CLASS_INSTANCE_METHOD(Socket, ack),
          CLASS_INSTANCE_METHOD(Socket, shutdown_wr),
          CLASS_INSTANCE_METHOD(Socket, ref),
          CLASS_INSTANCE_METHOD(Socket, unref),
          CLASS_INSTANCE_METHOD(Socket, nagle) });

    CLASS_SET_CONSTRUCTOR(SocketClass);

    EXPORT(Socket, SocketClass);
    return exports;
}

void Socket::emit_close()
{
    emit->BlockingCall([](TSFN_ARGS) { jsCallback.Call({ STRING("close") }); });
    emit->Release();
    emit = nullptr;
}

err_t tcp_receive_cb(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    thiz->emit->BlockingCall([p](TSFN_ARGS) {
        if (! p) {
            jsCallback.Call({ STRING("data"), UNDEFINED });
        }
        else {
            auto data = Napi::Uint8Array::New(env, p->tot_len);
            pbuf_copy_partial(p, data.Data(), p->tot_len, 0);
            ts_pbuf_free(p);
            jsCallback.Call({ STRING("data"), data });
        }
    });

    if (tpcb->state == TIME_WAIT) {
        // tx shutdown and FIN received
        thiz->emit_close();
    }
    return ERR_OK;
}

err_t tcp_sent_cb(void* arg, struct tcp_pcb* tpcb, u16_t len)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    thiz->emit->BlockingCall([len](TSFN_ARGS) { jsCallback.Call({ STRING("sent"), NUMBER(len) }); });
    return ERR_OK;
}

void tcp_err_cb(void* arg, err_t err)
{
    auto thiz = reinterpret_cast<Socket*>(arg);
    thiz->set_pcb(nullptr);   // TODO: cleanup tsfn properly

    if (err == ERR_CLSD) {
        thiz->emit_close();
    }
    else {
        thiz->emit->BlockingCall([err](TSFN_ARGS) {
            jsCallback.Call({ STRING("error"), MAKE_ERROR("TCP error", ERR_FIELD("code", NUMBER(err))).Value() });
        });
        thiz->emit->Release();
        thiz->emit = nullptr;
    }
}

VOID_METHOD(Socket::setEmitter)
{
    NB_ARGS(1);
    auto emit = ARG_FUNC(0);
    this->emit = TSFN_ONCE(emit, "TCP::EventEmitter");
}

void Socket::init(tcp_pcb* pcb)
{
    tcp_arg(this->pcb, this);

    tcp_recv(this->pcb, tcp_receive_cb);

    tcp_sent(this->pcb, tcp_sent_cb);

    tcp_err(this->pcb, tcp_err_cb);
}

VOID_METHOD(Socket::connect)
{
    NB_ARGS(2);
    int port = ARG_NUMBER(0);
    std::string address = ARG_STRING(1);

    ip_addr_t ip_addr;
    ipaddr_aton(address.c_str(), &ip_addr);
    typed_tcpip_callback([this, ip_addr, port]() {
        if (! this->pcb) {
            this->pcb = tcp_new();
        }
        this->init(this->pcb);

        int err = tcp_connect(pcb, &ip_addr, port, [](void* arg, struct tcp_pcb* tpcb, err_t err) -> err_t {
            auto thiz = reinterpret_cast<Socket*>(arg);
            thiz->emit->BlockingCall([addr = addr_info(tpcb)](TSFN_ARGS) {
                jsCallback.Call({ STRING("connect"), convert_addr_info(env, addr) });
            });
            return ERR_OK;
        });

        if (err != ERR_OK) {
            this->emit->BlockingCall([err](TSFN_ARGS) { jsCallback({ STRING("connect_error"), NUMBER(err) }); });
        }
    });
}

METHOD(Socket::send)
{
    NB_ARGS(1);
    auto data = ARG_UINT8ARRAY(0);

    return async_run(env, [&](auto promise) {
        typed_tcpip_callback(tsfn_once<tcpwnd_size_t>(
            env,
            "Socket::send",
            [this, bufLength = data.ByteLength(), buffer = data.Data()]() {
                tcpwnd_size_t sent = 0;

                while (true) {
                    tcpwnd_size_t sndbuf = tcp_sndbuf(this->pcb);
                    auto len = LWIP_MIN(UINT16_MAX, LWIP_MIN(sndbuf, bufLength - sent));
                    if (len == 0)
                        break;

                    tcp_write(this->pcb, buffer + sent, len, TCP_WRITE_FLAG_COPY);
                    sent += len;
                }

                return sent;
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

    typed_tcpip_callback([pcb = this->pcb, length]() {
        size_t remaining = length;
        do {
            u16_t recved = (u16_t)((remaining > 0xffff) ? 0xffff : remaining);
            tcp_recved(pcb, recved);
            remaining -= recved;
        } while (remaining != 0);
        // tcp_recved(pcb, length);
    });
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

    Napi::ThreadSafeFunction* onConnection;

  private:
    tcp_pcb* pcb;

    static METHOD(createServer);
    METHOD(address);

    Napi::Value _close(Napi::Env env);

    METHOD(close);

    VOID_METHOD(ref)
    {
        NO_ARGS();
        if (onConnection)
            onConnection->Ref(env);
    }
    VOID_METHOD(unref)
    {
        NO_ARGS();
        if (onConnection)
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

err_t accept_cb(void* arg, tcp_pcb* new_pcb, err_t err)
{
    auto onConnection = reinterpret_cast<Napi::ThreadSafeFunction*>(arg);

    // delay accepting connection until callback has been set up.
    tcp_backlog_delayed(new_pcb);
    int tsfnErr = onConnection->BlockingCall([err, new_pcb, addr = addr_info(new_pcb)](TSFN_ARGS) {
        if (err < 0) {
            jsCallback.Call({ ERROR("Accept error", err).Value() });
            return;
        }

        auto socketObj = Socket::constructor->New({});
        auto socket = Socket::Unwrap(socketObj);
        socket->set_pcb(new_pcb);

        jsCallback.Call({ UNDEFINED, socketObj, convert_addr_info(env, addr) });

        socket->init(new_pcb);

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
    else   // TODO error handling
        ipaddr_aton(address.c_str(), &ip_addr);

    auto onConnectionTsfn = TSFN_ONCE(onConnection, "TCP::onConnection");

    return async_run(env, [&](DeferredPromise promise) {
        typed_tcpip_callback(tsfn_once_tuple(
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
                tcp_arg(pcb, onConnectionTsfn);

                pcb = tcp_listen(pcb);
                tcp_accept(pcb, accept_cb);

                return { static_cast<err_t>(ERR_OK), pcb };
            },
            [promise, onConnectionTsfn](TSFN_ARGS, err_t err, tcp_pcb* pcb) {
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
        this->onConnection = nullptr;

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