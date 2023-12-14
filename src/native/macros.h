#ifndef NAPI_MACROS
#define NAPI_MACROS

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

#define EXPORT(F) exports[#F] = Napi::Function::New(env, F)

// CLASS

#define FUNC_REF(NAME) Napi::FunctionReference* NAME = new Napi::FunctionReference()

#define INIT_CLASS(NAME) NAME::Init(env, exports)

#define CLASS(NAME) class NAME : public Napi::ObjectWrap<NAME>

#define CONSTRUCTOR_DECL(NAME) NAME(CALLBACKINFO)

#define CONSTRUCTOR_IMPL(NAME) NAME::NAME(CALLBACKINFO) : Napi::ObjectWrap<NAME>(info)

#define CLASS_INIT_DECL() static Napi::Object Init(Napi::Env env, Napi::Object exports)

#define CLASS_DEFINE(NAME, ...) DefineClass(env, #NAME, __VA_ARGS__)

#define CLASS_INIT_IMPL(NAME) Napi::Object NAME::Init(Napi::Env env, Napi::Object exports)

#define CLASS_INSTANCE_METHOD(CLASS, NAME) InstanceMethod<&CLASS::NAME>(#NAME, napi_default)

// METHOD

#define CALLBACKINFO const Napi::CallbackInfo& info

#define METHOD(NAME) Napi::Value NAME(CALLBACKINFO)

#define CLASS_METHOD_IMPL(CLASS, NAME) METHOD(CLASS::NAME)

#define HANDLE_SCOPE()                                                                                                 \
    Napi::HandleScope scope(Env());                                                                                    \
    Napi::Env env = Env();

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

#define VOID env.Undefined()

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

// Reference

/**
 * Returns pointer to new reference. Has to be manually deleted!
 */
#define NEW_REF_UINT8ARRAY(ARRAY)                                                                                      \
    [&]() {                                                                                                            \
        auto __ref = new Napi::Reference<Napi::Uint8Array>;                                                            \
        *__ref = Napi::Persistent(ARRAY);                                                                              \
        return __ref;                                                                                                  \
    }()

// Threadsafe

/**
 * returns pointer to a new threadafe function with the specified callback
 * Memory is automatically freed when the tsfn finalises
 *
 * Last argument can contain extra finalising code (copy capture)
 */
#define TSFN_ONCE(CALLBACK, NAME, FINALISE)                                                                            \
    [&]() {                                                                                                            \
        auto __tsfn = new Napi::ThreadSafeFunction;                                                                    \
        *__tsfn = Napi::ThreadSafeFunction::New(env, CALLBACK, NAME, 0, 1, [=](Napi::Env) {                            \
            FINALISE;                                                                                                  \
            delete __tsfn;                                                                                             \
        });                                                                                                            \
        return __tsfn;                                                                                                 \
    }()

#define TSFN_ARGS Napi::Env env, Napi::Function jsCallback

// error creation

#define MAKE_ERROR(REASON, FIELDS)                                                                                     \
    [&]() {                                                                                                            \
        auto __error = Napi::Error::New(env, REASON);                                                                  \
        FIELDS;                                                                                                        \
        return __error;                                                                                                \
    }()

#define ERR_FIELD(NAME, VALUE) __error.Set(STRING(NAME), VALUE)

#endif   // NAPI_MACROS