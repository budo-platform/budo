#ifndef JS_FILE_BINDINGS_H
#define JS_FILE_BINDINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "quickjs.h"
#include "file_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct JsFileContext JsFileContext;
    typedef uint32_t JsFileBridgeToken;

    JsFileContext *js_file_init(JSContext *ctx, const char *root_dir);

    FileContext *js_file_context(JsFileContext *state);

    void js_file_cleanup(JsFileContext *state);

    void js_file_poll(JsFileContext *state);
    
    bool js_file_has_pending_work(JsFileContext *state);

    JsFileBridgeToken js_file_bridge_token(const JsFileContext *state);

    bool js_file_picker_complete(JsFileBridgeToken token, const char *name,
                                 const char *text, const char *error);

    bool js_file_save_complete(JsFileBridgeToken token, const char *error);

#ifdef __cplusplus
}
#endif

#endif