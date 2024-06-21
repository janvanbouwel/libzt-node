#ifndef NAPI_MACROS
#define NAPI_MACROS

#include "napi.h"

#include <functional>

// INITIALISATION

#define INIT_ADDON(NAME)                                                                                               \
    void Init(Napi::Env env, Napi::Object exports);                                                                    \
    Napi::Object Init_Wrap(Napi::Env env, Napi::Object exports)                                                        \
    {                                                                                                                  \
        Init(env, exports);                                                                                            \
        return exports;                                                                                                \
    }                                                                                                                  \
    NODE_API_MODULE(NAME, Init_Wrap);                                                                                  \
    void Init(Napi::Env env, Napi::Object exports)

#define EXPORT(NAME, VALUE) exports[#NAME] = VALUE

#define EXPORT_FUNCTION(F) EXPORT(F, FUNCTION(F))

// CLASS

#define FUNC_REF(NAME) Napi::FunctionReference* NAME = new Napi::FunctionReference()

#define INIT_CLASS(NAME) NAME::Init(env, exports)

#define CLASS(NAME) class NAME : public Napi::ObjectWrap<NAME>

#define CONSTRUCTOR(NAME) NAME(CALLBACKINFO) : Napi::ObjectWrap<NAME>(info)

#define CONSTRUCTOR_DECL(NAME) NAME(CALLBACKINFO)

#define CLASS_INIT_DECL() static Napi::Object Init(Napi::Env env, Napi::Object exports)

#define CLASS_DEFINE(NAME, ...) DefineClass(env, #NAME, __VA_ARGS__)

#define CLASS_INIT_IMPL(NAME) Napi::Object NAME::Init(Napi::Env env, Napi::Object exports)

#define _CLASS_METHOD(CLASS, NAME) <&CLASS::NAME>(#NAME, napi_default)

#define CLASS_INSTANCE_METHOD(CLASS, NAME) InstanceMethod _CLASS_METHOD(CLASS, NAME)

#define CLASS_STATIC_METHOD(CLASS, NAME) StaticMethod _CLASS_METHOD(CLASS, NAME)

#define CLASS_SET_CONSTRUCTOR(CLASS)                                                                                   \
    [&]() {                                                                                                            \
        constructor = new Napi::FunctionReference;                                                                     \
        *constructor = Napi::Persistent(CLASS);                                                                        \
    }()

// METHOD

#define CALLBACKINFO const Napi::CallbackInfo& info

#define VOID_METHOD(NAME) void NAME(CALLBACKINFO)

#define METHOD(NAME) Napi::Value NAME(CALLBACKINFO)

// ENVIRONMENT AND ARGUMENTS

#define NO_ARGS() Napi::Env env = info.Env()

#define NB_ARGS(N)                                                                                                     \
    Napi::Env env = info.Env();                                                                                        \
    do {                                                                                                               \
        if (info.Length() < N) {                                                                                       \
            throw Napi::TypeError::New(env, "Wrong number of arguments. Expected: " #N);                               \
        }                                                                                                              \
    } while (0)

#define ARG_FUNC(POS) [&]() { return info[POS].As<Napi::Function>(); }()

#define ARG_NUMBER(POS) [&]() { return info[POS].As<Napi::Number>(); }()

#define ARG_STRING(POS) [&]() { return info[POS].ToString(); }()

#define ARG_BOOLEAN(POS) [&]() { return info[POS].ToBoolean(); }()

#define ARG_UINT8ARRAY(POS) [&]() { return info[POS].As<Napi::Uint8Array>(); }()

// WRAP DATA

#define UNDEFINED env.Undefined()

#define BOOL(VALUE) Napi::Boolean::New(env, VALUE)

#define STRING(VALUE) Napi::String::New(env, VALUE)

#define NUMBER(VALUE) Napi::Number::New(env, VALUE)

// use inside of an OBJECT def macro
#define ADD_FIELD(NAME, VALUE) __ret_obj[NAME] = VALUE;

#define OBJECT(FIELDS)                                                                                                 \
    [&] {                                                                                                              \
        auto __ret_obj = Napi::Object::New(env);                                                                       \
        FIELDS                                                                                                         \
        return __ret_obj;                                                                                              \
    }()

#define FUNCTION(F) Napi::Function::New(env, F, #F)

// Reference

/**
 * Returns pointer to new persistent reference.
 */
std::shared_ptr<Napi::Reference<Napi::Uint8Array> > ref_uint8array(Napi::Uint8Array array)
{
    auto ref = new Napi::Reference<Napi::Uint8Array>;
    *ref = Napi::Persistent(array);
    return std::shared_ptr<Napi::Reference<Napi::Uint8Array> >(ref);
}

std::shared_ptr<Napi::Reference<Napi::Value> > ref_value(Napi::Value obj)
{
    auto ref = new Napi::Reference<Napi::Value>;
    *ref = Napi::Persistent(obj);
    return std::shared_ptr<Napi::Reference<Napi::Value> >(ref);
}

// Threadsafe

/**
 * returns pointer to a new threadafe function with the specified callback
 * Memory is automatically freed when the tsfn finalises
 */
#define TSFN_ONCE(CALLBACK, NAME)                                                                                      \
    [&]() {                                                                                                            \
        auto __tsfn = new Napi::ThreadSafeFunction;                                                                    \
        *__tsfn = Napi::ThreadSafeFunction::New(env, CALLBACK, NAME, 0, 1, [__tsfn](Napi::Env) { delete __tsfn; });    \
        return __tsfn;                                                                                                 \
    }()

#define TSFN_ARGS Napi::Env env, Napi::Function jsCallback

typedef std::shared_ptr<Napi::Promise::Deferred> DeferredPromise;

DeferredPromise create_promise(Napi::Env env)
{
    return std::make_shared<Napi::Promise::Deferred>(env);
}

/**
 * Creates a Napi promise and immediately passes it to the provided function which has the responsibilty to either
 * resolve or reject it (immediately or after work in a (different) thread). Returns the actual javascript promise
 * object.
 */
Napi::Promise async_run(Napi::Env env, std::function<void(DeferredPromise)> exec)
{
    auto promise = create_promise(env);

    exec(promise);

    return promise->Promise();
}

/**
 * Returns a callable which, when executed, first executes the provided function `threaded` in the current thread, and
 * then passes the result to `js_callback` in the addon's main thread.
 */
template <typename T>
std::function<void()>
tsfn_once(Napi::Env env, std::string name, std::function<T()> threaded, std::function<void(TSFN_ARGS, T)> js_callback)
{
    auto callback = Napi::Function::New(env, [](CALLBACKINFO) {});

    auto tsfn = TSFN_ONCE(callback, name);

    return [tsfn, threaded, js_callback]() {
        T ret = threaded();

        tsfn->BlockingCall([js_callback, ret](TSFN_ARGS) { js_callback(env, jsCallback, ret); });
        tsfn->Release();
    };
}

template <typename JSF, typename TF, typename... Types>
std::function<void()> tsfn_once_tuple(Napi::Env env, std::string name, TF threaded, JSF js_callback)
{
    auto callback = Napi::Function::New(env, [](CALLBACKINFO) {});

    auto tsfn = TSFN_ONCE(callback, name);

    return [tsfn, threaded, js_callback]() -> void {
        auto ret = threaded();

        tsfn->BlockingCall([js_callback, ret](TSFN_ARGS) {
            std::apply(js_callback, std::tuple_cat(std::make_tuple(env, jsCallback), ret));
        });
        tsfn->Release();
    };
}

/**
 * Returns a callable which, when executed, first executes the provided function `threaded` in the current thread, and
 * then executes `js_callback` in the addon's main thread.
 */
std::function<void()> tsfn_once_void(
    Napi::Env env,
    std::string name,
    std::function<void()> threaded,
    std::function<void(TSFN_ARGS)> js_callback)
{
    auto callback = Napi::Function::New(env, [](CALLBACKINFO) {});

    auto tsfn = TSFN_ONCE(callback, name);

    return [tsfn, threaded, js_callback]() {
        threaded();

        tsfn->BlockingCall([js_callback](TSFN_ARGS) { js_callback(env, jsCallback); });
        tsfn->Release();
    };
}

// error creation

#define ERROR(REASON, CODE) MAKE_ERROR(REASON, { ERR_FIELD("code", NUMBER(CODE)); })

#define MAKE_ERROR(REASON, FIELDS)                                                                                     \
    [&]() {                                                                                                            \
        auto __error = Napi::Error::New(env, REASON);                                                                  \
        FIELDS;                                                                                                        \
        return __error;                                                                                                \
    }()

#define ERR_FIELD(NAME, VALUE) __error.Set(STRING(NAME), VALUE)

#endif   // NAPI_MACROS