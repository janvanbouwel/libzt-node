#ifndef LWIP_MACROS
#define LWIP_MACROS

#include "lwip/tcpip.h"
#include "macros.h"
#include "napi.h"

#include <functional>

err_t typed_tcpip_callback(std::function<void()> callback)
{
    auto cb = new std::function<void()>;
    *cb = callback;

    return tcpip_callback(
        [](void* ctx) {
            auto cb = reinterpret_cast<std::function<void()>*>(ctx);
            (*cb)();
            delete cb;
        },
        cb);
}

void ts_pbuf_free(pbuf* p)
{
    tcpip_callback([](void* p) { pbuf_free(reinterpret_cast<pbuf*>(p)); }, p);
}

Napi::Promise lwip_thread_promise(
    Napi::Env env,
    std::function<std::function<void(Napi::Env, Napi::Promise::Deferred*)>()> callback)
{
    auto promise = new Napi::Promise::Deferred(env);

    Napi::ThreadSafeFunction* tsfn =
        TSFN_ONCE_WITH_FINALISE(Napi::Function::New(env, [](CALLBACKINFO) {}), "cb name", { delete promise; });

    typed_tcpip_callback([=]() {
        auto continuation = callback();

        tsfn->BlockingCall([=](TSFN_ARGS) { continuation(env, promise); });
        tsfn->Release();
    });

    return promise->Promise();
}

#endif