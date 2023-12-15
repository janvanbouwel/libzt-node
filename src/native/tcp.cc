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

  private:
    tcp_pcb* pcb = nullptr;

    Napi::ThreadSafeFunction emit;

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

VOID_METHOD(Socket::init)
{
    NB_ARGS(1);
    auto emit = ARG_FUNC(0);
    this->emit = Napi::ThreadSafeFunction::New(env, emit, "tcpEventEmitter", 0, 1);

    typed_tcpip_callback([=]() {
        if (! this->pcb)
            this->pcb = tcp_new();
        tcp_arg(this->pcb, this);

        tcp_recv(this->pcb, [](void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) -> err_t {
            auto thiz = reinterpret_cast<Socket*>(arg);
            if (thiz->emit.Acquire() != napi_ok) {
                if (p)
                    ts_pbuf_free(p);
                return ERR_OK;   // TODO: other return code?
            }
            thiz->emit.BlockingCall([=](TSFN_ARGS) {
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
            return ERR_OK;
        });

        tcp_sent(this->pcb, [](void* arg, struct tcp_pcb* tpcb, u16_t len) -> err_t {
            auto thiz = reinterpret_cast<Socket*>(arg);
            thiz->emit.BlockingCall([=](TSFN_ARGS) { jsCallback.Call({ STRING("sent"), NUMBER(len) }); });
            return ERR_OK;
        });

        tcp_err(this->pcb, [](void* arg, err_t err) {
            auto thiz = reinterpret_cast<Socket*>(arg);
            thiz->pcb = nullptr;   // TODO: cleanup tsfn properly
            thiz->emit.BlockingCall([=](TSFN_ARGS) {
                if (err == ERR_CLSD) {
                    jsCallback.Call({ STRING("close") });
                }
                else {
                    jsCallback.Call(
                        { STRING("error"), MAKE_ERROR("TCP error", ERR_FIELD("code", NUMBER(err))).Value() });
                }
            });
            thiz->emit.Release();
        });
    });
}
VOID_METHOD(Socket::connect)
{
    NB_ARGS(2);
    int port = ARG_NUMBER(0);
    std::string address = ARG_STRING(1);

    ip_addr_t ip_addr;
    ipaddr_aton(address.c_str(), &ip_addr);

    typed_tcpip_callback([=]() {
        tcp_connect(this->pcb, &ip_addr, port, [](void* arg, struct tcp_pcb* tpcb, err_t err) -> err_t {
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
    auto dataRef = NEW_REF_UINT8ARRAY(data);

    auto bufLength = data.ByteLength();
    auto buffer = data.Data();

    return lwip_thread_promise(env, [=]() {
        auto sndbuf = tcp_sndbuf(this->pcb);

        u16_t len = (sndbuf < bufLength) ? sndbuf : bufLength;
        tcp_write(this->pcb, buffer, len, TCP_WRITE_FLAG_COPY);

        return [=](Napi::Env env, Napi::Promise::Deferred* promise) -> void {
            promise->Resolve(NUMBER(len));
            delete dataRef;
        };
    });
}

VOID_METHOD(Socket::ack)
{
    NB_ARGS(1);
    int length = ARG_NUMBER(0);

    typed_tcpip_callback([=]() { tcp_recved(this->pcb, length); });
}

VOID_METHOD(Socket::shutdown_wr)
{
    typed_tcpip_callback([=]() { tcp_shutdown(this->pcb, 0, 1); });
}

/* #########################################
 * ###############  SERVER  ################
 * ######################################### */

CLASS(Server)
{
  public:
    static Napi::FunctionReference* constructor;

    CLASS_INIT_DECL();
    CONSTRUCTOR_DECL(Server);

  private:
    tcp_pcb* pcb;

    Napi::ThreadSafeFunction onConnection;

    VOID_METHOD(listen);
    METHOD(address);
    VOID_METHOD(close);

    tcp_accept_fn acceptCb;
};

Napi::FunctionReference* Server::constructor = new Napi::FunctionReference;

CLASS_INIT_IMPL(Server)
{
    auto ServerClass = CLASS_DEFINE(
        Server,
        { CLASS_INSTANCE_METHOD(Server, listen),
          CLASS_INSTANCE_METHOD(Server, address),
          CLASS_INSTANCE_METHOD(Server, close) });

    *constructor = Napi::Persistent(ServerClass);

    EXPORT(Server, ServerClass);
    return exports;
}

Server::CONSTRUCTOR(Server)
{
    NB_ARGS(1);
    auto onConnection = ARG_FUNC(0);
    this->onConnection = Napi::ThreadSafeFunction::New(env, onConnection, "TCP::onConnection", 0, 1);

    acceptCb = [](void* arg, tcp_pcb* new_pcb, err_t err) -> err_t {
        auto thiz = reinterpret_cast<Server*>(arg);

        // delay accepting connection until callback has been set up.
        tcp_backlog_delayed(new_pcb);

        thiz->onConnection.BlockingCall([=](TSFN_ARGS) {
            if (err < 0) {
                jsCallback.Call({ MAKE_ERROR("accept error", { ERR_FIELD("code", NUMBER(err)); }).Value() });
                return;
            }

            auto socket = Socket::constructor->New({});
            Socket::Unwrap(socket)->set_pcb(new_pcb);

            jsCallback.Call({ UNDEFINED, socket });

            // event handlers set in callback so now accept connection
            typed_tcpip_callback([=]() { tcp_backlog_accepted(new_pcb); });
        });

        return ERR_OK;
    };

    typed_tcpip_callback([=]() {
        this->pcb = tcp_new();
        tcp_arg(this->pcb, this);
    });
}

VOID_METHOD(Server::listen)
{
    NB_ARGS(3);
    int port = ARG_NUMBER(0);
    std::string address = ARG_STRING(1);
    auto callback = ARG_FUNC(2);

    auto onListening = TSFN_ONCE(callback, "onListening");

    ip_addr_t ip_addr;
    if (address.size() == 0)
        ip_addr = ip_addr_any_type;
    else
        ipaddr_aton(address.c_str(), &ip_addr);

    typed_tcpip_callback([=]() {
        int err = tcp_bind(this->pcb, &ip_addr, port);
        if (err < 0) {
            onListening->BlockingCall([=](TSFN_ARGS) {
                jsCallback.Call({ MAKE_ERROR("failed to bind", { ERR_FIELD("code", NUMBER(err)); }).Value() });
            });
            onListening->Release();
            return;
        }
        this->pcb = tcp_listen(this->pcb);
        tcp_accept(this->pcb, this->acceptCb);
        onListening->BlockingCall([](TSFN_ARGS) { jsCallback.Call({}); });
        onListening->Release();
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

VOID_METHOD(Server::close)
{
    NB_ARGS(1);
    auto cb = ARG_FUNC(0);
    auto onClose = TSFN_ONCE(cb, "tcpServerClose");

    typed_tcpip_callback([=]() {
        tcp_close(this->pcb);
        this->pcb = nullptr;

        this->onConnection.Release();

        onClose->BlockingCall();
        onClose->Release();
    });
}

}   // namespace TCP