#include "network/wasm_udp_bindings.h"
#include "tests/udp_mock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wasmtime.h>

typedef struct
{
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_linker_t *linker;
    WasmUdpBindingState *udp_state;
    UdpContext *udp;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t bind;
    wasmtime_func_t get_port;
    wasmtime_func_t send;
    wasmtime_func_t recv;
    wasmtime_func_t close;
} UdpRuntime;

static const char UDP_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"udp_bind\" (func $bind (param i32) (result i32)))\n"
    "  (import \"env\" \"udp_get_port\" (func $get_port (param i32) (result i32)))\n"
    "  (import \"env\" \"udp_send\" (func $send (param i32 i32 i32 i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"udp_recv\" (func $recv (param i32 i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"udp_close\" (func $close (param i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (export \"bind\" (func $bind))\n"
    "  (export \"get_port\" (func $get_port))\n"
    "  (export \"send\" (func $send))\n"
    "  (export \"recv\" (func $recv))\n"
    "  (export \"close\" (func $close)))";

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

static wasmtime_func_t get_function(UdpRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void udp_runtime_create(UdpRuntime *runtime)
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
    runtime->udp_state = wasm_udp_binding_state_create();
    assert(runtime->udp_state);
    runtime->udp = wasm_udp_context(runtime->udp_state);
    assert(runtime->udp);

    error = wasm_udp_register(runtime->linker, runtime->udp_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(UDP_MODULE_WAT, strlen(UDP_MODULE_WAT), &wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_module_new(runtime->engine, (const uint8_t *)wasm.data,
                                wasm.size, &runtime->module);
    wasm_byte_vec_delete(&wasm);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_linker_instantiate(runtime->linker, runtime->context,
                                        runtime->module, &runtime->instance, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);

    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        "memory", 6, &exported));
    assert(exported.kind == WASMTIME_EXTERN_MEMORY);
    runtime->memory = exported.of.memory;
    wasm_udp_set_memory(runtime->udp_state, runtime->context, &runtime->memory);
    runtime->bind = get_function(runtime, "bind");
    runtime->get_port = get_function(runtime, "get_port");
    runtime->send = get_function(runtime, "send");
    runtime->recv = get_function(runtime, "recv");
    runtime->close = get_function(runtime, "close");
}

static void udp_runtime_destroy(UdpRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasm_udp_clear_memory(runtime->udp_state);
    wasmtime_store_delete(runtime->store);
    wasm_udp_binding_state_destroy(runtime->udp_state);
    wasm_engine_delete(runtime->engine);
}

static int32_t call_i32(UdpRuntime *runtime, wasmtime_func_t *function,
                        wasmtime_val_t *args, size_t nargs)
{
    wasmtime_val_t result;
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 args, nargs, &result, 1, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
    assert(result.kind == WASMTIME_I32);
    return result.of.i32;
}

static void call_void(UdpRuntime *runtime, wasmtime_func_t *function,
                      wasmtime_val_t *args, size_t nargs)
{
    wasm_trap_t *trap = NULL;
    wasmtime_error_t *error = wasmtime_func_call(runtime->context, function,
                                                 args, nargs, NULL, 0, &trap);
    if (error)
        fail_wasmtime_error(error);
    if (trap)
        fail_wasm_trap(trap);
}

static int32_t bind_socket(UdpRuntime *runtime)
{
    wasmtime_val_t arg;
    arg.kind = WASMTIME_I32;
    arg.of.i32 = 0;
    return call_i32(runtime, &runtime->bind, &arg, 1);
}

static int32_t get_port(UdpRuntime *runtime, int32_t handle)
{
    wasmtime_val_t arg;
    arg.kind = WASMTIME_I32;
    arg.of.i32 = handle;
    return call_i32(runtime, &runtime->get_port, &arg, 1);
}

static uint8_t *memory_data(UdpRuntime *runtime)
{
    return wasmtime_memory_data(runtime->context, &runtime->memory);
}

static int32_t send_datagram(UdpRuntime *runtime, int32_t handle,
                             const char *host, int32_t port,
                             const uint8_t *data, int32_t length)
{
    static const int32_t host_offset = 64;
    static const int32_t data_offset = 256;
    wasmtime_val_t args[6];
    size_t host_length = strlen(host);
    uint8_t *memory = memory_data(runtime);

    memcpy(memory + host_offset, host, host_length);
    memcpy(memory + data_offset, data, (size_t)length);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = host_offset;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = (int32_t)host_length;
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = port;
    args[4].kind = WASMTIME_I32;
    args[4].of.i32 = data_offset;
    args[5].kind = WASMTIME_I32;
    args[5].of.i32 = length;
    return call_i32(runtime, &runtime->send, args, 6);
}

static int32_t receive_datagram(UdpRuntime *runtime, int32_t handle,
                                uint8_t *data, size_t data_capacity,
                                char *host, size_t host_capacity, int *port)
{
    static const int32_t data_offset = 512;
    static const int32_t info_offset = 1024;
    wasmtime_val_t args[4];
    uint8_t *memory = memory_data(runtime);
    int32_t received;
    uint32_t encoded_port;

    memset(memory + data_offset, 0, data_capacity);
    memset(memory + info_offset, 0, 68);
    args[0].kind = WASMTIME_I32;
    args[0].of.i32 = handle;
    args[1].kind = WASMTIME_I32;
    args[1].of.i32 = data_offset;
    args[2].kind = WASMTIME_I32;
    args[2].of.i32 = (int32_t)data_capacity;
    args[3].kind = WASMTIME_I32;
    args[3].of.i32 = info_offset;
    received = call_i32(runtime, &runtime->recv, args, 4);
    if (received <= 0)
        return received;

    assert((size_t)received <= data_capacity);
    memcpy(data, memory + data_offset, (size_t)received);
    memcpy(&encoded_port, memory + info_offset, sizeof(encoded_port));
    *port = (int)encoded_port;
    assert(host_capacity > 0);
    strncpy(host, (const char *)(memory + info_offset + 4), host_capacity - 1);
    host[host_capacity - 1] = '\0';
    return received;
}

int main(void)
{
    UdpRuntime runtime_a;
    UdpRuntime runtime_b;
    int32_t handle_a;
    int32_t handle_b;
    uint8_t received[8];
    char host[UDP_MAX_HOST];
    int port;
    const uint8_t sent_a[] = {11, 12, 13};
    const uint8_t sent_b[] = {21, 22};
    const uint8_t incoming_a[] = {31, 32};
    const uint8_t incoming_b[] = {41};
    const uint8_t sent_after_destroy[] = {51};
    wasmtime_val_t close_arg;

    udp_mock_reset();
    udp_runtime_create(&runtime_a);
    udp_runtime_create(&runtime_b);
    handle_a = bind_socket(&runtime_a);
    handle_b = bind_socket(&runtime_b);
    assert(handle_a == 0 && handle_b == 0);
    assert(get_port(&runtime_a, handle_a) != get_port(&runtime_b, handle_b));

    assert(send_datagram(&runtime_a, handle_a, "wasm-a", 4301,
                         sent_a, sizeof(sent_a)) == 1);
    assert(udp_mock_send_count(runtime_a.udp) == 1);
    assert(udp_mock_send_count(runtime_b.udp) == 0);
    assert(udp_mock_last_send_equals(runtime_a.udp, "wasm-a", 4301,
                                     sent_a, sizeof(sent_a)));
    assert(send_datagram(&runtime_b, handle_b, "wasm-b", 4302,
                         sent_b, sizeof(sent_b)) == 1);
    assert(udp_mock_last_send_equals(runtime_b.udp, "wasm-b", 4302,
                                     sent_b, sizeof(sent_b)));

    assert(udp_mock_enqueue(runtime_a.udp, handle_a, "from-a", 4311,
                            incoming_a, sizeof(incoming_a)));
    assert(udp_mock_enqueue(runtime_b.udp, handle_b, "from-b", 4312,
                            incoming_b, sizeof(incoming_b)));
    assert(receive_datagram(&runtime_a, handle_a, received, sizeof(received),
                            host, sizeof(host), &port) == (int32_t)sizeof(incoming_a));
    assert(memcmp(received, incoming_a, sizeof(incoming_a)) == 0);
    assert(strcmp(host, "from-a") == 0 && port == 4311);
    assert(receive_datagram(&runtime_b, handle_b, received, sizeof(received),
                            host, sizeof(host), &port) == (int32_t)sizeof(incoming_b));
    assert(memcmp(received, incoming_b, sizeof(incoming_b)) == 0);
    assert(strcmp(host, "from-b") == 0 && port == 4312);

    udp_runtime_destroy(&runtime_b);
    assert(udp_mock_live_count() == 1);
    assert(send_datagram(&runtime_a, handle_a, "wasm-a-again", 4303,
                         sent_after_destroy, sizeof(sent_after_destroy)) == 1);
    assert(udp_mock_last_send_equals(runtime_a.udp, "wasm-a-again", 4303,
                                     sent_after_destroy, sizeof(sent_after_destroy)));
    close_arg.kind = WASMTIME_I32;
    close_arg.of.i32 = handle_a;
    call_void(&runtime_a, &runtime_a.close, &close_arg, 1);
    assert(get_port(&runtime_a, handle_a) == -1);
    udp_runtime_destroy(&runtime_a);
    assert(udp_mock_live_count() == 0);
    assert(udp_mock_destroy_count() == 2);

    puts("{\"runtime\":\"wasmtime\",\"operation\":\"udp.bind\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"udp.receive\",\"result\":{\"length\":2,\"payloadVerified\":true},\"errorKind\":\"none\",\"errorCode\":\"\"}");

    puts("WASM UDP binding state isolation test passed");
    return 0;
}