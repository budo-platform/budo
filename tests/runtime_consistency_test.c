#include "core/app_entrypoint.h"
#include "core/app_metadata.h"
#include "network/udp_wrapper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void touch_file(const char *root, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fclose(file);
}

int main(void)
{
    char temporary[] = "/tmp/budo-runtime-consistency-XXXXXX";
    char *root = mkdtemp(temporary);
    assert(root);

    AppEntrypoint entrypoint;
    char error[256];
    touch_file(root, "main.js");
    touch_file(root, "main.ts");
    assert(app_entrypoint_resolve(root, APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA,
                                  &entrypoint, error, sizeof(error)));
    assert(strcmp(entrypoint.filename, "main.ts") == 0);
    touch_file(root, "main.lua");
    assert(!app_entrypoint_resolve(root, APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA,
                                   &entrypoint, error, sizeof(error)));
    assert(strstr(error, "Ambiguous"));

    char wasm_root[1024];
    snprintf(wasm_root, sizeof(wasm_root), "%s/wasm", root);
    assert(mkdir(wasm_root, 0700) == 0);
    touch_file(wasm_root, "main.wat");
    touch_file(wasm_root, "main.wasm");
    assert(app_entrypoint_resolve(wasm_root, APP_ENTRYPOINT_WEBASSEMBLY,
                                  &entrypoint, error, sizeof(error)));
    assert(strcmp(entrypoint.filename, "main.wasm") == 0);

    char version[64] = "1.0";
    assert(app_metadata_parse_version("{\"version\":\"2.0\",\"version_name\":\"2.0\"}",
                                      version, sizeof(version), error, sizeof(error)));
    assert(strcmp(version, "2.0") == 0);
    assert(!app_metadata_parse_version("{\"version\":\"2.0\",\"version_name\":\"3.0\"}",
                                       version, sizeof(version), error, sizeof(error)));
    assert(strstr(error, "Conflicting"));

    UdpContext *udp = udp_create();
    assert(udp);
    int receiver = udp_bind(udp, 0);
    int sender = udp_bind(udp, 0);
    assert(receiver >= 0 && sender >= 0);
    int port = udp_get_port(udp, receiver);
    unsigned char sent[4096];
    unsigned char small[2048];
    unsigned char received[4096];
    for (size_t i = 0; i < sizeof(sent); i++)
        sent[i] = (unsigned char)(i & 0xff);
    assert(udp_send(udp, sender, "127.0.0.1", port, sent, sizeof(sent)));

    UdpDatagram datagram;
    int count = 0;
    for (int attempt = 0; attempt < 100 && count == 0; attempt++)
    {
        count = udp_recv(udp, receiver, small, sizeof(small), &datagram);
        if (count == 0)
            usleep(1000);
    }
    assert(count == UDP_RECV_TRUNCATED);
    assert(strstr(udp_get_error(udp), "not consumed"));
    assert(udp_recv(udp, receiver, received, sizeof(received), &datagram) == (int)sizeof(sent));
    assert(memcmp(received, sent, sizeof(sent)) == 0);
    udp_destroy(udp);

    char path[1024];
    snprintf(path, sizeof(path), "%s/main.js", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/main.ts", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/main.lua", root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/main.wat", wasm_root);
    unlink(path);
    snprintf(path, sizeof(path), "%s/main.wasm", wasm_root);
    unlink(path);
    rmdir(wasm_root);
    rmdir(root);
    puts("runtime consistency tests passed");
    return 0;
}