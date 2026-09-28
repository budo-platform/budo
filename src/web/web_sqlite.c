#include "web/web_sqlite.h"

#include <emscripten.h>
#include <stdio.h>

static void (*g_ready_callback)(void) = NULL;

EMSCRIPTEN_KEEPALIVE
void web_sqlite_on_ready(void)
{
    printf("[budo-web] IDBFS synced — SQLite filesystem ready.\n");
    if (g_ready_callback)
    {
        void (*cb)(void) = g_ready_callback;
        g_ready_callback = NULL;
        cb();
    }
}

void web_sqlite_init_fs(void (*callback)(void))
{
    g_ready_callback = callback;

    EM_ASM({
        try { FS.mkdir('/db'); }
        catch(e) {  }
        try { FS.mkdir('/files'); }
        catch(e) {  }
        FS.mount(IDBFS, {}, '/db');
        FS.mount(IDBFS, {}, '/files');
        FS.syncfs(true, function(err) {
            if (err) console.warn('[budo-web] IDBFS initial sync error:', err);
            _web_sqlite_on_ready(); });
    });
}

void web_sqlite_sync(void)
{
    EM_ASM({
        FS.syncfs(false, function(err) {
            if (err) console.warn('[budo-web] IDBFS write-back error:', err); });
    });
}