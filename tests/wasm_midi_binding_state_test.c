#include "midi/wasm_midi_bindings.h"
#include "tests/midi_mock.h"
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
    WasmMidiBindingState *midi_state;
    UdpContext *udp;
    RtpMidiContext *rtpmidi;
    wasmtime_instance_t instance;
    wasmtime_memory_t memory;
    wasmtime_func_t get_output_count;
    wasmtime_func_t send_raw;
    wasmtime_func_t create_session;
    wasmtime_func_t get_session_count;
} MidiRuntime;

static const char MIDI_MODULE_WAT[] =
    "(module\n"
    "  (import \"env\" \"midi_get_output_count\" (func $count (result i32)))\n"
    "  (import \"env\" \"midi_send_raw\" (func $send (param i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"rtpmidi_create_session\" (func $create (param i32 i32 i32) (result i32)))\n"
    "  (import \"env\" \"rtpmidi_get_session_count\" (func $session_count (result i32)))\n"
    "  (memory (export \"memory\") 1)\n"
    "  (export \"get_output_count\" (func $count))\n"
    "  (export \"send_raw\" (func $send))\n"
    "  (export \"create_session\" (func $create))\n"
    "  (export \"get_session_count\" (func $session_count)))";

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

static wasmtime_func_t exported_func(MidiRuntime *runtime, const char *name)
{
    wasmtime_extern_t exported;
    assert(wasmtime_instance_export_get(runtime->context, &runtime->instance,
                                        name, strlen(name), &exported));
    assert(exported.kind == WASMTIME_EXTERN_FUNC);
    return exported.of.func;
}

static void midi_runtime_create(MidiRuntime *runtime)
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
    runtime->midi_state = wasm_midi_binding_state_create();
    assert(runtime->midi_state);
    runtime->udp = udp_create();
    assert(runtime->udp);
    runtime->rtpmidi = rtpmidi_create(runtime->udp);
    assert(runtime->rtpmidi);
    wasm_midi_set_rtpmidi(runtime->midi_state, runtime->rtpmidi);

    error = wasm_midi_register(runtime->linker, runtime->midi_state);
    if (error)
        fail_wasmtime_error(error);
    error = wasmtime_wat2wasm(MIDI_MODULE_WAT, strlen(MIDI_MODULE_WAT), &wasm);
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
    wasm_midi_set_memory(runtime->midi_state, runtime->context,
                         &runtime->memory);
    runtime->get_output_count = exported_func(runtime, "get_output_count");
    runtime->send_raw = exported_func(runtime, "send_raw");
    runtime->create_session = exported_func(runtime, "create_session");
    runtime->get_session_count = exported_func(runtime, "get_session_count");
}

static void midi_runtime_destroy(MidiRuntime *runtime)
{
    wasmtime_module_delete(runtime->module);
    wasmtime_linker_delete(runtime->linker);
    wasm_midi_clear_memory(runtime->midi_state);
    wasmtime_store_delete(runtime->store);
    wasm_midi_set_rtpmidi(runtime->midi_state, NULL);
    wasm_midi_binding_state_destroy(runtime->midi_state);
    rtpmidi_destroy(runtime->rtpmidi);
    udp_destroy(runtime->udp);
    wasm_engine_delete(runtime->engine);
}

static int32_t call_i32(MidiRuntime *runtime, wasmtime_func_t *function,
                        const wasmtime_val_t *args, size_t nargs)
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

static int32_t get_output_count(MidiRuntime *runtime)
{
    return call_i32(runtime, &runtime->get_output_count, NULL, 0);
}

static int32_t send_raw(MidiRuntime *runtime, int32_t pointer, int32_t length)
{
    wasmtime_val_t args[3] = {
        {.kind = WASMTIME_I32, .of.i32 = 0},
        {.kind = WASMTIME_I32, .of.i32 = pointer},
        {.kind = WASMTIME_I32, .of.i32 = length},
    };
    return call_i32(runtime, &runtime->send_raw, args, 3);
}

static int32_t create_session(MidiRuntime *runtime, int32_t pointer,
                              int32_t length)
{
    wasmtime_val_t args[3] = {
        {.kind = WASMTIME_I32, .of.i32 = pointer},
        {.kind = WASMTIME_I32, .of.i32 = length},
        {.kind = WASMTIME_I32, .of.i32 = 0},
    };
    return call_i32(runtime, &runtime->create_session, args, 3);
}

static void write_memory(MidiRuntime *runtime, int32_t pointer,
                         const uint8_t *data, size_t length)
{
    uint8_t *memory = wasmtime_memory_data(runtime->context, &runtime->memory);
    size_t memory_size = wasmtime_memory_data_size(runtime->context,
                                                   &runtime->memory);
    assert(pointer >= 0 && (size_t)pointer <= memory_size &&
           length <= memory_size - (size_t)pointer);
    memcpy(memory + pointer, data, length);
}

int main(void)
{
    MidiRuntime runtime_a;
    MidiRuntime runtime_b;
    const uint8_t bytes_a[] = {0xf0, 0x11, 0xf7};
    const uint8_t bytes_b[] = {0xf0, 0x22, 0xf7};
    const char name_a[] = "wasm-a";
    const char name_b[] = "wasm-b";
    MidiContext *midi_a;
    MidiContext *midi_b;

    udp_mock_reset();
    midi_mock_reset();
    midi_runtime_create(&runtime_a);
    midi_runtime_create(&runtime_b);
    assert(midi_mock_live_count() == 0);

    assert(get_output_count(&runtime_a) == 1);
    assert(get_output_count(&runtime_b) == 2);
    midi_a = midi_mock_context(0);
    midi_b = midi_mock_context(1);
    assert(midi_a && midi_b);

    write_memory(&runtime_a, 16, bytes_a, sizeof(bytes_a));
    write_memory(&runtime_b, 16, bytes_b, sizeof(bytes_b));
    assert(send_raw(&runtime_a, 16, sizeof(bytes_a)) == 1);
    assert(midi_mock_last_raw_byte(midi_a, 1) == 0x11);
    assert(midi_mock_last_raw_size(midi_b) == 0);
    assert(send_raw(&runtime_b, 16, sizeof(bytes_b)) == 1);
    assert(midi_mock_last_raw_byte(midi_b, 1) == 0x22);

    assert(send_raw(&runtime_a, 65535, 4) == 0);
    assert(midi_mock_last_raw_size(midi_a) == sizeof(bytes_a));

    write_memory(&runtime_a, 64, (const uint8_t *)name_a, strlen(name_a));
    write_memory(&runtime_b, 64, (const uint8_t *)name_b, strlen(name_b));
    assert(create_session(&runtime_a, 64, strlen(name_a)) == 0);
    assert(create_session(&runtime_b, 64, strlen(name_b)) == 0);
    assert(strcmp(rtpmidi_mock_session_name(runtime_a.rtpmidi, 0), name_a) == 0);
    assert(strcmp(rtpmidi_mock_session_name(runtime_b.rtpmidi, 0), name_b) == 0);
    assert(call_i32(&runtime_a, &runtime_a.get_session_count, NULL, 0) == 1);
    assert(call_i32(&runtime_b, &runtime_b.get_session_count, NULL, 0) == 1);

    midi_runtime_destroy(&runtime_b);
    assert(midi_mock_live_count() == 1);
    assert(rtpmidi_mock_live_count() == 1);
    assert(get_output_count(&runtime_a) == 1);
    assert(send_raw(&runtime_a, 16, sizeof(bytes_a)) == 1);
    assert(midi_mock_last_raw_byte(midi_a, 1) == 0x11);

    midi_runtime_destroy(&runtime_a);
    assert(midi_mock_destroy_count() == 2);
    assert(rtpmidi_mock_destroy_count() == 2);
    assert(udp_mock_destroy_count() == 2);
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"midi.localIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"midi.rtpmidiIsolation\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("{\"runtime\":\"wasmtime\",\"operation\":\"midi.cleanup\",\"result\":true,\"errorKind\":\"none\",\"errorCode\":\"\"}");
    puts("WASM MIDI binding state isolation test passed");
    return 0;
}