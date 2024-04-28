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

/**
 * Threadsafe pbuf_free
 */
void ts_pbuf_free(pbuf* p)
{
    tcpip_callback([](void* p) { pbuf_free(reinterpret_cast<pbuf*>(p)); }, p);
}

#endif