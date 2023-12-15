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

#define CLASS_INSTANCE_METHOD(CLASS, NAME) InstanceMethod<&CLASS::NAME>(#NAME, napi_default)

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
 */
#define TSFN_ONCE(CALLBACK, NAME) TSFN_ONCE_WITH_FINALISE(CALLBACK, NAME, )

/**
 * returns pointer to a new threadafe function with the specified callback
 * Memory is automatically freed when the tsfn finalises
 *
 * Third argument is napi reference that should be deleted when tsfn finalises
 */
#define TSFN_ONCE_WITH_REF(CALLBACK, NAME, REF)                                                                        \
    [&]() {                                                                                                            \
        auto __data_ref = REF;                                                                                         \
        return TSFN_ONCE_WITH_FINALISE(CALLBACK, NAME, { delete __data_ref; });                                        \
    }()

/**
 * returns pointer to a new threadafe function with the specified callback
 * Memory is automatically freed when the tsfn finalises
 *
 * Last argument can contain extra finalising code (copy capture)
 */
#define TSFN_ONCE_WITH_FINALISE(CALLBACK, NAME, FINALISE)                                                              \
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