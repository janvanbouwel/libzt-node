#ifndef LWIP_MACROS
#define LWIP_MACROS

#include "lwip/tcpip.h"
#include "macros.h"
#include "napi.h"

#include <functional>

/**
 * lwip's tcpip_callback (see below) but with a std::function instead of a void pointer to pass context.
 *
 *  @ingroup lwip_os
 * Call a specific function in the thread context of
 * tcpip_thread for easy access synchronization.
 * A function called in that way may access lwIP core code
 * without fearing concurrent access.
 * Blocks until the request is posted.
 * Must not be called from interrupt context!
 *
 * @param function the function to call
 * @return ERR_OK if the function was called, another err_t if not
 *
 */
err_t typed_tcpip_callback(std::function<void()> callback)
{
    auto cb = new std::function<void()>(callback);

    return tcpip_callback(
        [](void* ctx) {
            auto cb = reinterpret_cast<std::function<void()>*>(ctx);
            (*cb)();
            delete cb;
        },
        cb);
}

std::tuple<std::string, u16_t, bool, std::string, u16_t, bool> addr_info(tcp_pcb* pcb)
{
    char local[ZTS_IP_MAX_STR_LEN];
    ipaddr_ntoa_r(&pcb->local_ip, local, ZTS_IP_MAX_STR_LEN);

    char remote[ZTS_IP_MAX_STR_LEN];
    ipaddr_ntoa_r(&pcb->remote_ip, remote, ZTS_IP_MAX_STR_LEN);

    return { std::string(local),  pcb->local_port,  IPADDR_TYPE_V6 == pcb->local_ip.type,
             std::string(remote), pcb->remote_port, IPADDR_TYPE_V6 == pcb->remote_ip.type };
}

Napi::Object convert_addr_info(Napi::Env env, std::tuple<std::string, u16_t, bool, std::string, u16_t, bool> addr)
{
    return OBJECT({
        ADD_FIELD("localAddr", STRING(std::get<0>(addr)));
        ADD_FIELD("localPort", NUMBER(std::get<1>(addr)));
        ADD_FIELD("localFamily", std::get<2>(addr) ? STRING("IPv6") : STRING("IPv4"));
        ADD_FIELD("remoteAddr", STRING(std::get<3>(addr)));
        ADD_FIELD("remotePort", NUMBER(std::get<4>(addr)));
        ADD_FIELD("remoteFamily", std::get<5>(addr) ? STRING("IPv6") : STRING("IPv4"));
    });
}

/**
 * Threadsafe pbuf_free
 */
void ts_pbuf_free(pbuf* p)
{
    tcpip_callback([](void* p) { pbuf_free(reinterpret_cast<pbuf*>(p)); }, p);
}

#endif