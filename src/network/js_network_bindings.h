#ifndef JS_NETWORK_BINDINGS_H
#define JS_NETWORK_BINDINGS_H

#include "quickjs.h"
#include "network_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct JsNetworkContext JsNetworkContext;

    JsNetworkContext *js_network_init(JSContext *ctx, const char *project_dir);

    NetworkContext *js_network_context(JsNetworkContext *state);

    void js_network_poll(JsNetworkContext *state);
    
    bool js_network_has_pending_work(JsNetworkContext *state);

    void js_network_cleanup(JsNetworkContext *state);

#ifdef __cplusplus
}
#endif

#endif