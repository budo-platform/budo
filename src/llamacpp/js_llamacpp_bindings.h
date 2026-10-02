#ifndef BUDO_JS_LLAMACPP_BINDINGS_H
#define BUDO_JS_LLAMACPP_BINDINGS_H

typedef struct JSContext JSContext;
typedef struct JsLlamaCppContext JsLlamaCppContext;
typedef struct FileContext FileContext;

#ifdef __cplusplus
extern "C"
{
#endif
    JsLlamaCppContext *js_llamacpp_init(JSContext *ctx, FileContext *files);
    void js_llamacpp_poll(JsLlamaCppContext *state);
    
    bool js_llamacpp_has_pending_work(JsLlamaCppContext *state);
    void js_llamacpp_cancel_all(JsLlamaCppContext *state);
    void js_llamacpp_cleanup(JsLlamaCppContext *state);
#ifdef __cplusplus
}
#endif
#endif