#include "neural_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BUDO_NEURAL

bool neural_is_available(void) { return false; }

NeuralContext *neural_create(const char *project_dir)
{
    (void)project_dir;
    return NULL;
}

void neural_destroy(NeuralContext *ctx) { (void)ctx; }

int neural_load_model(NeuralContext *ctx, const char *rel_path)
{
    (void)ctx;
    (void)rel_path;
    return -1;
}

int neural_load_model_from_buffer(NeuralContext *ctx, const void *data, size_t size)
{
    (void)ctx;
    (void)data;
    (void)size;
    return -1;
}

void neural_unload_model(NeuralContext *ctx, int model_id)
{
    (void)ctx;
    (void)model_id;
}

bool neural_get_model_info(NeuralContext *ctx, int model_id,
                           NeuralModelInfo *out)
{
    (void)ctx;
    (void)model_id;
    (void)out;
    return false;
}

bool neural_run(NeuralContext *ctx, int model_id,
                const NeuralTensor *inputs, int n_inputs,
                NeuralTensor *outputs, int n_outputs)
{
    (void)ctx;
    (void)model_id;
    (void)inputs;
    (void)n_inputs;
    (void)outputs;
    (void)n_outputs;
    return false;
}

const char *neural_get_error(NeuralContext *ctx)
{
    (void)ctx;
    return "";
}

#else 

#include "onnxruntime_c_api.h"

#if defined(__APPLE__) && __has_include("onnxruntime_coreml_provider_factory.h")
#include "onnxruntime_coreml_provider_factory.h"
#define NEURAL_HAVE_COREML 1
#endif

#if defined(__ANDROID__) && __has_include("onnxruntime_nnapi_provider_factory.h")
#include "onnxruntime_nnapi_provider_factory.h"
#define NEURAL_HAVE_NNAPI 1
#endif

#if !defined(_WIN32)
#include <dlfcn.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef const OrtApiBase *(*fn_OrtGetApiBase)(void);
#ifdef NEURAL_HAVE_COREML
typedef OrtStatus *(*fn_CoreML)(OrtSessionOptions *, uint32_t);
#endif
#ifdef NEURAL_HAVE_NNAPI
typedef OrtStatus *(*fn_Nnapi)(OrtSessionOptions *, uint32_t);
#endif

static void *g_ort_dl = NULL; 
static fn_OrtGetApiBase g_OrtGetApiBase = NULL;
#ifdef NEURAL_HAVE_COREML
static fn_CoreML g_CoreML = NULL;
#endif
#ifdef NEURAL_HAVE_NNAPI
static fn_Nnapi g_Nnapi = NULL;
#endif

static bool ort_dl_load(void)
{
    if (g_ort_dl)
        return g_OrtGetApiBase != NULL;

#if defined(__APPLE__)
    static const char *const kNames[] = {
        "libonnxruntime.dylib", "libonnxruntime.1.dylib", NULL};
#elif defined(_WIN32)
    static const char *const kNames[] = {"onnxruntime.dll", NULL};
#else
    static const char *const kNames[] = {
        "libonnxruntime.so", "libonnxruntime.so.1", NULL};
#endif

    for (int i = 0; kNames[i] && !g_ort_dl; ++i)
    {
#if !defined(_WIN32)
        g_ort_dl = dlopen(kNames[i], RTLD_LAZY | RTLD_LOCAL);
#else
        g_ort_dl = (void *)LoadLibraryA(kNames[i]);
#endif
    }

#if defined(ORT_RUNTIME_PATH) && !defined(_WIN32)
    if (!g_ort_dl)
        g_ort_dl = dlopen(ORT_RUNTIME_PATH, RTLD_LAZY | RTLD_LOCAL);
#elif defined(ORT_RUNTIME_PATH) && defined(_WIN32)
    if (!g_ort_dl)
        g_ort_dl = (void *)LoadLibraryA(ORT_RUNTIME_PATH);
#endif

    if (!g_ort_dl)
        return false;

#if !defined(_WIN32)
    g_OrtGetApiBase = (fn_OrtGetApiBase)dlsym(g_ort_dl, "OrtGetApiBase");
#ifdef NEURAL_HAVE_COREML
    g_CoreML = (fn_CoreML)dlsym(g_ort_dl,
                                "OrtSessionOptionsAppendExecutionProvider_CoreML");
#endif
#ifdef NEURAL_HAVE_NNAPI
    g_Nnapi = (fn_Nnapi)dlsym(g_ort_dl,
                              "OrtSessionOptionsAppendExecutionProvider_Nnapi");
#endif
#else
    g_OrtGetApiBase = (fn_OrtGetApiBase)GetProcAddress((HMODULE)g_ort_dl,
                                                       "OrtGetApiBase");
#endif

    if (!g_OrtGetApiBase)
    {
#if !defined(_WIN32)
        dlclose(g_ort_dl);
#else
        FreeLibrary((HMODULE)g_ort_dl);
#endif
        g_ort_dl = NULL;
        return false;
    }
    return true;
}

static void *read_file_buffer(const char *path, size_t *out_size)
{
    FILE *file;
    long size;
    void *buffer;

    if (out_size)
        *out_size = 0;
    if (!path || !path[0])
        return NULL;

    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        return NULL;
    }
    rewind(file);

    buffer = malloc((size_t)size);
    if (!buffer)
    {
        fclose(file);
        return NULL;
    }
    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size)
    {
        free(buffer);
        fclose(file);
        return NULL;
    }
    fclose(file);
    if (out_size)
        *out_size = (size_t)size;
    return buffer;
}

typedef struct
{
    void *data;      
    size_t capacity; 
    size_t size;     
} OutputBuffer;

typedef struct
{
    bool active;
    OrtSession *session;
    
    NeuralModelInfo info;
    
    OutputBuffer out_bufs[NEURAL_MAX_TENSORS];
} ModelSlot;

struct NeuralContext
{
    char project_dir[4096];
    char last_error[1024];

    const OrtApi *api;
    OrtEnv *env;
    OrtSessionOptions *default_opts;

    ModelSlot slots[NEURAL_MAX_MODELS];

    NeuralContext *next_live;
};

static const OrtApi *g_ort_api = NULL;

static NeuralContext *g_neural_live_contexts = NULL;
static bool g_neural_atexit_registered = false;

static void neural_track_live(NeuralContext *ctx)
{
    ctx->next_live = g_neural_live_contexts;
    g_neural_live_contexts = ctx;
}

static void neural_untrack_live(NeuralContext *ctx)
{
    for (NeuralContext **link = &g_neural_live_contexts; *link; link = &(*link)->next_live)
    {
        if (*link == ctx)
        {
            *link = ctx->next_live;
            ctx->next_live = NULL;
            return;
        }
    }
}

static void neural_atexit_release(void)
{
    
    while (g_neural_live_contexts)
        neural_destroy(g_neural_live_contexts);
}

static void ort_log_callback(void *param, OrtLoggingLevel severity,
                             const char *category, const char *logid,
                             const char *code_location, const char *message)
{
    (void)param;
    (void)code_location;
    if (severity >= ORT_LOGGING_LEVEL_WARNING)
    {
        fprintf(stderr, "[neural/%s/%s] %s\n",
                category ? category : "?",
                logid ? logid : "?",
                message ? message : "");
    }
}

static void capture_ort_error(NeuralContext *ctx, OrtStatus *status)
{
    if (!status)
        return;
    const char *msg = ctx->api->GetErrorMessage(status);
    if (msg)
        snprintf(ctx->last_error, sizeof(ctx->last_error), "%s", msg);
    else
        snprintf(ctx->last_error, sizeof(ctx->last_error), "unknown ORT error");
    ctx->api->ReleaseStatus(status);
}

#define ORT_CHECK(call)                 \
    do                                  \
    {                                   \
        OrtStatus *_s = (call);         \
        if (_s)                         \
        {                               \
            capture_ort_error(ctx, _s); \
            goto fail;                  \
        }                               \
    } while (0)

static bool resolve_model_path(NeuralContext *ctx, const char *rel_path,
                               char *out, size_t out_size)
{
    if (!rel_path || rel_path[0] == '\0')
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "model path is empty");
        return false;
    }

    if (rel_path[0] == '/')
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "absolute model paths are not allowed");
        return false;
    }

    const char *p = rel_path;
    while (*p)
    {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
        {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "path traversal is not allowed in model path");
            return false;
        }
        while (*p && *p != '/')
            p++;
        while (*p == '/')
            p++;
    }

    size_t len = strlen(rel_path);
    if (len < 5 || strcmp(rel_path + len - 5, ".onnx") != 0)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "only .onnx model files are supported (got: %s)", rel_path);
        return false;
    }

    snprintf(out, out_size, "%s/%s", ctx->project_dir, rel_path);
    return true;
}

static NeuralDtype ort_type_to_neural(ONNXTensorElementDataType t)
{
    switch (t)
    {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
        return NEURAL_DTYPE_FLOAT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
        return NEURAL_DTYPE_INT32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
        return NEURAL_DTYPE_INT64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
        return NEURAL_DTYPE_UINT8;
    default:
        return NEURAL_DTYPE_UNKNOWN;
    }
}

static ONNXTensorElementDataType neural_type_to_ort(NeuralDtype d)
{
    switch (d)
    {
    case NEURAL_DTYPE_FLOAT32:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    case NEURAL_DTYPE_INT32:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;
    case NEURAL_DTYPE_INT64:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    case NEURAL_DTYPE_UINT8:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
    default:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    }
}

static size_t neural_dtype_element_size(NeuralDtype d)
{
    switch (d)
    {
    case NEURAL_DTYPE_FLOAT32:
        return 4;
    case NEURAL_DTYPE_INT32:
        return 4;
    case NEURAL_DTYPE_INT64:
        return 8;
    case NEURAL_DTYPE_UINT8:
        return 1;
    default:
        return 0;
    }
}

static bool populate_model_info(NeuralContext *ctx, ModelSlot *slot)
{
    const OrtApi *api = ctx->api;
    NeuralModelInfo *info = &slot->info;
    OrtModelMetadata *meta = NULL;
    OrtTypeInfo *type_info = NULL;

    memset(info, 0, sizeof(*info));

    OrtStatus *s = api->SessionGetModelMetadata(slot->session, &meta);
    if (!s)
    {
        char *buf = NULL;
        OrtAllocator *alloc = NULL;
        (void)api->GetAllocatorWithDefaultOptions(&alloc);

        if (!api->ModelMetadataGetProducerName(meta, alloc, &buf) && buf)
        {
            snprintf(info->producer_name, sizeof(info->producer_name), "%s", buf);
            (void)api->AllocatorFree(alloc, buf);
            buf = NULL;
        }
        if (!api->ModelMetadataGetGraphName(meta, alloc, &buf) && buf)
        {
            snprintf(info->graph_name, sizeof(info->graph_name), "%s", buf);
            (void)api->AllocatorFree(alloc, buf);
            buf = NULL;
        }
        if (!api->ModelMetadataGetDomain(meta, alloc, &buf) && buf)
        {
            snprintf(info->domain, sizeof(info->domain), "%s", buf);
            (void)api->AllocatorFree(alloc, buf);
            buf = NULL;
        }
        if (!api->ModelMetadataGetDescription(meta, alloc, &buf) && buf)
        {
            snprintf(info->description, sizeof(info->description), "%s", buf);
            (void)api->AllocatorFree(alloc, buf);
            buf = NULL;
        }
        (void)api->ModelMetadataGetVersion(meta, &info->version);
        api->ReleaseModelMetadata(meta);
    }
    else
    {
        
        api->ReleaseStatus(s);
    }

    size_t input_count = 0;
    if (api->SessionGetInputCount(slot->session, &input_count) != NULL)
        goto done;
    info->input_count = (int)(input_count < NEURAL_MAX_TENSORS
                                  ? input_count
                                  : NEURAL_MAX_TENSORS);

    OrtAllocator *alloc = NULL;
    (void)api->GetAllocatorWithDefaultOptions(&alloc);

    for (int i = 0; i < info->input_count; i++)
    {
        NeuralTensorInfo *ti = &info->inputs[i];

        char *name = NULL;
        s = api->SessionGetInputName(slot->session, (size_t)i, alloc, &name);
        if (!s && name)
        {
            snprintf(ti->name, sizeof(ti->name), "%s", name);
            (void)api->AllocatorFree(alloc, name);
        }
        else if (s)
        {
            api->ReleaseStatus(s);
        }

        s = api->SessionGetInputTypeInfo(slot->session, (size_t)i, &type_info);
        if (!s)
        {
            const OrtTensorTypeAndShapeInfo *ts = NULL;
            (void)api->CastTypeInfoToTensorInfo(type_info, &ts);
            if (ts)
            {
                ONNXTensorElementDataType elem_type;
                (void)api->GetTensorElementType(ts, &elem_type);
                ti->dtype = ort_type_to_neural(elem_type);

                size_t rank = 0;
                (void)api->GetDimensionsCount(ts, &rank);
                ti->rank = (int)(rank < NEURAL_MAX_RANK ? rank : NEURAL_MAX_RANK);
                (void)api->GetDimensions(ts, ti->shape, (size_t)ti->rank);
            }
            api->ReleaseTypeInfo(type_info);
            type_info = NULL;
        }
        else
        {
            api->ReleaseStatus(s);
        }
    }

    size_t output_count = 0;
    if (api->SessionGetOutputCount(slot->session, &output_count) != NULL)
        goto done;
    info->output_count = (int)(output_count < NEURAL_MAX_TENSORS
                                   ? output_count
                                   : NEURAL_MAX_TENSORS);

    for (int i = 0; i < info->output_count; i++)
    {
        NeuralTensorInfo *ti = &info->outputs[i];

        char *name = NULL;
        s = api->SessionGetOutputName(slot->session, (size_t)i, alloc, &name);
        if (!s && name)
        {
            snprintf(ti->name, sizeof(ti->name), "%s", name);
            (void)api->AllocatorFree(alloc, name);
        }
        else if (s)
        {
            api->ReleaseStatus(s);
        }

        s = api->SessionGetOutputTypeInfo(slot->session, (size_t)i, &type_info);
        if (!s)
        {
            const OrtTensorTypeAndShapeInfo *ts = NULL;
            (void)api->CastTypeInfoToTensorInfo(type_info, &ts);
            if (ts)
            {
                ONNXTensorElementDataType elem_type;
                (void)api->GetTensorElementType(ts, &elem_type);
                ti->dtype = ort_type_to_neural(elem_type);

                size_t rank = 0;
                (void)api->GetDimensionsCount(ts, &rank);
                ti->rank = (int)(rank < NEURAL_MAX_RANK ? rank : NEURAL_MAX_RANK);
                (void)api->GetDimensions(ts, ti->shape, (size_t)ti->rank);
            }
            api->ReleaseTypeInfo(type_info);
            type_info = NULL;
        }
        else
        {
            api->ReleaseStatus(s);
        }
    }

done:
    return true;
}

static void free_slot_output_buffers(ModelSlot *slot)
{
    for (int i = 0; i < NEURAL_MAX_TENSORS; i++)
    {
        free(slot->out_bufs[i].data);
        slot->out_bufs[i].data = NULL;
        slot->out_bufs[i].capacity = 0;
        slot->out_bufs[i].size = 0;
    }
}

static bool ensure_output_buffer(OutputBuffer *buf, size_t need)
{
    if (buf->capacity >= need)
        return true;

    free(buf->data);
    buf->data = malloc(need);
    if (!buf->data)
    {
        buf->capacity = 0;
        buf->size = 0;
        return false;
    }
    buf->capacity = need;
    return true;
}

bool neural_is_available(void) { return ort_dl_load(); }

NeuralContext *neural_create(const char *project_dir)
{
    
    if (!g_ort_api)
    {
        if (!ort_dl_load())
        {
            fprintf(stderr, "neural: libonnxruntime not found — "
                            "install it or place it on the library search path\n");
            return NULL;
        }
        g_ort_api = g_OrtGetApiBase()->GetApi(ORT_API_VERSION);
    }

    if (!g_ort_api)
    {
        fprintf(stderr, "neural: failed to obtain ORT API (version %u)\n",
                ORT_API_VERSION);
        return NULL;
    }

    NeuralContext *ctx = (NeuralContext *)calloc(1, sizeof(NeuralContext));
    if (!ctx)
        return NULL;

    ctx->api = g_ort_api;

    snprintf(ctx->project_dir, sizeof(ctx->project_dir), "%s",
             project_dir ? project_dir : ".");

    OrtStatus *s = ctx->api->CreateEnvWithCustomLogger(
        ort_log_callback,
        NULL, 
        ORT_LOGGING_LEVEL_WARNING,
        "budo",
        &ctx->env);
    if (s)
    {
        capture_ort_error(ctx, s);
        fprintf(stderr, "neural: failed to create ORT env: %s\n",
                ctx->last_error);
        free(ctx);
        return NULL;
    }

    s = ctx->api->CreateSessionOptions(&ctx->default_opts);
    if (s)
    {
        capture_ort_error(ctx, s);
        fprintf(stderr, "neural: failed to create session options: %s\n",
                ctx->last_error);
        ctx->api->ReleaseEnv(ctx->env);
        free(ctx);
        return NULL;
    }

    (void)ctx->api->SetIntraOpNumThreads(ctx->default_opts, 1);
    (void)ctx->api->SetInterOpNumThreads(ctx->default_opts, 1);

    if (!g_neural_atexit_registered)
    {
        g_neural_atexit_registered = true;
        atexit(neural_atexit_release);
    }
    neural_track_live(ctx);

#if defined(NEURAL_HAVE_COREML)
    
    if (g_CoreML)
    {
        OrtStatus *coreml_s = g_CoreML(ctx->default_opts, 0);
        if (coreml_s)
        {
            
            ctx->api->ReleaseStatus(coreml_s);
        }
    }
#endif

#if defined(NEURAL_HAVE_NNAPI)
    
    if (g_Nnapi)
    {
        OrtStatus *nnapi_s = g_Nnapi(ctx->default_opts, 0);
        if (nnapi_s)
        {
            ctx->api->ReleaseStatus(nnapi_s);
        }
    }
#endif

    return ctx;
}

void neural_destroy(NeuralContext *ctx)
{
    if (!ctx)
        return;

    neural_untrack_live(ctx);

    const OrtApi *api = ctx->api;

    for (int i = 0; i < NEURAL_MAX_MODELS; i++)
    {
        if (ctx->slots[i].active)
        {
            free_slot_output_buffers(&ctx->slots[i]);
            if (ctx->slots[i].session)
                api->ReleaseSession(ctx->slots[i].session);
            ctx->slots[i].active = false;
            ctx->slots[i].session = NULL;
        }
    }

    if (ctx->default_opts)
        api->ReleaseSessionOptions(ctx->default_opts);
    if (ctx->env)
        api->ReleaseEnv(ctx->env);

    free(ctx);
}

int neural_load_model(NeuralContext *ctx, const char *rel_path)
{
    if (!ctx)
        return -1;

    char full_path[4096 + 256];
    if (!resolve_model_path(ctx, rel_path, full_path, sizeof(full_path)))
        return -1;

    size_t size = 0;
    void *data = read_file_buffer(full_path, &size);
    if (!data)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "failed to read model file: %s", rel_path ? rel_path : "(null)");
        return -1;
    }

    int model_id = neural_load_model_from_buffer(ctx, data, size);
    free(data);
    return model_id;
}

int neural_load_model_from_buffer(NeuralContext *ctx, const void *data, size_t size)
{
    if (!ctx)
        return -1;

    if (!data || size == 0)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error), "empty model buffer");
        return -1;
    }

    int slot_idx = -1;
    for (int i = 0; i < NEURAL_MAX_MODELS; i++)
    {
        if (!ctx->slots[i].active)
        {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "maximum model limit (%d) reached", NEURAL_MAX_MODELS);
        return -1;
    }

    ModelSlot *slot = &ctx->slots[slot_idx];
    memset(slot, 0, sizeof(*slot));

    OrtStatus *s = ctx->api->CreateSessionFromArray(
        ctx->env, data, size, ctx->default_opts, &slot->session);
    if (s)
    {
        capture_ort_error(ctx, s);
        return -1;
    }

    if (!populate_model_info(ctx, slot))
    {
        ctx->api->ReleaseSession(slot->session);
        slot->session = NULL;
        return -1;
    }

    slot->active = true;
    return slot_idx;
}

void neural_unload_model(NeuralContext *ctx, int model_id)
{
    if (!ctx)
        return;
    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return;

    ModelSlot *slot = &ctx->slots[model_id];
    if (!slot->active)
        return;

    free_slot_output_buffers(slot);
    if (slot->session)
        ctx->api->ReleaseSession(slot->session);

    memset(slot, 0, sizeof(*slot));
}

bool neural_get_model_info(NeuralContext *ctx, int model_id,
                           NeuralModelInfo *out)
{
    if (!ctx || !out)
        return false;
    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return false;

    ModelSlot *slot = &ctx->slots[model_id];
    if (!slot->active)
        return false;

    *out = slot->info;
    return true;
}

bool neural_run(NeuralContext *ctx, int model_id,
                const NeuralTensor *inputs, int n_inputs,
                NeuralTensor *outputs, int n_outputs)
{
    if (!ctx || !inputs || !outputs)
        return false;
    if (model_id < 0 || model_id >= NEURAL_MAX_MODELS)
        return false;

    ModelSlot *slot = &ctx->slots[model_id];
    if (!slot->active)
        return false;

    const OrtApi *api = ctx->api;
    OrtMemoryInfo *mem = NULL;
    OrtValue **in_vals = NULL;
    OrtValue **out_vals = NULL;

    const char **in_names = NULL;
    const char **out_names = NULL;

    if (n_inputs != slot->info.input_count ||
        n_outputs != slot->info.output_count)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "tensor count mismatch: model expects %d inputs / %d outputs, "
                 "got %d inputs / %d outputs",
                 slot->info.input_count, slot->info.output_count,
                 n_inputs, n_outputs);
        return false;
    }

    in_vals = (OrtValue **)calloc((size_t)n_inputs, sizeof(OrtValue *));
    out_vals = (OrtValue **)calloc((size_t)n_outputs, sizeof(OrtValue *));
    in_names = (const char **)malloc((size_t)n_inputs * sizeof(const char *));
    out_names = (const char **)malloc((size_t)n_outputs * sizeof(const char *));

    if (!in_vals || !out_vals || !in_names || !out_names)
    {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                 "out of memory allocating inference buffers");
        goto fail;
    }

    OrtStatus *s = api->CreateCpuMemoryInfo(
        OrtArenaAllocator, OrtMemTypeDefault, &mem);
    if (s)
    {
        capture_ort_error(ctx, s);
        goto fail;
    }

    for (int i = 0; i < n_inputs; i++)
        in_names[i] = slot->info.inputs[i].name;
    for (int i = 0; i < n_outputs; i++)
        out_names[i] = slot->info.outputs[i].name;

    for (int i = 0; i < n_inputs; i++)
    {
        const NeuralTensor *t = &inputs[i];

        ONNXTensorElementDataType ort_type = neural_type_to_ort(t->dtype);
        if (ort_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED)
        {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "unsupported dtype for input tensor %d", i);
            goto fail;
        }

        s = api->CreateTensorWithDataAsOrtValue(
            mem,
            (void *)t->data, 
            t->byte_count,
            t->shape,
            (size_t)t->rank,
            ort_type,
            &in_vals[i]);
        if (s)
        {
            capture_ort_error(ctx, s);
            goto fail;
        }
    }

    s = api->Run(slot->session,
                 NULL, 
                 in_names, (const OrtValue *const *)in_vals, (size_t)n_inputs,
                 out_names, (size_t)n_outputs, out_vals);
    if (s)
    {
        capture_ort_error(ctx, s);
        goto fail;
    }

    for (int i = 0; i < n_outputs; i++)
    {
        OrtValue *ov = out_vals[i];
        if (!ov)
        {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "ORT returned NULL output for tensor %d", i);
            goto fail;
        }

        OrtTensorTypeAndShapeInfo *ts = NULL;
        s = api->GetTensorTypeAndShape(ov, &ts);
        if (s)
        {
            capture_ort_error(ctx, s);
            goto fail;
        }

        ONNXTensorElementDataType elem_type;
        (void)api->GetTensorElementType(ts, &elem_type);

        size_t rank = 0;
        (void)api->GetDimensionsCount(ts, &rank);
        if (rank > NEURAL_MAX_RANK)
            rank = NEURAL_MAX_RANK;

        int64_t shape[NEURAL_MAX_RANK];
        (void)api->GetDimensions(ts, shape, rank);

        size_t elem_count = 0;
        (void)api->GetTensorShapeElementCount(ts, &elem_count);
        api->ReleaseTensorTypeAndShapeInfo(ts);

        NeuralDtype dtype = ort_type_to_neural(elem_type);
        size_t elem_size = neural_dtype_element_size(dtype);
        if (elem_size == 0)
        {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "unsupported output dtype for tensor %d", i);
            goto fail;
        }

        size_t byte_count = elem_count * elem_size;

        void *ort_data = NULL;
        s = api->GetTensorMutableData(ov, &ort_data);
        if (s)
        {
            capture_ort_error(ctx, s);
            goto fail;
        }

        OutputBuffer *ob = &slot->out_bufs[i];
        if (!ensure_output_buffer(ob, byte_count))
        {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                     "out of memory for output buffer %d (%zu bytes)", i,
                     byte_count);
            goto fail;
        }
        memcpy(ob->data, ort_data, byte_count);
        ob->size = byte_count;

        outputs[i].data = ob->data;
        outputs[i].byte_count = byte_count;
        outputs[i].rank = (int)rank;
        outputs[i].dtype = dtype;
        for (size_t d = 0; d < rank; d++)
            outputs[i].shape[d] = shape[d];
    }

    for (int i = 0; i < n_inputs; i++)
        if (in_vals[i])
            api->ReleaseValue(in_vals[i]);
    for (int i = 0; i < n_outputs; i++)
        if (out_vals[i])
            api->ReleaseValue(out_vals[i]);
    api->ReleaseMemoryInfo(mem);
    free(in_vals);
    free(out_vals);
    free(in_names);
    free(out_names);
    return true;

fail:
    if (in_vals)
        for (int i = 0; i < n_inputs; i++)
            if (in_vals[i])
                api->ReleaseValue(in_vals[i]);
    if (out_vals)
        for (int i = 0; i < n_outputs; i++)
            if (out_vals[i])
                api->ReleaseValue(out_vals[i]);
    if (mem)
        api->ReleaseMemoryInfo(mem);
    free(in_vals);
    free(out_vals);
    free(in_names);
    free(out_names);
    return false;
}

const char *neural_get_error(NeuralContext *ctx)
{
    if (!ctx)
        return "";
    return ctx->last_error;
}

#endif