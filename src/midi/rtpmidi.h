#ifndef RTPMIDI_H
#define RTPMIDI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "network/udp_wrapper.h"
#include "midi_wrapper.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define RTPMIDI_MAX_SESSIONS 8
#define RTPMIDI_MAX_PEERS 4
#define RTPMIDI_MAX_NAME 128
#define RTPMIDI_DEFAULT_PORT 5004
#define RTPMIDI_RTP_PAYLOAD_TYPE 0x61 

    typedef enum
    {
        RTPMIDI_STATE_IDLE,
        RTPMIDI_STATE_LISTENING,
        RTPMIDI_STATE_INVITING_CTRL,
        RTPMIDI_STATE_INVITING_DATA,
        RTPMIDI_STATE_CONNECTED,
        RTPMIDI_STATE_CLOSED
    } RtpMidiSessionState;

    typedef struct
    {
        bool active;
        char host[UDP_MAX_HOST];
        int control_port;
        int data_port;
        uint32_t ssrc;
        char name[RTPMIDI_MAX_NAME];
        uint64_t last_sync_time; 
    } RtpMidiPeer;

    typedef struct
    {
        int handle;
        char name[RTPMIDI_MAX_NAME];
        int port;
        RtpMidiSessionState state;
        int peer_count;
    } RtpMidiSessionInfo;

    typedef struct RtpMidiContext RtpMidiContext;

    typedef void (*RtpMidiCallback)(int session_handle, const MidiMessage *message, void *user_data);

    RtpMidiContext *rtpmidi_create(UdpContext *udp_ctx);

    void rtpmidi_destroy(RtpMidiContext *ctx);

    int rtpmidi_create_session(RtpMidiContext *ctx, const char *name, int control_port);

    bool rtpmidi_connect(RtpMidiContext *ctx, int session, const char *host, int port);

    void rtpmidi_destroy_session(RtpMidiContext *ctx, int session);

    void rtpmidi_set_callback(RtpMidiContext *ctx, int session,
                              RtpMidiCallback callback, void *user_data);

    bool rtpmidi_send_message(RtpMidiContext *ctx, int session,
                              uint8_t status, uint8_t data1, uint8_t data2);

    bool rtpmidi_send_raw(RtpMidiContext *ctx, int session,
                          const uint8_t *data, size_t length);

    void rtpmidi_poll(RtpMidiContext *ctx);

    int rtpmidi_get_session_count(RtpMidiContext *ctx);

    bool rtpmidi_get_session_info(RtpMidiContext *ctx, int index, RtpMidiSessionInfo *info);

    const char *rtpmidi_get_error(RtpMidiContext *ctx);

#ifdef __cplusplus
}
#endif

#endif