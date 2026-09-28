#include "js_neural_bindings.h"
#include "neural_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NeuralContext *js_neural_context(JSContext *ctx,
                                        JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);
    NeuralContext *neural_ctx = NULL;

    if (data && size == sizeof(neural_ctx))
        memcpy(&neural_ctx, data, sizeof(neural_ctx));
    return neural_ctx;
}

static const char *dtype_to_string(NeuralDtype dtype)
{
    switch (dtype)
    {
    case NEURAL_DTYPE_FLOAT32:
        return "float32";
    case NEURAL_DTYPE_INT32:
        return "int32";
    case NEURAL_DTYPE_INT64:
        return "int64";
    case NEURAL_DTYPE_UINT8:
        return "uint8";
    default:
        return "unknown";
    }
}

static bool extract_typed_array(JSContext *ctx, JSValue ta,
                                const void **out_data,
                                size_t *out_byte_count,
                                NeuralDtype *out_dtype,
                                JSValue *out_ab_ref)
{
    *out_ab_ref = JS_UNDEFINED;

    size_t byte_offset, byte_length, bytes_per_element;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, ta, &byte_offset,
                                        &byte_length, &bytes_per_element);
    if (JS_IsException(ab))
        return false;

    size_t ab_size;
    uint8_t *raw = JS_GetArrayBuffer(ctx, &ab_size, ab);
    if (!raw)
    {
        JS_FreeValue(ctx, ab);
        return false;
    }

    NeuralDtype dtype;
    if (bytes_per_element == 1)
    {
        dtype = NEURAL_DTYPE_UINT8;
    }
    else if (bytes_per_element == 8)
    {
        dtype = NEURAL_DTYPE_INT64;
    }
    else if (bytes_per_element == 4)
    {
        JSValue ctor = JS_GetPropertyStr(ctx, ta, "constructor");
        JSValue name_val = JS_GetPropertyStr(ctx, ctor, "name");
        const char *cname = JS_ToCString(ctx, name_val);
        dtype = (cname && strcmp(cname, "Float32Array") == 0)
                    ? NEURAL_DTYPE_FLOAT32
                    : NEURAL_DTYPE_INT32;
        JS_FreeCString(ctx, cname);
        JS_FreeValue(ctx, name_val);
        JS_FreeValue(ctx, ctor);
    }
    else
    {
        dtype = NEURAL_DTYPE_UNKNOWN;
    }

    *out_data = raw + byte_offset;
    *out_byte_count = byte_length;
    *out_dtype = dtype;
    *out_ab_ref = ab; 
    return true;
}

static const uint8_t *extract_buffer(JSContext *ctx, JSValue val,
                                     size_t *out_byte_count,
                                     JSValue *out_ab_ref)
{
    *out_ab_ref = JS_UNDEFINED;
    *out_byte_count = 0;

    size_t byte_offset = 0, byte_length = 0, bpe = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bpe);
    if (!JS_IsException(ab))
    {
        size_t ab_size = 0;
        uint8_t *raw = JS_GetArrayBuffer(ctx, &ab_size, ab);
        if (raw)
        {
            *out_byte_count = byte_length;
            *out_ab_ref = ab;
            return raw + byte_offset;
        }
        JS_FreeValue(ctx, ab);
    }

    size_t ab_size = 0;
    uint8_t *raw = JS_GetArrayBuffer(ctx, &ab_size, val);
    if (raw)
    {
        *out_byte_count = ab_size;
        return raw;
    }
    return NULL;
}

static JSValue make_output_typed_array(JSContext *ctx, const NeuralTensor *t)
{
    const char *ctor_name;
    switch (t->dtype)
    {
    case NEURAL_DTYPE_FLOAT32:
        ctor_name = "Float32Array";
        break;
    case NEURAL_DTYPE_INT32:
        ctor_name = "Int32Array";
        break;
    case NEURAL_DTYPE_INT64:
        ctor_name = "BigInt64Array";
        break;
    case NEURAL_DTYPE_UINT8:
        ctor_name = "Uint8Array";
        break;
    default:
        ctor_name = "Uint8Array";
        break;
    }

    JSValue ab = JS_NewArrayBufferCopy(ctx, (const uint8_t *)t->data,
                                       t->byte_count);
    if (JS_IsException(ab))
        return ab;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, ctor_name);
    JS_FreeValue(ctx, global);

    JSValue ta = JS_CallConstructor(ctx, ctor, 1, &ab);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, ab);
    return ta;
}

static JSValue tensor_info_to_js(JSContext *ctx, const NeuralTensorInfo *ti)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, ti->name));
    JS_SetPropertyStr(ctx, obj, "dtype", JS_NewString(ctx, dtype_to_string(ti->dtype)));

    JSValue shape_arr = JS_NewArray(ctx);
    for (int i = 0; i < ti->rank; i++)
        JS_SetPropertyUint32(ctx, shape_arr, i, JS_NewInt64(ctx, ti->shape[i]));
    JS_SetPropertyStr(ctx, obj, "shape", shape_arr);

    return obj;
}

static JSValue js_neural_is_available(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    (void)func_data;
    return JS_NewBool(ctx, neural_is_available());
}

static JSValue js_neural_get_error(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv, int magic,
                                   JSValueConst *func_data)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx)
        return JS_NewString(ctx, "");
    return JS_NewString(ctx, neural_get_error(neural_ctx));
}

static JSValue js_neural_load_model(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic,
                                    JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_NewInt32(ctx, -1);

    ApiError error;
    int model_id = neural_service_load_model(neural_ctx, path, &error);
    JS_FreeCString(ctx, path);

    if (model_id < 0)
        return JS_ThrowInternalError(ctx, "%s", neural_get_error(neural_ctx));

    return JS_NewInt32(ctx, model_id);
}

static JSValue js_neural_load_model_from_buffer(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv, int magic,
                                                JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx || argc < 1)
        return JS_NewInt32(ctx, -1);

    size_t byte_count = 0;
    JSValue ab_ref = JS_UNDEFINED;
    const uint8_t *data = extract_buffer(ctx, argv[0], &byte_count, &ab_ref);
    if (!data)
        return JS_ThrowTypeError(ctx, "neural.loadModelFromBuffer requires an ArrayBuffer or TypedArray");

    int model_id = neural_load_model_from_buffer(neural_ctx, data, byte_count);
    if (!JS_IsUndefined(ab_ref))
        JS_FreeValue(ctx, ab_ref);

    if (model_id < 0)
        return JS_ThrowInternalError(ctx, "%s", neural_get_error(neural_ctx));

    return JS_NewInt32(ctx, model_id);
}

static JSValue js_neural_unload_model(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx || argc < 1)
        return JS_UNDEFINED;

    int model_id;
    JS_ToInt32(ctx, &model_id, argv[0]);
    neural_unload_model(neural_ctx, model_id);
    return JS_UNDEFINED;
}

static JSValue js_neural_get_model_info(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic,
                                        JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx || argc < 1)
        return JS_NULL;

    int model_id;
    JS_ToInt32(ctx, &model_id, argv[0]);

    NeuralModelInfo info;
    if (!neural_get_model_info(neural_ctx, model_id, &info))
        return JS_ThrowInternalError(ctx, "%s", neural_get_error(neural_ctx));

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "description", JS_NewString(ctx, info.description));
    JS_SetPropertyStr(ctx, obj, "producerName", JS_NewString(ctx, info.producer_name));
    JS_SetPropertyStr(ctx, obj, "graphName", JS_NewString(ctx, info.graph_name));
    JS_SetPropertyStr(ctx, obj, "domain", JS_NewString(ctx, info.domain));
    JS_SetPropertyStr(ctx, obj, "version", JS_NewInt64(ctx, info.version));

    JSValue inputs_arr = JS_NewArray(ctx);
    for (int i = 0; i < info.input_count; i++)
        JS_SetPropertyUint32(ctx, inputs_arr, i,
                             tensor_info_to_js(ctx, &info.inputs[i]));
    JS_SetPropertyStr(ctx, obj, "inputs", inputs_arr);

    JSValue outputs_arr = JS_NewArray(ctx);
    for (int i = 0; i < info.output_count; i++)
        JS_SetPropertyUint32(ctx, outputs_arr, i,
                             tensor_info_to_js(ctx, &info.outputs[i]));
    JS_SetPropertyStr(ctx, obj, "outputs", outputs_arr);

    return obj;
}

static JSValue js_neural_run(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int magic,
                             JSValueConst *func_data)
{
    (void)this_val;
    (void)magic;
    NeuralContext *neural_ctx = js_neural_context(ctx, func_data);
    if (!neural_ctx || argc < 2)
        return JS_ThrowTypeError(ctx, "neural.run() requires (modelId, inputs)");

    int model_id;
    JS_ToInt32(ctx, &model_id, argv[0]);
    JSValue inputs_obj = argv[1];

    NeuralModelInfo info;
    if (!neural_get_model_info(neural_ctx, model_id, &info))
        return JS_ThrowInternalError(ctx, "%s", neural_get_error(neural_ctx));

    NeuralTensor in_tensors[NEURAL_MAX_TENSORS];
    JSValue ab_refs[NEURAL_MAX_TENSORS];
    int ab_ref_count = 0;
    memset(in_tensors, 0, sizeof(in_tensors));

    for (int i = 0; i < info.input_count; i++)
    {
        const char *name = info.inputs[i].name;
        JSValue ta = JS_GetPropertyStr(ctx, inputs_obj, name);

        if (JS_IsUndefined(ta) || JS_IsNull(ta) || JS_IsException(ta))
        {
            JS_FreeValue(ctx, ta);
            for (int j = 0; j < ab_ref_count; j++)
                JS_FreeValue(ctx, ab_refs[j]);
            return JS_ThrowTypeError(ctx,
                                     "neural.run(): missing input tensor '%s'", name);
        }

        const void *data;
        size_t byte_count;
        NeuralDtype dtype;
        JSValue ab_ref;

        if (!extract_typed_array(ctx, ta, &data, &byte_count, &dtype, &ab_ref))
        {
            JS_FreeValue(ctx, ta);
            for (int j = 0; j < ab_ref_count; j++)
                JS_FreeValue(ctx, ab_refs[j]);
            return JS_ThrowTypeError(ctx,
                                     "neural.run(): input '%s' is not a TypedArray", name);
        }
        JS_FreeValue(ctx, ta);

        in_tensors[i].data = data;
        in_tensors[i].byte_count = byte_count;
        in_tensors[i].dtype = dtype;
        in_tensors[i].rank = info.inputs[i].rank;
        memcpy(in_tensors[i].shape, info.inputs[i].shape,
               sizeof(int64_t) * (size_t)info.inputs[i].rank);

        size_t bpe = (byte_count > 0 && info.inputs[i].rank > 0)
                         ? byte_count 
                         : 1;
        
        switch (dtype)
        {
        case NEURAL_DTYPE_INT64:
            bpe = 8;
            break;
        case NEURAL_DTYPE_FLOAT32:
        case NEURAL_DTYPE_INT32:
            bpe = 4;
            break;
        case NEURAL_DTYPE_UINT8:
            bpe = 1;
            break;
        default:
            bpe = 1;
            break;
        }
        size_t elem_count = (bpe > 0) ? byte_count / bpe : 0;
        int64_t static_prod = 1;
        int dynamic_idx = -1;
        for (int d = 0; d < in_tensors[i].rank; d++)
        {
            if (in_tensors[i].shape[d] < 0)
                dynamic_idx = d;
            else
                static_prod *= in_tensors[i].shape[d];
        }
        if (dynamic_idx >= 0 && static_prod > 0)
            in_tensors[i].shape[dynamic_idx] = (int64_t)(elem_count / (size_t)static_prod);

        ab_refs[ab_ref_count++] = ab_ref;
    }

    NeuralTensor out_tensors[NEURAL_MAX_TENSORS];
    memset(out_tensors, 0, sizeof(out_tensors));

    bool ok = neural_run(neural_ctx, model_id,
                         in_tensors, info.input_count,
                         out_tensors, info.output_count);

    for (int i = 0; i < ab_ref_count; i++)
        JS_FreeValue(ctx, ab_refs[i]);

    if (!ok)
        return JS_ThrowInternalError(ctx, "%s", neural_get_error(neural_ctx));

    JSValue result = JS_NewObject(ctx);
    for (int i = 0; i < info.output_count; i++)
    {
        JSValue ta = make_output_typed_array(ctx, &out_tensors[i]);
        if (JS_IsException(ta))
        {
            JS_FreeValue(ctx, result);
            return ta;
        }
        JS_SetPropertyStr(ctx, result, info.outputs[i].name, ta);
    }

    return result;
}

typedef struct JsNeuralFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsNeuralFunction;

static const JsNeuralFunction js_neural_funcs[] = {
    {"isAvailable", 0, js_neural_is_available},
    {"getError", 0, js_neural_get_error},
    {"loadModel", 1, js_neural_load_model},
    {"loadModelFromBuffer", 1, js_neural_load_model_from_buffer},
    {"unloadModel", 1, js_neural_unload_model},
    {"getModelInfo", 1, js_neural_get_model_info},
    {"run", 2, js_neural_run},
};

static int js_neural_add_function(JSContext *ctx, JSValue neural_obj,
                                  const JsNeuralFunction *definition,
                                  NeuralContext *neural_ctx)
{
    JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)&neural_ctx,
                                         sizeof(neural_ctx));
    if (JS_IsException(data))
        return -1;

    JSValue function = JS_NewCFunctionData(ctx, definition->callback,
                                           definition->length, 0, 1, &data);
    JS_FreeValue(ctx, data);
    if (JS_IsException(function))
        return -1;

    return JS_SetPropertyStr(ctx, neural_obj, definition->name, function);
}

void js_neural_init(JSContext *ctx, NeuralContext *neural_ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");
    JSValue neural_obj = JS_NewObject(ctx);

    for (size_t index = 0;
         index < sizeof(js_neural_funcs) / sizeof(js_neural_funcs[0]);
         index++)
        js_neural_add_function(ctx, neural_obj, &js_neural_funcs[index],
                               neural_ctx);

    JS_SetPropertyStr(ctx, sys_obj, "neural", neural_obj);
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    static const char k_neural_staged_shim[] =
        "(function(n){\n"
        "  if (!n) return;\n"
        "  n._stagedInputs = {};\n"
        "  n._stagedOutputs = {};\n"
        "  n.setInput = function(modelId, name, data, _shape) {\n"
        "    var m = this._stagedInputs[modelId] || (this._stagedInputs[modelId] = {});\n"
        "    m[name] = data;\n"
        "  };\n"
        "  n.getOutput = function(modelId, name) {\n"
        "    var o = this._stagedOutputs[modelId];\n"
        "    return o ? o[name] : undefined;\n"
        "  };\n"
        "  var _origRun = n.run.bind(n);\n"
        "  n.run = function(modelId, inputs) {\n"
        "    if (inputs === undefined) inputs = this._stagedInputs[modelId] || {};\n"
        "    var outputs = _origRun(modelId, inputs);\n"
        "    this._stagedOutputs[modelId] = outputs;\n"
        "    return outputs;\n"
        "  };\n"
        "  var _origUnload = n.unloadModel.bind(n);\n"
        "  n.unloadModel = function(modelId) {\n"
        "    delete this._stagedInputs[modelId];\n"
        "    delete this._stagedOutputs[modelId];\n"
        "    return _origUnload(modelId);\n"
        "  };\n"
        "})(sys.neural);\n";
    JSValue r = JS_Eval(ctx, k_neural_staged_shim, sizeof(k_neural_staged_shim) - 1,
                        "<neural-shim>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
    {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        fprintf(stderr, "[neural] failed to install staged shim: %s\n",
                msg ? msg : "<unknown>");
        if (msg)
            JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, r);
}