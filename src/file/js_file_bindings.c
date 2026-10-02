#include "js_file_bindings.h"
#include "file_service.h"
#include "core/subsystem_queue.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef BUDO_WEB
#include <emscripten.h>
#endif

#ifdef _WIN32
#define FILE_PICKER_POPEN _popen
#define FILE_PICKER_PCLOSE _pclose
#else
#define FILE_PICKER_POPEN popen
#define FILE_PICKER_PCLOSE pclose
#endif

#define FILE_PICKER_MAX_TEXT (8 * 1024 * 1024)
#define FILE_BRIDGE_SLOT_COUNT 16
#define FILE_BRIDGE_SLOT_BITS 8
#define FILE_BRIDGE_GENERATION_MAX 0x00ffffffu
#define FILE_COMPLETION_CAPACITY 8
#define FILE_COMPLETION_MAX_ERROR 4096

typedef enum JsFileCompletionType
{
    JS_FILE_COMPLETION_PICK,
    JS_FILE_COMPLETION_SAVE
} JsFileCompletionType;

typedef struct JsFileCompletion
{
    JsFileCompletionType type;
    char *name;
    char *text;
    char *error;
} JsFileCompletion;

struct JsFileContext
{
    FileContext *file_ctx;
    JSContext *js_ctx;
    JSValue function_data;
    JSValue picker_callback;
    JSValue save_callback;
    char picker_extension[32];
    JsFileBridgeToken bridge_token;
    bool shutting_down;
    SubsystemQueue completions;
    JsFileCompletion completion_storage[FILE_COMPLETION_CAPACITY];
};

typedef struct JsFileBridgeSlot
{
    JsFileContext *state;
    uint32_t generation;
} JsFileBridgeSlot;

static JsFileBridgeSlot g_file_bridge_slots[FILE_BRIDGE_SLOT_COUNT];
static BudoMutex g_file_bridge_mutex;
static bool g_file_bridge_mutex_ready;

#ifdef _WIN32
static INIT_ONCE g_file_bridge_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK file_bridge_initialize_once(PINIT_ONCE once, PVOID parameter,
                                                 PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    g_file_bridge_mutex_ready = budo_mutex_init(&g_file_bridge_mutex);
    return TRUE;
}
#else
static pthread_once_t g_file_bridge_once = PTHREAD_ONCE_INIT;

static void file_bridge_initialize_once(void)
{
    g_file_bridge_mutex_ready = budo_mutex_init(&g_file_bridge_mutex);
}
#endif

static bool file_bridge_initialize(void)
{
#ifdef _WIN32
    if (!InitOnceExecuteOnce(&g_file_bridge_once, file_bridge_initialize_once,
                             NULL, NULL))
        return false;
#else
    if (pthread_once(&g_file_bridge_once, file_bridge_initialize_once) != 0)
        return false;
#endif
    return g_file_bridge_mutex_ready;
}

static JsFileBridgeToken file_bridge_register(JsFileContext *state)
{
    JsFileBridgeToken token = 0;

    if (!state || !file_bridge_initialize())
        return 0;

    budo_mutex_lock(&g_file_bridge_mutex);
    for (uint32_t index = 0; index < FILE_BRIDGE_SLOT_COUNT; index++)
    {
        JsFileBridgeSlot *slot = &g_file_bridge_slots[index];
        if (slot->state)
            continue;
        if (slot->generation == 0)
            slot->generation = 1;
        slot->state = state;
        token = (slot->generation << FILE_BRIDGE_SLOT_BITS) | (index + 1);
        break;
    }
    budo_mutex_unlock(&g_file_bridge_mutex);
    return token;
}

static void file_bridge_invalidate(JsFileBridgeToken token,
                                   JsFileContext *state)
{
    uint32_t encoded_slot = token & 0xffu;

    if (!token || !state || encoded_slot == 0 ||
        encoded_slot > FILE_BRIDGE_SLOT_COUNT || !file_bridge_initialize())
        return;

    budo_mutex_lock(&g_file_bridge_mutex);
    JsFileBridgeSlot *slot = &g_file_bridge_slots[encoded_slot - 1];
    JsFileBridgeToken current =
        (slot->generation << FILE_BRIDGE_SLOT_BITS) | encoded_slot;
    if (slot->state == state && current == token)
    {
        slot->state = NULL;
        slot->generation = slot->generation >= FILE_BRIDGE_GENERATION_MAX
                               ? 1
                               : slot->generation + 1;
    }
    budo_mutex_unlock(&g_file_bridge_mutex);
}

static char *file_completion_copy(const char *value, size_t max_length)
{
    size_t length = 0;
    char *copy;

    if (!value)
        return NULL;
    while (length <= max_length && value[length])
        length++;
    if (length > max_length)
        return NULL;
    copy = (char *)malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static void file_completion_free(JsFileCompletion *completion)
{
    if (!completion)
        return;
    free(completion->name);
    free(completion->text);
    free(completion->error);
    memset(completion, 0, sizeof(*completion));
}

static bool file_bridge_submit(JsFileBridgeToken token,
                               JsFileCompletion *completion)
{
    uint32_t encoded_slot = token & 0xffu;
    bool queued = false;

    if (!completion || !token || encoded_slot == 0 ||
        encoded_slot > FILE_BRIDGE_SLOT_COUNT || !file_bridge_initialize())
        return false;

    budo_mutex_lock(&g_file_bridge_mutex);
    JsFileBridgeSlot *slot = &g_file_bridge_slots[encoded_slot - 1];
    JsFileBridgeToken current =
        (slot->generation << FILE_BRIDGE_SLOT_BITS) | encoded_slot;
    if (slot->state && current == token && !slot->state->shutting_down)
        queued = subsystem_queue_try_push(&slot->state->completions, completion);
    budo_mutex_unlock(&g_file_bridge_mutex);
    return queued;
}

static bool file_name_has_extension(const char *name, const char *extension)
{
    if (!extension || !extension[0])
        return true;
    if (!name)
        return false;
    size_t name_length = strlen(name);
    size_t extension_length = strlen(extension);
    if (extension_length > name_length)
        return false;
    const char *suffix = name + name_length - extension_length;
    for (size_t i = 0; i < extension_length; i++)
    {
        char left = suffix[i];
        char right = extension[i];
        if (left >= 'A' && left <= 'Z')
            left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z')
            right = (char)(right - 'A' + 'a');
        if (left != right)
            return false;
    }
    return true;
}

static void file_picker_dispatch(JsFileContext *state, const char *name,
                                 const char *text, const char *error)
{
    if (!state || state->shutting_down || !state->js_ctx ||
        JS_IsUndefined(state->picker_callback))
        return;

    if (text && !file_name_has_extension(name, state->picker_extension))
    {
        text = NULL;
        error = "Selected file has the wrong extension";
    }

    JSContext *ctx = state->js_ctx;
    JSValue args[2];
    if (text)
    {
        args[0] = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, args[0], "name",
                          JS_NewString(ctx, name ? name : "selected.txt"));
        JS_SetPropertyStr(ctx, args[0], "text", JS_NewString(ctx, text));
        args[1] = JS_NULL;
    }
    else
    {
        args[0] = JS_NULL;
        args[1] = JS_NewString(ctx, error ? error : "File selection cancelled");
    }

    JSValue callback = state->picker_callback;
    state->picker_callback = JS_UNDEFINED;
    JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 2, args);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, callback);
    state->picker_extension[0] = '\0';
}

static void file_save_dispatch(JsFileContext *state, const char *error)
{
    if (!state || state->shutting_down || !state->js_ctx ||
        JS_IsUndefined(state->save_callback))
        return;
    JSContext *ctx = state->js_ctx;
    JSValue argument = error ? JS_NewString(ctx, error) : JS_NULL;
    JSValue callback = state->save_callback;
    state->save_callback = JS_UNDEFINED;
    JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 1, &argument);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, argument);
    JS_FreeValue(ctx, callback);
}

bool js_file_picker_complete(JsFileBridgeToken token, const char *name,
                             const char *text, const char *error)
{
    JsFileCompletion completion = {0};
    completion.type = JS_FILE_COMPLETION_PICK;
    completion.name = file_completion_copy(name, FILE_MAX_PATH - 1);
    completion.text = file_completion_copy(text, FILE_PICKER_MAX_TEXT);
    completion.error = file_completion_copy(error, FILE_COMPLETION_MAX_ERROR);
    if ((name && !completion.name) || (text && !completion.text) ||
        (error && !completion.error) || !file_bridge_submit(token, &completion))
    {
        file_completion_free(&completion);
        return false;
    }
    return true;
}

bool js_file_save_complete(JsFileBridgeToken token, const char *error)
{
    JsFileCompletion completion = {0};
    completion.type = JS_FILE_COMPLETION_SAVE;
    completion.error = file_completion_copy(error, FILE_COMPLETION_MAX_ERROR);
    if ((error && !completion.error) || !file_bridge_submit(token, &completion))
    {
        file_completion_free(&completion);
        return false;
    }
    return true;
}

#ifdef BUDO_WEB
EM_JS(void, web_pick_text_file, (JsFileBridgeToken token, const char *extension), {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = extension ? UTF8ToString(extension) : 'text/plain';
    input.style.display = 'none';
    input.addEventListener('cancel', function() {
        const error = stringToNewUTF8('File selection cancelled');
        _js_file_picker_complete(token, 0, 0, error);
        _free(error);
        input.remove(); });
    input.addEventListener('change', function() {
        const file = input.files && input.files[0];
        if (!file) {
            const error = stringToNewUTF8('File selection cancelled');
            _js_file_picker_complete(token, 0, 0, error);
            _free(error);
            input.remove();
            return;
        }
        if (file.size > 8 * 1024 * 1024) {
            const error = stringToNewUTF8('Selected file is larger than 8 MiB');
            _js_file_picker_complete(token, 0, 0, error);
            _free(error);
            input.remove();
            return;
        }
        file.text().then(function(text) {
            const name = stringToNewUTF8(file.name);
            const content = stringToNewUTF8(text);
            _js_file_picker_complete(token, name, content, 0);
            _free(name);
            _free(content);
            input.remove();
        }).catch(function(reason) {
            const error = stringToNewUTF8(String(reason));
            _js_file_picker_complete(token, 0, 0, error);
            _free(error);
            input.remove();
        }); });
    document.body.appendChild(input);
    input.click();
});

EM_JS(void, web_save_text_file, (JsFileBridgeToken token, const char *name, const char *text), {
    try {
        const blob = new Blob([UTF8ToString(text)], {
type:
    'text/plain;charset=utf-8' });
        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = UTF8ToString(name);
        document.body.appendChild(link);
        link.click();
        link.remove();
        URL.revokeObjectURL(link.href);
        _js_file_save_complete(token, 0);
}
catch(reason)
{
    const error = stringToNewUTF8(String(reason));
    _js_file_save_complete(token, error);
    _free(error);
}
});
#endif

#if !defined(BUDO_WEB) && !defined(BUDO_ANDROID)
static bool desktop_pick_text_file(JsFileContext *state,
                                   const char *extension)
{
    const char *command;
#ifdef __APPLE__
    command = extension && strcmp(extension, ".json") == 0
                  ? "osascript -e 'POSIX path of (choose file with prompt \"Choose a JSON file\" of type {\"public.json\"})' 2>/dev/null"
                  : "osascript -e 'POSIX path of (choose file with prompt \"Choose a text file\")' 2>/dev/null";
#elif defined(_WIN32)
    command = extension && strcmp(extension, ".json") == 0
                  ? "powershell.exe -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.OpenFileDialog; $d.Filter='JSON files|*.json'; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}\""
                  : "powershell.exe -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.OpenFileDialog; $d.Filter='Text files|*.txt|All files|*.*'; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}\"";
#else
    command = extension && strcmp(extension, ".json") == 0
                  ? "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --title='Choose a JSON file' --file-filter='JSON files | *.json'; elif command -v kdialog >/dev/null 2>&1; then kdialog --getopenfilename . '*.json'; fi"
                  : "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --title='Choose a text file'; elif command -v kdialog >/dev/null 2>&1; then kdialog --getopenfilename . '*.txt'; fi";
#endif
    FILE *pipe = FILE_PICKER_POPEN(command, "r");
    if (!pipe)
        return false;
    char path[FILE_MAX_PATH];
    size_t length = fread(path, 1, sizeof(path) - 1, pipe);
    FILE_PICKER_PCLOSE(pipe);
    path[length] = '\0';
    while (length > 0 && (path[length - 1] == '\n' || path[length - 1] == '\r'))
        path[--length] = '\0';
    if (length == 0)
    {
        file_picker_dispatch(state, NULL, NULL, "File selection cancelled");
        return true;
    }

    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0)
    {
        if (file)
            fclose(file);
        file_picker_dispatch(state, NULL, NULL, "Could not open selected file");
        return true;
    }
    long size = ftell(file);
    if (size < 0 || size > FILE_PICKER_MAX_TEXT || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        file_picker_dispatch(state, NULL, NULL,
                             "Selected file is empty or larger than 8 MiB");
        return true;
    }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text)
    {
        fclose(file);
        file_picker_dispatch(state, NULL, NULL,
                             "Out of memory reading selected file");
        return true;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    const char *name = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (!name || (backslash && backslash > name))
        name = backslash;
#endif
    file_picker_dispatch(state, name ? name + 1 : path, text, NULL);
    free(text);
    return true;
}

static bool desktop_save_text_file(JsFileContext *state, const char *name,
                                   const char *text)
{
    char command[2048];
#ifdef __APPLE__
    snprintf(command, sizeof(command),
             "osascript -e 'POSIX path of (choose file name with prompt \"Save text file\" default name \"%s\")' 2>/dev/null",
             name);
#elif defined(_WIN32)
    snprintf(command, sizeof(command),
             "powershell.exe -NoProfile -Command \"Add-Type -AssemblyName System.Windows.Forms; $d=New-Object System.Windows.Forms.SaveFileDialog; $d.FileName='%s'; $d.Filter='JSON files|*.json|Text files|*.txt'; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}\"",
             name);
#else
    snprintf(command, sizeof(command),
             "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --save --confirm-overwrite --filename='%s'; elif command -v kdialog >/dev/null 2>&1; then kdialog --getsavefilename . '%s'; fi",
             name, name);
#endif
    FILE *pipe = FILE_PICKER_POPEN(command, "r");
    if (!pipe)
        return false;
    char path[FILE_MAX_PATH];
    size_t length = fread(path, 1, sizeof(path) - 1, pipe);
    FILE_PICKER_PCLOSE(pipe);
    path[length] = '\0';
    while (length > 0 && (path[length - 1] == '\n' || path[length - 1] == '\r'))
        path[--length] = '\0';
    if (length == 0)
    {
        file_save_dispatch(state, "File save cancelled");
        return true;
    }
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        file_save_dispatch(state, "Could not create selected file");
        return true;
    }
    size_t text_length = strlen(text);
    bool ok = fwrite(text, 1, text_length, file) == text_length;
    if (fclose(file) != 0)
        ok = false;
    file_save_dispatch(state, ok ? NULL : "Could not write selected file");
    return true;
}
#endif

#ifdef BUDO_ANDROID
extern bool android_file_pick_text(JsFileBridgeToken token,
                                   const char *extension);
extern bool android_file_save_text(JsFileBridgeToken token, const char *name,
                                   const char *text);
#elif defined(BUDO_FILE_BRIDGE_TEST)
extern bool budo_test_file_pick_text(JsFileBridgeToken token,
                                     const char *extension);
extern bool budo_test_file_save_text(JsFileBridgeToken token, const char *name,
                                     const char *text);
#endif

static JsFileContext *js_file_binding_state(
    JSContext *ctx, JSValueConst *func_data)
{
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, func_data[0]);

    return data && size == sizeof(JsFileContext)
               ? (JsFileContext *)data
               : NULL;
}

#define JS_FILE_CALLBACK(name)                                           \
    static JSValue name(JSContext *ctx, JSValueConst this_val, int argc, \
                        JSValueConst *argv, int magic,                   \
                        JSValueConst *func_data)

#define JS_FILE_STATE()                                           \
    JsFileContext *state = js_file_binding_state(ctx, func_data); \
    FileContext *file_ctx = state ? state->file_ctx : NULL;       \
    (void)magic

static bool asset_path(const char *path, char output[FILE_MAX_PATH])
{
    int length = path[0]
                     ? snprintf(output, FILE_MAX_PATH, "assets/%s", path)
                     : snprintf(output, FILE_MAX_PATH, "assets");
    return length >= 0 && length < FILE_MAX_PATH;
}

static JSValue file_list_result(JSContext *ctx, FileListResult *result)
{
    if (!result)
        return JS_NULL;

    JSValue arr = JS_NewArray(ctx);
    for (int i = 0; i < result->count; i++)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "name",
                          JS_NewString(ctx, result->entries[i].name));
        JS_SetPropertyStr(ctx, obj, "type",
                          JS_NewString(ctx, result->entries[i].type == FILE_ENTRY_DIRECTORY ? "directory" : "file"));
        if (result->entries[i].type == FILE_ENTRY_FILE && result->entries[i].size >= 0)
            JS_SetPropertyStr(ctx, obj, "size", JS_NewInt64(ctx, result->entries[i].size));
        JS_SetPropertyUint32(ctx, arr, i, obj);
    }
    file_list_free(result);
    return arr;
}

static JSValue check_path(JSContext *ctx, const char *method, int argc,
                          JSValueConst *argv, int expected_argc, bool allow_empty,
                          const char **out_path)
{
    if (argc != expected_argc)
        return JS_ThrowTypeError(ctx, "file.%s expects %d argument%s", method,
                                 expected_argc, expected_argc == 1 ? "" : "s");
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "file.%s path must be a string", method);
    *out_path = JS_ToCString(ctx, argv[0]);
    if (!*out_path)
        return JS_EXCEPTION;
    bool writable = strcmp(method, "writeText") == 0 ||
                    strcmp(method, "writeBinary") == 0 ||
                    strcmp(method, "appendBinary") == 0;
    if (!file_virtual_path_is_valid(*out_path, allow_empty, writable))
    {
        JS_FreeCString(ctx, *out_path);
        *out_path = NULL;
        return JS_ThrowRangeError(ctx, "files.%s requires an assets/ or files/ path%s",
                                  method, writable ? " under files/" : "");
    }
    return JS_UNDEFINED;
}

JS_FILE_CALLBACK(js_file_list)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path = "";
    if (argc > 1)
        return JS_ThrowTypeError(ctx, "file.list expects zero or one argument");
    if (argc == 1)
    {
        if (!JS_IsString(argv[0]))
            return JS_ThrowTypeError(ctx, "file.list path must be a string");
        rel_path = JS_ToCString(ctx, argv[0]);
        if (!rel_path)
            return JS_EXCEPTION;
        if (!file_virtual_path_is_valid(rel_path, true, false))
        {
            JS_FreeCString(ctx, rel_path);
            return JS_ThrowRangeError(ctx, "files.list requires assets/, files/, or no path");
        }
    }

    FileListResult *result = file_list(file_ctx, rel_path);

    if (argc == 1)
        JS_FreeCString(ctx, rel_path);

    return file_list_result(ctx, result);
}

JS_FILE_CALLBACK(js_asset_list)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *path = "";
    if (argc > 1)
        return JS_ThrowTypeError(ctx, "assets.list expects zero or one argument");
    if (argc == 1)
    {
        if (!JS_IsString(argv[0]))
            return JS_ThrowTypeError(ctx, "assets.list path must be a string");
        path = JS_ToCString(ctx, argv[0]);
        if (!path)
            return JS_EXCEPTION;
        if (!file_path_is_valid(path, true))
        {
            JS_FreeCString(ctx, path);
            return JS_ThrowRangeError(ctx, "assets.list requires a safe relative path");
        }
    }
    char virtual_path[FILE_MAX_PATH];
    bool valid = asset_path(path, virtual_path);
    if (argc == 1)
        JS_FreeCString(ctx, path);
    if (!valid)
        return JS_ThrowRangeError(ctx, "assets.list path is too long");
    return file_list_result(ctx, file_list(file_ctx, virtual_path));
}

static JSValue check_asset_path(JSContext *ctx, const char *method, int argc,
                                JSValueConst *argv, char output[FILE_MAX_PATH])
{
    if (argc != 1)
        return JS_ThrowTypeError(ctx, "assets.%s expects one argument", method);
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "assets.%s path must be a string", method);
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    bool valid = file_path_is_valid(path, false) && asset_path(path, output);
    JS_FreeCString(ctx, path);
    if (!valid)
        return JS_ThrowRangeError(ctx, "assets.%s requires a safe relative path", method);
    return JS_UNDEFINED;
}

JS_FILE_CALLBACK(js_asset_read_text)
{
    (void)this_val;
    JS_FILE_STATE();
    char path[FILE_MAX_PATH];
    JSValue error = check_asset_path(ctx, "readText", argc, argv, path);
    if (JS_IsException(error))
        return error;
    size_t length = 0;
    ApiError api_error;
    char *content = file_service_read_text_virtual(file_ctx, path, &length,
                                                   &api_error);
    if (!content)
        return JS_NULL;
    JSValue result = JS_NewStringLen(ctx, content, length);
    free(content);
    return result;
}

JS_FILE_CALLBACK(js_asset_read_binary)
{
    (void)this_val;
    JS_FILE_STATE();
    char path[FILE_MAX_PATH];
    JSValue error = check_asset_path(ctx, "readBinary", argc, argv, path);
    if (JS_IsException(error))
        return error;
    size_t length = 0;
    uint8_t *data = file_read_binary(file_ctx, path, &length);
    if (!data)
        return JS_NULL;
    JSValue result = JS_NewArrayBufferCopy(ctx, data, length);
    free(data);
    return result;
}

JS_FILE_CALLBACK(js_asset_exists)
{
    (void)this_val;
    JS_FILE_STATE();
    char path[FILE_MAX_PATH];
    JSValue error = check_asset_path(ctx, "exists", argc, argv, path);
    if (JS_IsException(error))
        return error;
    return JS_NewBool(ctx, file_is_file(file_ctx, path));
}

JS_FILE_CALLBACK(js_asset_is_directory)
{
    (void)this_val;
    JS_FILE_STATE();
    char path[FILE_MAX_PATH];
    JSValue error = check_asset_path(ctx, "isDirectory", argc, argv, path);
    if (JS_IsException(error))
        return error;
    return JS_NewBool(ctx, file_is_directory(file_ctx, path));
}

JS_FILE_CALLBACK(js_asset_size)
{
    (void)this_val;
    JS_FILE_STATE();
    char path[FILE_MAX_PATH];
    JSValue error = check_asset_path(ctx, "size", argc, argv, path);
    if (JS_IsException(error))
        return error;
    int64_t size = file_size(file_ctx, path);
    return size < 0 ? JS_NULL : JS_NewInt64(ctx, size);
}

JS_FILE_CALLBACK(js_file_read_text)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path;
    JSValue err = check_path(ctx, "readText", argc, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    size_t len = 0;
    ApiError api_error;
    char *content = file_service_read_text_virtual(file_ctx, rel_path, &len,
                                                   &api_error);
    JS_FreeCString(ctx, rel_path);

    if (!content)
        return JS_NULL;

    JSValue result = JS_NewStringLen(ctx, content, len);
    free(content);
    return result;
}

JS_FILE_CALLBACK(js_file_read_binary)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path;
    JSValue err = check_path(ctx, "readBinary", argc, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    size_t len = 0;
    uint8_t *data = file_read_binary(file_ctx, rel_path, &len);
    JS_FreeCString(ctx, rel_path);

    if (!data)
        return JS_NULL;

    JSValue ab = JS_NewArrayBufferCopy(ctx, data, len);
    free(data);
    return ab;
}

JS_FILE_CALLBACK(js_file_exists)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path;
    JSValue err = check_path(ctx, "exists", argc, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    bool exists = file_is_file(file_ctx, rel_path);
    JS_FreeCString(ctx, rel_path);

    return JS_NewBool(ctx, exists);
}

JS_FILE_CALLBACK(js_file_is_directory)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path;
    JSValue err = check_path(ctx, "isDirectory", argc, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    bool is_dir = file_is_directory(file_ctx, rel_path);
    JS_FreeCString(ctx, rel_path);

    return JS_NewBool(ctx, is_dir);
}

JS_FILE_CALLBACK(js_file_size)
{
    (void)this_val;
    JS_FILE_STATE();
    const char *rel_path;
    JSValue err = check_path(ctx, "size", argc, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    int64_t sz = file_size(file_ctx, rel_path);
    JS_FreeCString(ctx, rel_path);

    if (sz < 0)
        return JS_NULL;

    return JS_NewInt64(ctx, sz);
}

JS_FILE_CALLBACK(js_file_write_text)
{
    (void)this_val;
    JS_FILE_STATE();
    if (argc != 2)
        return JS_ThrowTypeError(ctx, "file.writeText requires (path, text) arguments");
    if (!JS_IsString(argv[0]) || !JS_IsString(argv[1]))
        return JS_ThrowTypeError(ctx, "file.writeText path and text must be strings");
    const char *rel_path;
    JSValue err = check_path(ctx, "writeText", 1, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    size_t len = 0;
    const char *text = JS_ToCStringLen(ctx, &len, argv[1]);
    if (!text)
    {
        JS_FreeCString(ctx, rel_path);
        return JS_EXCEPTION;
    }

    bool ok = file_write_text(file_ctx, rel_path, text, len);
    JS_FreeCString(ctx, rel_path);
    JS_FreeCString(ctx, text);

    if (!ok)
        return JS_NULL;

    return JS_NewString(ctx, file_get_last_write_path(file_ctx));
}

JS_FILE_CALLBACK(js_file_write_binary)
{
    (void)this_val;
    JS_FILE_STATE();
    if (argc != 2)
        return JS_ThrowTypeError(ctx, "file.writeBinary requires (path, data) arguments");
    const char *rel_path;
    JSValue err = check_path(ctx, "writeBinary", 1, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    size_t len = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &len, argv[1]);

    if (!data)
    {
        size_t byte_offset = 0;
        size_t byte_length = 0;
        size_t bytes_per_element = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[1], &byte_offset,
                                                &byte_length, &bytes_per_element);
        if (!JS_IsException(buffer))
        {
            size_t buf_len = 0;
            uint8_t *buf = JS_GetArrayBuffer(ctx, &buf_len, buffer);
            if (buf)
            {
                data = buf + byte_offset;
                len = byte_length;
            }
            JS_FreeValue(ctx, buffer);
        }
    }

    if (!data)
    {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "file.writeBinary requires an ArrayBuffer or TypedArray");
    }

    bool ok = file_write_binary(file_ctx, rel_path, data, len);
    JS_FreeCString(ctx, rel_path);

    if (!ok)
        return JS_NULL;

    return JS_NewString(ctx, file_get_last_write_path(file_ctx));
}

JS_FILE_CALLBACK(js_file_append_binary)
{
    (void)this_val;
    JS_FILE_STATE();
    if (argc != 2)
        return JS_ThrowTypeError(ctx, "file.appendBinary requires (path, data) arguments");
    const char *rel_path;
    JSValue err = check_path(ctx, "appendBinary", 1, argv, 1, false, &rel_path);
    if (JS_IsException(err))
        return err;

    size_t len = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (!data)
    {
        size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, argv[1], &byte_offset,
                                                &byte_length, &bytes_per_element);
        if (!JS_IsException(buffer))
        {
            size_t buffer_length = 0;
            uint8_t *bytes = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
            if (bytes)
            {
                data = bytes + byte_offset;
                len = byte_length;
            }
            JS_FreeValue(ctx, buffer);
        }
    }
    if (!data)
    {
        JS_FreeCString(ctx, rel_path);
        return JS_ThrowTypeError(ctx, "file.appendBinary requires an ArrayBuffer or TypedArray");
    }
    bool ok = file_append_binary(file_ctx, rel_path, data, len);
    JS_FreeCString(ctx, rel_path);
    return ok ? JS_NewString(ctx, file_get_last_write_path(file_ctx)) : JS_NULL;
}

JS_FILE_CALLBACK(js_file_get_error)
{
    (void)this_val;
    (void)argv;
    JS_FILE_STATE();
    if (argc != 0)
        return JS_ThrowTypeError(ctx, "file.getError expects no arguments");
    if (!file_ctx)
        return JS_NewString(ctx, "");
    return JS_NewString(ctx, file_get_error(file_ctx));
}

JS_FILE_CALLBACK(js_file_pick_text)
{
    (void)this_val;
    JS_FILE_STATE();
    (void)file_ctx;
    if (argc < 1 || argc > 2 || !JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "files.pickText expects a callback and optional extension");
    if (!state || state->shutting_down || !state->bridge_token ||
        !JS_IsUndefined(state->picker_callback))
        return JS_FALSE;

    state->picker_extension[0] = '\0';
    if (argc == 2)
    {
        const char *extension = JS_ToCString(ctx, argv[1]);
        if (!extension)
            return JS_EXCEPTION;
        bool valid = extension[0] == '.' &&
                     strlen(extension) < sizeof(state->picker_extension);
        if (valid)
            snprintf(state->picker_extension, sizeof(state->picker_extension),
                     "%s", extension);
        JS_FreeCString(ctx, extension);
        if (!valid)
            return JS_ThrowRangeError(ctx, "files.pickText extension must start with '.' and be shorter than 32 bytes");
    }

    state->picker_callback = JS_DupValue(ctx, argv[0]);
    bool launched = false;
#ifdef BUDO_WEB
    web_pick_text_file(state->bridge_token, state->picker_extension);
    launched = true;
#elif defined(BUDO_ANDROID)
    launched = android_file_pick_text(state->bridge_token,
                                      state->picker_extension);
#elif defined(BUDO_FILE_BRIDGE_TEST)
    launched = budo_test_file_pick_text(state->bridge_token,
                                        state->picker_extension);
#else
    launched = desktop_pick_text_file(state, state->picker_extension);
#endif
    if (!launched && !JS_IsUndefined(state->picker_callback))
    {
        JS_FreeValue(ctx, state->picker_callback);
        state->picker_callback = JS_UNDEFINED;
    }
    return JS_NewBool(ctx, launched);
}

JS_FILE_CALLBACK(js_file_save_text)
{
    (void)this_val;
    JS_FILE_STATE();
    (void)file_ctx;
    if (argc != 3 || !JS_IsString(argv[0]) || !JS_IsString(argv[1]) ||
        !JS_IsFunction(ctx, argv[2]))
        return JS_ThrowTypeError(ctx, "files.saveText expects name, text, and callback");
    if (!state || state->shutting_down || !state->bridge_token ||
        !JS_IsUndefined(state->save_callback))
        return JS_FALSE;
    const char *name = JS_ToCString(ctx, argv[0]);
    const char *text = JS_ToCString(ctx, argv[1]);
    if (!name || !text)
    {
        if (name)
            JS_FreeCString(ctx, name);
        if (text)
            JS_FreeCString(ctx, text);
        return JS_EXCEPTION;
    }
    bool safe_name = name[0] && strlen(name) < 256;
    for (size_t i = 0; safe_name && name[i]; i++)
    {
        char ch = name[i];
        safe_name = (ch >= 'a' && ch <= 'z') ||
                    (ch >= 'A' && ch <= 'Z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '.' || ch == '_' || ch == '-' || ch == ' ';
    }
    if (!safe_name)
    {
        JS_FreeCString(ctx, name);
        JS_FreeCString(ctx, text);
        return JS_ThrowRangeError(ctx, "files.saveText name must be a simple file name");
    }
    state->save_callback = JS_DupValue(ctx, argv[2]);
    bool launched = false;
#ifdef BUDO_WEB
    web_save_text_file(state->bridge_token, name, text);
    launched = true;
#elif defined(BUDO_ANDROID)
    launched = android_file_save_text(state->bridge_token, name, text);
#elif defined(BUDO_FILE_BRIDGE_TEST)
    launched = budo_test_file_save_text(state->bridge_token, name, text);
#else
    launched = desktop_save_text_file(state, name, text);
#endif
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, text);
    if (!launched && !JS_IsUndefined(state->save_callback))
    {
        JS_FreeValue(ctx, state->save_callback);
        state->save_callback = JS_UNDEFINED;
    }
    return JS_NewBool(ctx, launched);
}

typedef struct JsFileFunction
{
    const char *name;
    uint8_t length;
    JSCFunctionData *callback;
} JsFileFunction;

static const JsFileFunction js_file_funcs[] = {
    {"list", 0, js_file_list},
    {"readText", 1, js_file_read_text},
    {"readBinary", 1, js_file_read_binary},
    {"writeText", 2, js_file_write_text},
    {"writeBinary", 2, js_file_write_binary},
    {"appendBinary", 2, js_file_append_binary},
    {"exists", 1, js_file_exists},
    {"isDirectory", 1, js_file_is_directory},
    {"size", 1, js_file_size},
    {"pickText", 1, js_file_pick_text},
    {"saveText", 3, js_file_save_text},
    {"getError", 0, js_file_get_error},
};

static const JsFileFunction js_asset_funcs[] = {
    {"list", 0, js_asset_list},
    {"readText", 1, js_asset_read_text},
    {"readBinary", 1, js_asset_read_binary},
    {"exists", 1, js_asset_exists},
    {"isDirectory", 1, js_asset_is_directory},
    {"size", 1, js_asset_size},
    {"getError", 0, js_file_get_error},
};

static int js_file_add_functions(JSContext *ctx, JSValue object,
                                 const JsFileFunction *functions,
                                 size_t count, JSValue function_data)
{
    for (size_t index = 0; index < count; index++)
    {
        JSValue function = JS_NewCFunctionData(
            ctx, functions[index].callback, functions[index].length,
            0, 1, &function_data);
        if (JS_IsException(function))
            return -1;
        if (JS_SetPropertyStr(ctx, object, functions[index].name, function) < 0)
            return -1;
    }
    return 0;
}

static void js_file_ctx_free(JSRuntime *runtime, void *opaque, void *pointer)
{
    JsFileContext *state = (JsFileContext *)pointer;
    (void)runtime;
    (void)opaque;

    if (!state)
        return;
    file_bridge_invalidate(state->bridge_token, state);
    subsystem_queue_close(&state->completions);
    JsFileCompletion completion;
    while (subsystem_queue_try_pop(&state->completions, &completion))
        file_completion_free(&completion);
    subsystem_queue_destroy(&state->completions);
    file_destroy(state->file_ctx);
    free(state);
}

JsFileContext *js_file_init(JSContext *ctx, const char *root_dir)
{
    JsFileContext *state =
        (JsFileContext *)calloc(1, sizeof(JsFileContext));
    if (!state)
        return NULL;
    state->picker_callback = JS_UNDEFINED;
    state->save_callback = JS_UNDEFINED;
    state->function_data = JS_UNDEFINED;
    state->js_ctx = ctx;
    state->file_ctx = file_create(root_dir);
    if (!state->file_ctx)
    {
        fprintf(stderr, "Failed to create file context\n");
        free(state);
        return NULL;
    }
    if (!subsystem_queue_init(&state->completions, state->completion_storage,
                              sizeof(state->completion_storage[0]),
                              FILE_COMPLETION_CAPACITY))
    {
        file_destroy(state->file_ctx);
        free(state);
        return NULL;
    }
    state->bridge_token = file_bridge_register(state);
    if (!state->bridge_token)
    {
        subsystem_queue_destroy(&state->completions);
        file_destroy(state->file_ctx);
        free(state);
        return NULL;
    }
    state->function_data = JS_NewArrayBuffer(
        ctx, (uint8_t *)state, sizeof(*state), js_file_ctx_free,
        NULL, false);
    if (JS_IsException(state->function_data))
    {
        file_bridge_invalidate(state->bridge_token, state);
        subsystem_queue_destroy(&state->completions);
        file_destroy(state->file_ctx);
        free(state);
        return NULL;
    }

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue sys_obj = JS_GetPropertyStr(ctx, global, "sys");

    JSValue file_obj = JS_NewObject(ctx);
    JSValue asset_obj = JS_NewObject(ctx);
    bool registration_failed =
        JS_IsException(global) || JS_IsException(sys_obj) ||
        JS_IsException(file_obj) || JS_IsException(asset_obj);
    if (!registration_failed)
        registration_failed =
            js_file_add_functions(
                ctx, file_obj, js_file_funcs,
                sizeof(js_file_funcs) / sizeof(js_file_funcs[0]),
                state->function_data) < 0 ||
            js_file_add_functions(
                ctx, asset_obj, js_asset_funcs,
                sizeof(js_asset_funcs) / sizeof(js_asset_funcs[0]),
                state->function_data) < 0;
    if (!registration_failed)
    {
        registration_failed =
            JS_SetPropertyStr(ctx, sys_obj, "files", file_obj) < 0;
        file_obj = JS_UNDEFINED;
    }
    if (!registration_failed)
    {
        registration_failed =
            JS_SetPropertyStr(ctx, sys_obj, "assets", asset_obj) < 0;
        asset_obj = JS_UNDEFINED;
    }
    if (registration_failed)
    {
        JS_FreeValue(ctx, asset_obj);
        JS_FreeValue(ctx, file_obj);
        JS_FreeValue(ctx, sys_obj);
        JS_FreeValue(ctx, global);
        js_file_cleanup(state);
        return NULL;
    }
    JS_FreeValue(ctx, sys_obj);
    JS_FreeValue(ctx, global);

    return state;
}

FileContext *js_file_context(JsFileContext *state)
{
    return state ? state->file_ctx : NULL;
}

JsFileBridgeToken js_file_bridge_token(const JsFileContext *state)
{
    return state ? state->bridge_token : 0;
}

bool js_file_has_pending_work(JsFileContext *state)
{
    return state && !JS_IsUndefined(state->picker_callback);
}

void js_file_poll(JsFileContext *state)
{
    JsFileCompletion completion;

    if (!state || state->shutting_down)
        return;
    while (subsystem_queue_try_pop(&state->completions, &completion))
    {
        if (completion.type == JS_FILE_COMPLETION_PICK)
            file_picker_dispatch(state, completion.name, completion.text,
                                 completion.error);
        else
            file_save_dispatch(state, completion.error);
        file_completion_free(&completion);
    }
}

void js_file_cleanup(JsFileContext *state)
{
    JSContext *ctx;
    JSValue function_data;
    JsFileCompletion completion;

    if (!state || state->shutting_down)
        return;

    file_bridge_invalidate(state->bridge_token, state);
    state->bridge_token = 0;
    state->shutting_down = true;
    subsystem_queue_close(&state->completions);
    while (subsystem_queue_try_pop(&state->completions, &completion))
        file_completion_free(&completion);

    ctx = state->js_ctx;
    function_data = state->function_data;
    if (ctx && !JS_IsUndefined(state->picker_callback))
        JS_FreeValue(ctx, state->picker_callback);
    if (ctx && !JS_IsUndefined(state->save_callback))
        JS_FreeValue(ctx, state->save_callback);
    state->picker_callback = JS_UNDEFINED;
    state->save_callback = JS_UNDEFINED;
    state->function_data = JS_UNDEFINED;
    state->js_ctx = NULL;
    file_destroy(state->file_ctx);
    state->file_ctx = NULL;

    if (ctx && !JS_IsUndefined(function_data))
        JS_FreeValue(ctx, function_data);
}

#undef JS_FILE_STATE
#undef JS_FILE_CALLBACK