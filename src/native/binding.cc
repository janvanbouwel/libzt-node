#include "ZeroTierSockets.h"
#include "macros.h"
#include "tcp.cc"
#include "udp.cc"

#include <napi.h>
#include <sstream>

#define THROW_ERROR(ERR, FUN)                                                                                                \
    do {                                                                                                               \
        if (ERR < 0) {                                                                                                 \
            auto error = ERROR("Error during " FUN " call.", ERR);                \
            throw error;                                                                                               \
        }                                                                                                              \
    } while (0)

// ### init ###

VOID_METHOD(init_from_storage)
{
    NB_ARGS(1);
    auto configPath = ARG_STRING(0);

    int err = zts_init_from_storage(std::string(configPath).c_str());
    THROW_ERROR(err, "init_from_storage");
}

Napi::ThreadSafeFunction event_callback;

void event_handler(void* msgPtr)
{
    event_callback.Acquire();

    zts_event_msg_t* msg = reinterpret_cast<zts_event_msg_t*>(msgPtr);
    int event = msg->event_code;
    auto cb = [event](TSFN_ARGS) { jsCallback.Call({ NUMBER(event) }); };

    event_callback.NonBlockingCall(cb);

    event_callback.Release();
}

/**
 * @param cb { (event: number) => void } Callback that is called for every event.
 */
VOID_METHOD(init_set_event_handler)
{
    NB_ARGS(1);
    auto cb = ARG_FUNC(0);

    event_callback = Napi::ThreadSafeFunction::New(env, cb, "zts_event_listener", 0, 1);

    int err = zts_init_set_event_handler(&event_handler);
    THROW_ERROR(err, "init_set_event_handler");
}

// ### node ###

VOID_METHOD(node_start)
{
    NO_ARGS();

    int err = zts_node_start();
    THROW_ERROR(err, "node_start");
}

METHOD(node_is_online)
{
    NO_ARGS();
    return BOOL(zts_node_is_online());
}

METHOD(node_get_id)
{
    NO_ARGS();

    auto id = zts_node_get_id();

    std::ostringstream ss;
    ss << std::hex << id;

    return STRING(ss.str());
}

VOID_METHOD(node_stop)
{
    NO_ARGS();

    int err = zts_node_stop();
    THROW_ERROR(err, "node_stop");

    if (event_callback)
        event_callback.Abort();
}

VOID_METHOD(node_free)
{
    NO_ARGS();

    int err = zts_node_free();
    THROW_ERROR(err, "node_free");

    if (event_callback)
        event_callback.Abort();
}

// ### net ###

uint64_t convert_net_id(std::string net_id)
{
    return std::stoull(net_id, nullptr, 16);
}

VOID_METHOD(net_join)
{
    NB_ARGS(1);
    auto net_id = ARG_STRING(0);

    int err = zts_net_join(convert_net_id(net_id));
    THROW_ERROR(err, "net_join");
}

VOID_METHOD(net_leave)
{
    NB_ARGS(1);
    auto net_id = ARG_STRING(0);

    int err = zts_net_leave(convert_net_id(net_id));
    THROW_ERROR(err, "net_leave");
}

METHOD(net_transport_is_ready)
{
    NB_ARGS(1);
    auto net_id = ARG_STRING(0);

    return BOOL(zts_net_transport_is_ready(convert_net_id(net_id)));
}

// ### addr ###

METHOD(addr_get_str)
{
    NB_ARGS(2);
    auto net_id = ARG_STRING(0);
    auto ipv6 = ARG_BOOLEAN(1);

    auto family = ipv6 ? ZTS_AF_INET6 : ZTS_AF_INET;

    char addr[ZTS_IP_MAX_STR_LEN];

    int err = zts_addr_get_str(convert_net_id(net_id), family, addr, ZTS_IP_MAX_STR_LEN);
    THROW_ERROR(err, "addr_get_str");

    return STRING(addr);
}

// NAPI initialiser

INIT_ADDON(zts)
{
    // init
    EXPORT_FUNCTION(init_from_storage);
    EXPORT_FUNCTION(init_set_event_handler);
    // node
    EXPORT_FUNCTION(node_start);
    EXPORT_FUNCTION(node_is_online);
    EXPORT_FUNCTION(node_get_id);
    EXPORT_FUNCTION(node_stop);
    EXPORT_FUNCTION(node_free);

    // net
    EXPORT_FUNCTION(net_join);
    EXPORT_FUNCTION(net_leave);
    EXPORT_FUNCTION(net_transport_is_ready);

    // addr
    EXPORT_FUNCTION(addr_get_str);

    INIT_CLASS(TCP::Socket);
    INIT_CLASS(TCP::Server);

    INIT_CLASS(UDP::Socket);
}
