#include "network/wasm_network_bindings.h"
#include "tests/network_mock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

typedef struct NetworkRuntime
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmNetworkBindingState *network_state;
    NetworkContext *network;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t fetch;
    wasmtime_func_t status;
    wasmtime_func_t status_code;
    wasmtime_func_t body_len;
    wasmtime_func_t body;
    wasmtime_func_t release;
} NetworkRuntime;

static const char NETWORK_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"network_fetch\" (func $fetch (param i32 i32 i32 i32 i32 i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"network_fetch_status\" (func $status (param i32) (result i32)))\n"
    "  (import \"env\" \"network_fetch_status_code\" (func $status_code (param i32) (result i32)))\n"
    "  (import \"env\" \"network_fetch_response_body_len\" (func $body_len (param i32) (result i32)))\n"
    "  (import \"env\" \"network_fetch_response_body\" (func $body (param i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"network_fetch_release\" (func $release (param i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (export \"fetch\" (func $fetch))\n"
    "  (export \"status\" (func $status))\n"
    "  (export \"status_code\" (func $status_code))\n"
    "  (export \"body_len\" (func $body_len))\n"
    "  (export \"body\" (func $body))\n"
    "  (export \"release\" (func $release)))";

static void fail_wasmtime_error(wasmtime_error_t *error)
{
    wasm_byte_vec_t message;
    wasmtime_error_message(error, &message);
    fprintf(stderr, "Wasmtime error: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasmtime_error_delete(error);
    assert(0);
}

static void fail_wasm_trap(wasm_trap_t *trap)
{
    wasm_byte_vec_t message;
    wasm_trap_message(trap, &message);
    fprintf(stderr, "WASM trap: %.*s\n", (int)message.size, message.data);
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(trap);
    assert(0);
}

static wasmtime_func_t get_function(NetworkRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void network_runtime_create(NetworkRuntime *runtime)
{
    wasm_byte_vec_t wasm;
    wasmtime_extern_t exported;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error;

    memset(runtime, 0, sizeof(*runtime));
    runtime->engine = wasm_engine_new();
    assert(runtime->engine);
    runtime->store = wasmtime_store_new(runtime->engine, NULL, NULL);
    assert(runtime->store);
    runtime->context = wasmtime_store_context(runtime->store);
    runtime->linker = wasmtime_linker_new(runtime->engine);
    assert(runtime->linker);
    runtime->network_state = wasm_network_binding_state_create(NULL);
    assert(runtime->network_state);
    runtime->network = wasm_network_context(runtime->network_state);
    assert(runtime->network);

    error = wasm_network_register_state(runtime->linker,
                                        runtime->network_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(NETWORK_MODULE_WAT,
                              strlen(NETWORK_MODULE_WAT), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime->engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime->module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime->linker, runtime->context,
                                        runtime->module, &runtime->instance,
                                        &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "memory", 6, &exported));
    assert(exported.kind == WASMTIME_EXTERN_MEMORY);
    runtime->memory = exported.of.memory;
    wasm_network_binding_state_set_memory(runtime->network_state,
                                          runtime->context,
                                          &runtime->memory);
    runtime->fetch = get_function(runtime, "fetch");
    runtime->status = get_function(runtime, "status");
    runtime->status_code = get_function(runtime, "status_code");
    runtime->body_len = get_function(runtime, "body_len");
    runtime->body = get_function(runtime, "body");
    runtime->release = get_function(runtime, "release");
}

static void network_runtime_destroy(NetworkRuntime *runtime)
{
    wasm_network_shutdown(runtime->network_state);
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasmtime_store_delete(runtime->store);
    wasm_network_binding_state_destroy(runtime->network_state);
    wasm_engine_delete(runtime->engine);
    memset(runtime, 0, sizeof(*runtime));
}

static int32_t call_i32(NetworkRuntime *runtime, wasmtime_func_t *function,
                        wasmtime_val_t *args, size_t nargs)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, function, args, nargs, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static void call_void(NetworkRuntime *runtime, wasmtime_func_t *function,
                      wasmtime_val_t *args, size_t nargs)
{
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(
        runtime->context, function, args, nargs, NULL, 0, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

static uint8_t *memory_data(NetworkRuntime *runtime)
{
    return wasmtime_memory_data(runtime->context, &runtime->memory);
}

static int32_t start_fetch(NetworkRuntime *runtime, const char *url)
{
    static const int32_t method_offset = 0;
    static const int32_t url_offset = 64;
    wasmtime_val_t args[8];
    size_t url_length = strlen(url);
    uint8_t *memory = memory_data(runtime);

    memcpy(memory + method_offset, "GET", 3);
    memcpy(memory + url_offset, url, url_length);
    for (int index = 0; index < 8; index++)
        args[index].kind = WASMTIME_I32;
    args[0].of.i32 = method_offset;
    args[1].of.i32 = 3;
    args[2].of.i32 = url_offset;
    args[3].of.i32 = (int32_t)url_length;
    args[4].of.i32 = 0;
    args[5].of.i32 = 0;
    args[6].of.i32 = 0;
    args[7].of.i32 = 0;
    return call_i32(runtime, &runtime->fetch, args, 8);
}

static int32_t call_slot(NetworkRuntime *runtime, wasmtime_func_t *function,
                         int32_t slot)
{
    wasmtime_val_t arg;
    arg.kind = WASMTIME_I32;
    arg.of.i32 = slot;
    return call_i32(runtime, function, &arg, 1);
}

static void assert_response(NetworkRuntime *runtime, int32_t slot,
                            int status, const char *body)
{
    static const int32_t body_offset = 4096;
    wasmtime_val_t args[3];
    int32_t expected_length = (int32_t)strlen(body);
    int32_t written;

    assert(call_slot(runtime, &runtime->status, slot) == 1);
    assert(call_slot(runtime, &runtime->status_code, slot) == status);
    assert(call_slot(runtime, &runtime->body_len, slot) == expected_length);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = slot;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = body_offset;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = 128;
    written = call_i32(runtime, &runtime->body, args, 3);
    assert(written == expected_length);
    assert(memcmp(memory_data(runtime) + body_offset, body,
                  (size_t)expected_length) == 0);
}

static void release_slot(NetworkRuntime *runtime, int32_t slot)
{
    wasmtime_val_t arg;
    arg.kind = WASMTIME_I32;
    arg.of.i32 = slot;
    call_void(runtime, &runtime->release, &arg, 1);
}

int main(void)
{
    NetworkRuntime runtime_a;
    NetworkRuntime runtime_b;
    int32_t slot_a;
    int32_t slot_b;
    int32_t released_slot;
    int32_t slot_after_destroy;
    int request_a;
    int request_b;
    int released_request;
    int request_after_destroy;

    network_mock_reset();
    network_runtime_create(&runtime_a);
    network_runtime_create(&runtime_b);
    slot_a = start_fetch(&runtime_a, "https://wasm-a.test/first");
    slot_b = start_fetch(&runtime_b, "https://wasm-b.test/stale");
    assert(slot_a == 0 && slot_b == 0);
    request_a = network_mock_find_request(runtime_a.network,
                                          "https://wasm-a.test/first");
    request_b = network_mock_find_request(runtime_b.network,
                                          "https://wasm-b.test/stale");
    assert(request_a > 0 && request_b > 0 && request_a != request_b);

    network_runtime_destroy(&runtime_b);
    assert(network_mock_live_count() == 1);
    assert(network_mock_complete(request_b, 200, "stale-wasm"));
    assert(network_mock_discarded_completion_count() == 1);

    assert(network_mock_complete(request_a, 221, "wasm-a"));
    wasm_network_poll(runtime_a.network_state);
    assert_response(&runtime_a, slot_a, 221, "wasm-a");
    release_slot(&runtime_a, slot_a);

    released_slot = start_fetch(&runtime_a,
                                "https://wasm-a.test/released");
    assert(released_slot == 0);
    released_request = network_mock_find_request(
        runtime_a.network, "https://wasm-a.test/released");
    assert(released_request > 0);
    release_slot(&runtime_a, released_slot);

    slot_after_destroy = start_fetch(&runtime_a,
                                     "https://wasm-a.test/second");
    assert(slot_after_destroy == 0);
    request_after_destroy = network_mock_find_request(
        runtime_a.network, "https://wasm-a.test/second");
    assert(request_after_destroy > 0);
    assert(network_mock_complete(released_request, 223, "released"));
    wasm_network_poll(runtime_a.network_state);
    assert(call_slot(&runtime_a, &runtime_a.status, slot_after_destroy) == 0);
    assert(network_mock_complete(request_after_destroy, 222, "wasm-again"));
    wasm_network_poll(runtime_a.network_state);
    assert_response(&runtime_a, slot_after_destroy, 222, "wasm-again");
    release_slot(&runtime_a, slot_after_destroy);

    network_runtime_destroy(&runtime_a);
    assert(network_mock_live_count() == 0);
    assert(network_mock_destroy_count() == 2);
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"network.complete\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"network.staleCompletionDiscarded\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"network.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("WASM network binding state isolation test passed");
    return 0;
}