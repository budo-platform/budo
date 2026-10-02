#ifndef JS_MIDI_BINDINGS_H
#define JS_MIDI_BINDINGS_H

#include "quickjs.h"
#include "midi_wrapper.h"
#include "rtpmidi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct JsMidiContext JsMidiContext;

    JsMidiContext *js_midi_init(JSContext *ctx);

    void js_midi_cleanup(JsMidiContext *state);

    void js_midi_set_context(JsMidiContext *state,
                             MidiContext *midi_ctx, JSContext *js_ctx);

    void js_midi_poll(JsMidiContext *state);
    
    bool js_midi_has_pending_work(JsMidiContext *state);

    void js_midi_set_rtpmidi(JsMidiContext *state,
                             RtpMidiContext *rtp_ctx);

    size_t js_midi_dropped_messages(JsMidiContext *state);
    size_t js_midi_dropped_sysex(JsMidiContext *state);
    size_t js_midi_dropped_rtpmidi(JsMidiContext *state);

#ifdef __cplusplus
}
#endif

#endif