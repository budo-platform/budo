#ifndef JS_NEURAL_BINDINGS_H
#define JS_NEURAL_BINDINGS_H

#include "quickjs.h"
#include "neural_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void js_neural_init(JSContext *ctx, NeuralContext *neural_ctx);

#ifdef __cplusplus
}
#endif

#endif