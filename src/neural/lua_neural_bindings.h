#ifndef LUA_NEURAL_BINDINGS_H
#define LUA_NEURAL_BINDINGS_H

#include "neural_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void lua_neural_init(void *L, NeuralContext *neural_ctx);

#ifdef __cplusplus
}
#endif

#endif