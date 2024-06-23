#include "ZeroTierSockets.h"
#include "macros.h"
#include "tcp.cc"
#include "udp.cc"

#include <napi.h>
#include <sstream>

#define THROW_ERROR(ERR, FUN)                                                                                          \
    do {                                                                                                               \
        if (ERR < 0) {                                                                                                 \
            auto error = ERROR("Error during " FUN " call.", ERR);                                                     \
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

VOID_METHOD(init_from_memory)
{
    NB_ARGS(1);
    auto key = ARG_UINT8ARRAY(0);

    int err = zts_init_from_memory((char*)key.Data(), key.ByteLength());
    THROW_ERROR(err, "init_from_memory");
}

// ### event and lifetime ###

Napi::ThreadSafeFunction* event_callback = nullptr;

void event_handler(void* msgPtr)
{
    if (! event_callback) {
        return;
    }

    zts_event_msg_t* msg = reinterpret_cast<zts_event_msg_t*>(msgPtr);
    int event = msg->event_code;
    auto cb = [event](TSFN_ARGS) { jsCallback.Call({ NUMBER(event) }); };

    int status = event_callback->BlockingCall(cb);

    if (status != napi_ok) {
        event_callback = nullptr;
    }
}

/**
 * The event handler represents the lifetime of the libzt stack. If it is unreffed (either manually or by node_stop) and
 * it is the only thing keeping the node process itself alive, it will abort and its finaliser frees libzt.
 *
 * If node_free is explicitly called, the tsfn is aborted and in its finaliser the actual node is freed.
 *
 * @param cb { (event: number) => void } Callback that is called for every event.
 */
VOID_METHOD(init_set_event_handler)
{
    NB_ARGS(1);
    auto cb = ARG_FUNC(0);

    event_callback = [&] {
        auto tsfn = new Napi::ThreadSafeFunction;
        *tsfn = Napi::ThreadSafeFunction::New(env, cb, "zts_event_listener", 0, 1, [tsfn](Napi::Env) {
            delete tsfn;
            zts_node_free();
        });
        return tsfn;
    }();

    int err = zts_init_set_event_handler(&event_handler);
    THROW_ERROR(err, "init_set_event_handler");
}

VOID_METHOD(ref)
{
    NO_ARGS();

    if (event_callback)
        event_callback->Ref(env);
}

VOID_METHOD(unref)
{
    NO_ARGS();
    if (event_callback)
        event_callback->Unref(env);
}

// ### node ###

VOID_METHOD(node_start)
{
    NO_ARGS();

    int err = zts_node_start();
    THROW_ERROR(err, "node_start");

    if (event_callback)
        event_callback->Ref(env);
}

VOID_METHOD(node_stop)
{
    NO_ARGS();

    int err = zts_node_stop();
    THROW_ERROR(err, "node_stop");

    if (event_callback)
        event_callback->Unref(env);
}

VOID_METHOD(node_free)
{
    NO_ARGS();

    int err = zts_node_free();
    THROW_ERROR(err, "node_free");

    if (event_callback) {
        event_callback->Abort();
        event_callback = nullptr;
    }
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
    EXPORT_FUNCTION(init_from_memory);

    // event
    EXPORT_FUNCTION(init_set_event_handler);
    EXPORT_FUNCTION(ref);
    EXPORT_FUNCTION(unref);
    // node
    EXPORT_FUNCTION(node_start);
    EXPORT_FUNCTION(node_stop);
    EXPORT_FUNCTION(node_free);
    EXPORT_FUNCTION(node_is_online);
    EXPORT_FUNCTION(node_get_id);

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
