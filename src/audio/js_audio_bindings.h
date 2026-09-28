#ifndef JS_AUDIO_BINDINGS_H
#define JS_AUDIO_BINDINGS_H

#include "quickjs.h"
#include "audio_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct JsAudioContext JsAudioContext;

    JsAudioContext *js_audio_init(JSContext *ctx, const char *asset_root);

    void js_audio_cleanup(JsAudioContext *state);

#ifdef __cplusplus
}
#endif

#endif