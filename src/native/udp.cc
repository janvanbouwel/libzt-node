#include "lwip/udp.h"

#include "ZeroTierSockets.h"
#include "lwip-util.h"
#include "lwip/tcpip.h"
#include "macros.h"

#include <iostream>
#include <napi.h>

namespace UDP {

struct recv_data {
    pbuf* p;
    char addr[ZTS_IP_MAX_STR_LEN];
    u16_t port;
};
void tsfnOnRecv(TSFN_ARGS, nullptr_t* ctx, recv_data* rd);

using OnRecvTSFN = Napi::TypedThreadSafeFunction<nullptr_t, recv_data, tsfnOnRecv>;

CLASS(Socket)
{
  public:
    static Napi::FunctionReference* constructor;

    CLASS_INIT_DECL();
    CONSTRUCTOR_DECL(Socket);

    OnRecvTSFN onRecv;

  private:
    udp_pcb* pcb;

    METHOD(send);
    METHOD(bind);
    METHOD(close);

    METHOD(address);
    METHOD(remoteAddress);

    METHOD(connect);
    VOID_METHOD(disconnect);
    VOID_METHOD(ref)
    {
        NO_ARGS();
        onRecv.Ref(env);
    }
    VOID_METHOD(unref)
    {
        NO_ARGS();
        onRecv.Unref(env);
    }
};

Napi::FunctionReference* Socket::constructor = nullptr;

CLASS_INIT_IMPL(Socket)
{
    auto SocketClass = CLASS_DEFINE(
        Socket,
        { CLASS_INSTANCE_METHOD(Socket, send),
          CLASS_INSTANCE_METHOD(Socket, bind),
          CLASS_INSTANCE_METHOD(Socket, close),
          CLASS_INSTANCE_METHOD(Socket, address),
          CLASS_INSTANCE_METHOD(Socket, remoteAddress),
          CLASS_INSTANCE_METHOD(Socket, connect),
          CLASS_INSTANCE_METHOD(Socket, disconnect),
          CLASS_INSTANCE_METHOD(Socket, ref),
          CLASS_INSTANCE_METHOD(Socket, unref) });

    CLASS_SET_CONSTRUCTOR(SocketClass);

    EXPORT(UDP, SocketClass);
    return exports;
}

void lwip_recv_cb(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port)
{
    auto thiz = reinterpret_cast<Socket*>(arg);

    recv_data* rd = new recv_data {};
    rd->p = p;
    rd->port = port;
    ipaddr_ntoa_r(addr, rd->addr, ZTS_IP_MAX_STR_LEN);

    thiz->onRecv.BlockingCall(rd);
}

void tsfnOnRecv(TSFN_ARGS, nullptr_t* ctx, recv_data* rd)
{
    if (env == NULL) {
        ts_pbuf_free(rd->p);
        delete rd;

        return;
    }
    pbuf* p = rd->p;

    auto data =
        Napi::Buffer<char>::NewOrCopy(env, reinterpret_cast<char*>(p->payload), p->len, [p](Napi::Env env, char* data) {
            ts_pbuf_free(p);
        });

    auto addr = STRING(rd->addr);
    auto port = NUMBER(rd->port);

    delete rd;

    jsCallback.Call({ data, addr, port });
}

/**
 * @param ipv6 { bool } sets the type of the udp socket
 * @param recvCallback { (data: Buffer, addr: string, port: number)=>void }
 * called when receiving data
 */
Socket::CONSTRUCTOR(Socket)
{
    NB_ARGS(2);
    bool ipv6 = ARG_BOOLEAN(0);
    auto recvCallback = ARG_FUNC(1);

    onRecv = OnRecvTSFN::New(env, recvCallback, "recvCallback", 0, 1, nullptr);

    typed_tcpip_callback([this, ipv6]() {
        this->pcb = udp_new_ip_type(ipv6 ? IPADDR_TYPE_V6 : IPADDR_TYPE_V4);

        udp_recv(this->pcb, lwip_recv_cb, this);
    });
}

METHOD(Socket::send)
{
    NB_ARGS(3);
    auto data = ARG_UINT8ARRAY(0);
    std::string addr = ARG_STRING(1);
    int port = ARG_NUMBER(2);

    ip_addr_t ip_addr;
    if (port)
        ipaddr_aton(addr.c_str(), &ip_addr);

    return async_run(env, [&](DeferredPromise promise) {
        typed_tcpip_callback(tsfn_once(
            env,
            "UDP::Socket::send",
            [this,
             port,
             ip_addr,
             len = data.ByteLength(),
             buffer = data.Data(),
             dataRef = ref_uint8array(data),
             promise]() {
                struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_REF);
                p->payload = buffer;

                auto err = port ? udp_sendto(this->pcb, p, &ip_addr, port) : udp_send(this->pcb, p);

                pbuf_free(p);

                return [err, dataRef, promise](TSFN_ARGS) {
                    dataRef->Reset();
                    if (err != ERR_OK)
                        promise->Reject(ERROR("send error", err).Value());
                    else
                        promise->Resolve(UNDEFINED);
                };
            }));
    });
}

METHOD(Socket::bind)
{
    NB_ARGS(2);
    std::string addr = ARG_STRING(0);
    int port = ARG_NUMBER(1);

    ip_addr_t ip_addr;

    if (addr.size() == 0)
        ip_addr = ip6_addr_any;
    else
        ipaddr_aton(addr.c_str(), &ip_addr);

    return async_run(env, [&](DeferredPromise promise) {
        typed_tcpip_callback(tsfn_once(env, "UDP::Socket::bind", [this, ip_addr, port, promise]() {
            auto err = udp_bind(pcb, &ip_addr, port);

            return [err, promise](TSFN_ARGS) {
                if (err != ERR_OK)
                    promise->Reject(ERROR("Bind error", err).Value());
                else
                    promise->Resolve(UNDEFINED);
            };
        }));
    });
}

METHOD(Socket::close)
{
    NO_ARGS();

    return async_run(env, [&](DeferredPromise promise) {
        if (pcb) {
            auto old_pcb = pcb;
            pcb = nullptr;

            typed_tcpip_callback(tsfn_once(env, "UDP::Socket::close", [old_pcb, this, promise]() {
                LWIP_ASSERT("pcb was null", old_pcb != nullptr);
                udp_remove(old_pcb);

                return [this, promise](TSFN_ARGS) {
                    onRecv.Abort();
                    promise->Resolve(UNDEFINED);
                };
            }));
            pcb = nullptr;
        }
        else {
            // TODO handle this differently?
            promise->Resolve(UNDEFINED);
        }
    });
}

METHOD(Socket::address)
{
    NO_ARGS();

    char addr[ZTS_IP_MAX_STR_LEN];
    ipaddr_ntoa_r(&pcb->local_ip, addr, ZTS_IP_MAX_STR_LEN);

    return OBJECT({
        ADD_FIELD("address", STRING(addr));
        ADD_FIELD("port", NUMBER(pcb->local_port));
        ADD_FIELD("family", IP_IS_V6(&pcb->local_ip) ? STRING("udp6") : STRING("udp4"))
    });
}

METHOD(Socket::remoteAddress)
{
    NO_ARGS();

    char addr[ZTS_IP_MAX_STR_LEN];
    ipaddr_ntoa_r(&pcb->remote_ip, addr, ZTS_IP_MAX_STR_LEN);

    return OBJECT({
        ADD_FIELD("address", STRING(addr));
        ADD_FIELD("port", NUMBER(pcb->remote_port));
        ADD_FIELD("family", IP_IS_V6(&pcb->remote_ip) ? STRING("udp6") : STRING("udp4"))
    });
}

METHOD(Socket::connect)
{
    NB_ARGS(2);

    std::string address = ARG_STRING(0);
    int port = ARG_NUMBER(1);

    ip_addr_t addr;
    ipaddr_aton(address.c_str(), &addr);

    return async_run(env, [&](DeferredPromise promise) {
        typed_tcpip_callback(tsfn_once(env, "UDP::Socket::connect", [this, addr, port, promise]() {
            auto err = udp_connect(pcb, &addr, port);

            return [err, promise](TSFN_ARGS) {
                if (err != ERR_OK)
                    promise->Reject(ERROR("Connect error", err).Value());
                else
                    promise->Resolve(UNDEFINED);
            };
        }));
    });
}

VOID_METHOD(Socket::disconnect)
{
    typed_tcpip_callback([pcb = this->pcb]() { udp_disconnect(pcb); });
}

}   // namespace UDP
