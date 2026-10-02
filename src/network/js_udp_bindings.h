#ifndef JS_UDP_BINDINGS_H
#define JS_UDP_BINDINGS_H

#include "quickjs.h"
#include "udp_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct JsUdpContext JsUdpContext;

    JsUdpContext *js_udp_init(JSContext *ctx);

    UdpContext *js_udp_context(JsUdpContext *state);

    void js_udp_cleanup(JsUdpContext *state);

    void js_udp_poll(JsUdpContext *state);
    
    bool js_udp_has_pending_work(JsUdpContext *state);

#ifdef __cplusplus
}
#endif

#endif