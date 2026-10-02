#include "rtpmidi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define APPLEMIDI_SIGNATURE 0xFFFF

#define APPLEMIDI_CMD_INVITATION 0x494E 
#define APPLEMIDI_CMD_ACCEPT     0x4F4B 
#define APPLEMIDI_CMD_REJECT     0x4E4F 
#define APPLEMIDI_CMD_END        0x4259 
#define APPLEMIDI_CMD_SYNC       0x434B 

#define APPLEMIDI_PROTOCOL_VERSION 2

#define RTP_VERSION 2
#define RTP_MIDI_PT RTPMIDI_RTP_PAYLOAD_TYPE

#define INVITE_TIMEOUT_US 1000000  
#define INVITE_MAX_RETRIES 5
#define SYNC_INTERVAL_US 10000000  

typedef struct
{
    bool active;
    char name[RTPMIDI_MAX_NAME];
    uint32_t ssrc;
    int control_socket; 
    int data_socket;    
    int control_port;
    int data_port;

    RtpMidiSessionState state;
    RtpMidiPeer peers[RTPMIDI_MAX_PEERS];
    int peer_count;

    uint16_t seq_number;
    uint32_t rtp_timestamp;

    uint32_t invite_token;
    char invite_host[UDP_MAX_HOST];
    int invite_port;
    int invite_retries;
    uint64_t invite_sent_time;

    uint64_t last_sync_sent;

    RtpMidiCallback callback;
    void *user_data;
} RtpMidiSession;

struct RtpMidiContext
{
    UdpContext *udp;
    RtpMidiSession sessions[RTPMIDI_MAX_SESSIONS];
    char error_msg[256];
};

static void set_error(RtpMidiContext *ctx, const char *msg)
{
    if (ctx && msg)
    {
        strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
        ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
    }
}

static uint64_t get_time_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000ULL +
           (uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000ULL /
               (uint64_t)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
#endif
}

static uint32_t generate_ssrc(void)
{
    
    uint32_t t = (uint32_t)time(NULL);
    uint32_t r = (uint32_t)rand();
    return t ^ r ^ (r << 16);
}

static uint32_t generate_token(void)
{
    return (uint32_t)rand() ^ ((uint32_t)rand() << 16);
}

static void put_u32be(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val);
}

static uint32_t get_u32be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static void put_u16be(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val);
}

static uint16_t get_u16be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static void put_u64be(uint8_t *buf, uint64_t val)
{
    put_u32be(buf, (uint32_t)(val >> 32));
    put_u32be(buf + 4, (uint32_t)(val));
}

static uint64_t get_u64be(const uint8_t *buf)
{
    return ((uint64_t)get_u32be(buf) << 32) | (uint64_t)get_u32be(buf + 4);
}

static int build_session_msg(uint8_t *buf, uint16_t command,
                             uint32_t version, uint32_t token,
                             uint32_t ssrc, const char *name)
{
    int offset = 0;
    put_u16be(buf + offset, APPLEMIDI_SIGNATURE);
    offset += 2;
    put_u16be(buf + offset, command);
    offset += 2;
    put_u32be(buf + offset, version);
    offset += 4;
    put_u32be(buf + offset, token);
    offset += 4;
    put_u32be(buf + offset, ssrc);
    offset += 4;

    if (name)
    {
        size_t name_len = strlen(name);
        if (name_len > RTPMIDI_MAX_NAME - 1)
            name_len = RTPMIDI_MAX_NAME - 1;
        memcpy(buf + offset, name, name_len);
        offset += (int)name_len;
        buf[offset++] = '\0';
    }

    return offset;
}

static int build_sync_msg(uint8_t *buf, uint32_t ssrc, uint8_t count,
                          uint64_t t1, uint64_t t2, uint64_t t3)
{
    int offset = 0;
    put_u16be(buf + offset, APPLEMIDI_SIGNATURE);
    offset += 2;
    put_u16be(buf + offset, APPLEMIDI_CMD_SYNC);
    offset += 2;
    put_u32be(buf + offset, ssrc);
    offset += 4;
    buf[offset++] = count;
    buf[offset++] = 0; 
    buf[offset++] = 0;
    buf[offset++] = 0;
    put_u64be(buf + offset, t1);
    offset += 8;
    put_u64be(buf + offset, t2);
    offset += 8;
    put_u64be(buf + offset, t3);
    offset += 8;
    return offset;
}

static bool is_applemidi_packet(const uint8_t *data, int len)
{
    if (len < 4)
        return false;
    return get_u16be(data) == APPLEMIDI_SIGNATURE;
}

static void send_invitation(RtpMidiContext *ctx, RtpMidiSession *sess,
                            int sock_handle, const char *host, int port)
{
    uint8_t buf[256];
    int len = build_session_msg(buf, APPLEMIDI_CMD_INVITATION,
                                APPLEMIDI_PROTOCOL_VERSION,
                                sess->invite_token, sess->ssrc, sess->name);
    udp_send(ctx->udp, sock_handle, host, port, buf, len);
}

static void send_accept(RtpMidiContext *ctx, RtpMidiSession *sess,
                        int sock_handle, const char *host, int port,
                        uint32_t token)
{
    uint8_t buf[256];
    int len = build_session_msg(buf, APPLEMIDI_CMD_ACCEPT,
                                APPLEMIDI_PROTOCOL_VERSION,
                                token, sess->ssrc, sess->name);
    udp_send(ctx->udp, sock_handle, host, port, buf, len);
}

static void send_end(RtpMidiContext *ctx, RtpMidiSession *sess,
                     RtpMidiPeer *peer)
{
    uint8_t buf[256];
    uint32_t token = generate_token();
    int len = build_session_msg(buf, APPLEMIDI_CMD_END,
                                APPLEMIDI_PROTOCOL_VERSION,
                                token, sess->ssrc, NULL);
    udp_send(ctx->udp, sess->control_socket, peer->host, peer->control_port,
             buf, len);
}

static void send_sync_ck0(RtpMidiContext *ctx, RtpMidiSession *sess,
                          RtpMidiPeer *peer)
{
    uint64_t now = get_time_us();
    uint8_t buf[64];
    int len = build_sync_msg(buf, sess->ssrc, 0, now, 0, 0);
    udp_send(ctx->udp, sess->data_socket, peer->host, peer->data_port,
             buf, len);
}

static RtpMidiPeer *find_or_add_peer(RtpMidiSession *sess,
                                     const char *host, int control_port)
{
    
    for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
    {
        if (sess->peers[i].active &&
            strcmp(sess->peers[i].host, host) == 0 &&
            sess->peers[i].control_port == control_port)
        {
            return &sess->peers[i];
        }
    }

    for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
    {
        if (!sess->peers[i].active)
        {
            RtpMidiPeer *p = &sess->peers[i];
            memset(p, 0, sizeof(*p));
            p->active = true;
            strncpy(p->host, host, UDP_MAX_HOST - 1);
            p->control_port = control_port;
            p->data_port = control_port + 1;
            sess->peer_count++;
            return p;
        }
    }

    return NULL;
}

static bool send_rtp_midi(RtpMidiContext *ctx, RtpMidiSession *sess,
                          const uint8_t *midi_data, int midi_len)
{
    if (midi_len <= 0 || midi_len > 512)
        return false;

    uint8_t buf[1024];
    int offset = 0;

    buf[offset++] = (RTP_VERSION << 6); 
    buf[offset++] = RTP_MIDI_PT;        
    put_u16be(buf + offset, sess->seq_number++);
    offset += 2;
    put_u32be(buf + offset, sess->rtp_timestamp);
    offset += 4;
    sess->rtp_timestamp += 1; 
    put_u32be(buf + offset, sess->ssrc);
    offset += 4;

    if (midi_len <= 15)
    {
        buf[offset++] = (uint8_t)(midi_len & 0x0F); 
    }
    else
    {
        
        buf[offset++] = 0x80 | (uint8_t)((midi_len >> 8) & 0x0F);
        buf[offset++] = (uint8_t)(midi_len & 0xFF);
    }

    memcpy(buf + offset, midi_data, midi_len);
    offset += midi_len;

    bool any_sent = false;
    for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
    {
        if (sess->peers[i].active)
        {
            if (udp_send(ctx->udp, sess->data_socket,
                         sess->peers[i].host, sess->peers[i].data_port,
                         buf, offset))
            {
                any_sent = true;
            }
        }
    }

    return any_sent;
}

static void parse_rtp_midi(RtpMidiSession *sess, const uint8_t *data, int len)
{
    if (len < 13)
        return; 

    uint8_t version = (data[0] >> 6) & 0x03;
    if (version != RTP_VERSION)
        return;

    int offset = 12; 

    if (offset >= len)
        return;

    bool long_header = (data[offset] & 0x80) != 0;
    
    int midi_len;

    if (long_header)
    {
        if (offset + 2 > len)
            return;
        midi_len = ((data[offset] & 0x0F) << 8) | data[offset + 1];
        offset += 2;
    }
    else
    {
        midi_len = data[offset] & 0x0F;
        offset += 1;
    }

    if (midi_len <= 0 || offset + midi_len > len)
        return;

    const uint8_t *midi_data = data + offset;
    int midi_pos = 0;
    uint8_t running_status = 0;

    while (midi_pos < midi_len && sess->callback)
    {
        uint8_t byte = midi_data[midi_pos];

        if (byte == 0x00 && midi_pos + 1 < midi_len &&
            (midi_data[midi_pos + 1] & 0x80))
        {
            midi_pos++; 
            continue;
        }

        uint8_t status;
        int data_start;

        if (byte & 0x80)
        {
            status = byte;
            running_status = byte;
            data_start = midi_pos + 1;
        }
        else
        {
            
            status = running_status;
            data_start = midi_pos;
        }

        if (status == 0)
        {
            midi_pos++;
            continue;
        }

        int msg_data_len;
        uint8_t type = status & 0xF0;

        if (status == 0xF0)
        {
            
            while (midi_pos < midi_len && midi_data[midi_pos] != 0xF7)
                midi_pos++;
            if (midi_pos < midi_len)
                midi_pos++; 
            continue;
        }
        else if (type == 0xC0 || type == 0xD0)
        {
            msg_data_len = 1;
        }
        else if (type >= 0x80 && type <= 0xE0)
        {
            msg_data_len = 2;
        }
        else if (status >= 0xF1 && status <= 0xF7)
        {
            
            midi_pos = data_start + 1;
            continue;
        }
        else if (status >= 0xF8)
        {
            
            MidiMessage msg = {0};
            msg.status = status;
            sess->callback(0, &msg, sess->user_data);
            midi_pos = data_start;
            continue;
        }
        else
        {
            midi_pos++;
            continue;
        }

        if (data_start + msg_data_len > midi_len)
            break;

        MidiMessage msg = {0};
        msg.status = status;
        msg.data1 = midi_data[data_start];
        if (msg_data_len >= 2)
            msg.data2 = midi_data[data_start + 1];
        msg.timestamp = get_time_us();

        sess->callback(0, &msg, sess->user_data);

        midi_pos = data_start + msg_data_len;
    }
}

static void handle_control_packet(RtpMidiContext *ctx, RtpMidiSession *sess,
                                  const uint8_t *data, int len,
                                  const char *from_host, int from_port)
{
    if (len < 16)
        return; 

    uint16_t command = get_u16be(data + 2);
    uint32_t token = get_u32be(data + 8);
    uint32_t remote_ssrc = get_u32be(data + 12);

    char remote_name[RTPMIDI_MAX_NAME] = {0};
    if (len > 16)
    {
        size_t name_len = len - 16;
        if (name_len >= RTPMIDI_MAX_NAME)
            name_len = RTPMIDI_MAX_NAME - 1;
        memcpy(remote_name, data + 16, name_len);
        remote_name[name_len] = '\0';
        
        size_t actual = strlen(remote_name);
        remote_name[actual] = '\0';
    }

    switch (command)
    {
    case APPLEMIDI_CMD_INVITATION:
    {
        
        RtpMidiPeer *peer = find_or_add_peer(sess, from_host, from_port);
        if (peer)
        {
            peer->ssrc = remote_ssrc;
            if (remote_name[0])
                strncpy(peer->name, remote_name, RTPMIDI_MAX_NAME - 1);

            send_accept(ctx, sess, sess->control_socket, from_host, from_port, token);

            if (sess->state == RTPMIDI_STATE_LISTENING ||
                sess->state == RTPMIDI_STATE_IDLE)
            {
                sess->state = RTPMIDI_STATE_CONNECTED;
            }
        }
        break;
    }

    case APPLEMIDI_CMD_ACCEPT:
    {
        if (sess->state == RTPMIDI_STATE_INVITING_CTRL &&
            token == sess->invite_token)
        {
            
            RtpMidiPeer *peer = find_or_add_peer(sess, from_host, from_port);
            if (peer)
            {
                peer->ssrc = remote_ssrc;
                if (remote_name[0])
                    strncpy(peer->name, remote_name, RTPMIDI_MAX_NAME - 1);
            }

            sess->state = RTPMIDI_STATE_INVITING_DATA;
            sess->invite_retries = 0;
            sess->invite_sent_time = 0; 
        }
        else if (sess->state == RTPMIDI_STATE_INVITING_DATA &&
                 token == sess->invite_token)
        {
            
            sess->state = RTPMIDI_STATE_CONNECTED;

            for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
            {
                if (sess->peers[i].active)
                {
                    send_sync_ck0(ctx, sess, &sess->peers[i]);
                }
            }
            sess->last_sync_sent = get_time_us();
        }
        break;
    }

    case APPLEMIDI_CMD_REJECT:
    {
        if (sess->state == RTPMIDI_STATE_INVITING_CTRL ||
            sess->state == RTPMIDI_STATE_INVITING_DATA)
        {
            sess->state = RTPMIDI_STATE_LISTENING;
        }
        break;
    }

    case APPLEMIDI_CMD_END:
    {
        
        for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
        {
            if (sess->peers[i].active &&
                sess->peers[i].ssrc == remote_ssrc)
            {
                sess->peers[i].active = false;
                sess->peer_count--;
                break;
            }
        }
        if (sess->peer_count <= 0)
        {
            sess->state = RTPMIDI_STATE_LISTENING;
            sess->peer_count = 0;
        }
        break;
    }

    default:
        break;
    }
}

static void handle_data_packet(RtpMidiContext *ctx, RtpMidiSession *sess,
                               const uint8_t *data, int len,
                               const char *from_host, int from_port)
{
    if (is_applemidi_packet(data, len))
    {
        
        if (len >= 4)
        {
            uint16_t command = get_u16be(data + 2);

            if (command == APPLEMIDI_CMD_INVITATION)
            {
                
                if (len >= 16)
                {
                    uint32_t token = get_u32be(data + 8);
                    send_accept(ctx, sess, sess->data_socket, from_host, from_port, token);
                }
            }
            else if (command == APPLEMIDI_CMD_ACCEPT)
            {
                
                if (len >= 16)
                {
                    uint32_t token = get_u32be(data + 8);
                    if (sess->state == RTPMIDI_STATE_INVITING_DATA &&
                        token == sess->invite_token)
                    {
                        sess->state = RTPMIDI_STATE_CONNECTED;
                        sess->last_sync_sent = get_time_us();
                    }
                }
            }
            else if (command == APPLEMIDI_CMD_SYNC && len >= 36)
            {
                
                uint32_t remote_ssrc = get_u32be(data + 4);
                uint8_t count = data[8];
                uint64_t t1 = get_u64be(data + 12);
                uint64_t t2 = get_u64be(data + 20);
                uint64_t t3 = get_u64be(data + 28);
                uint64_t now = get_time_us();

                (void)remote_ssrc;

                if (count == 0)
                {
                    
                    uint8_t buf[64];
                    int msg_len = build_sync_msg(buf, sess->ssrc, 1, t1, now, 0);
                    udp_send(ctx->udp, sess->data_socket, from_host, from_port,
                             buf, msg_len);
                }
                else if (count == 1)
                {
                    
                    uint8_t buf[64];
                    int msg_len = build_sync_msg(buf, sess->ssrc, 2, t1, t2, now);
                    udp_send(ctx->udp, sess->data_socket, from_host, from_port,
                             buf, msg_len);
                }
                
                (void)t3;
            }
        }
        return;
    }

    parse_rtp_midi(sess, data, len);
}

static void poll_session(RtpMidiContext *ctx, RtpMidiSession *sess)
{
    if (!sess->active)
        return;

    uint8_t buf[2048];
    UdpDatagram dgram;

    for (int n = 0; n < 64; n++)
    {
        int received = udp_recv(ctx->udp, sess->control_socket, buf, sizeof(buf), &dgram);
        if (received <= 0)
            break;
        if (is_applemidi_packet(buf, received))
        {
            handle_control_packet(ctx, sess, buf, received, dgram.host, dgram.port);
        }
    }

    for (int n = 0; n < 128; n++)
    {
        int received = udp_recv(ctx->udp, sess->data_socket, buf, sizeof(buf), &dgram);
        if (received <= 0)
            break;
        handle_data_packet(ctx, sess, buf, received, dgram.host, dgram.port);
    }

    uint64_t now = get_time_us();

    if (sess->state == RTPMIDI_STATE_INVITING_CTRL)
    {
        if (now - sess->invite_sent_time >= INVITE_TIMEOUT_US)
        {
            if (sess->invite_retries >= INVITE_MAX_RETRIES)
            {
                sess->state = RTPMIDI_STATE_LISTENING;
            }
            else
            {
                send_invitation(ctx, sess, sess->control_socket,
                                sess->invite_host, sess->invite_port);
                sess->invite_retries++;
                sess->invite_sent_time = now;
            }
        }
    }
    else if (sess->state == RTPMIDI_STATE_INVITING_DATA)
    {
        if (now - sess->invite_sent_time >= INVITE_TIMEOUT_US)
        {
            if (sess->invite_retries >= INVITE_MAX_RETRIES)
            {
                sess->state = RTPMIDI_STATE_LISTENING;
            }
            else
            {
                send_invitation(ctx, sess, sess->data_socket,
                                sess->invite_host, sess->invite_port + 1);
                sess->invite_retries++;
                sess->invite_sent_time = now;
            }
        }
    }

    if (sess->state == RTPMIDI_STATE_CONNECTED &&
        now - sess->last_sync_sent >= SYNC_INTERVAL_US)
    {
        for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
        {
            if (sess->peers[i].active)
            {
                send_sync_ck0(ctx, sess, &sess->peers[i]);
            }
        }
        sess->last_sync_sent = now;
    }
}

RtpMidiContext *rtpmidi_create(UdpContext *udp_ctx)
{
    if (!udp_ctx)
        return NULL;

    RtpMidiContext *ctx = (RtpMidiContext *)calloc(1, sizeof(RtpMidiContext));
    if (!ctx)
        return NULL;

    ctx->udp = udp_ctx;

    srand((unsigned int)time(NULL));

    return ctx;
}

void rtpmidi_destroy(RtpMidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < RTPMIDI_MAX_SESSIONS; i++)
    {
        if (ctx->sessions[i].active)
        {
            rtpmidi_destroy_session(ctx, i);
        }
    }

    free(ctx);
}

int rtpmidi_create_session(RtpMidiContext *ctx, const char *name, int control_port)
{
    if (!ctx)
        return -1;

    if (control_port == 0)
        control_port = RTPMIDI_DEFAULT_PORT;

    int handle = -1;
    for (int i = 0; i < RTPMIDI_MAX_SESSIONS; i++)
    {
        if (!ctx->sessions[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free session slots");
        return -1;
    }

    RtpMidiSession *sess = &ctx->sessions[handle];
    memset(sess, 0, sizeof(*sess));

    sess->control_socket = udp_bind(ctx->udp, control_port);
    if (sess->control_socket < 0)
    {
        set_error(ctx, "Failed to bind control port");
        return -1;
    }

    sess->control_port = udp_get_port(ctx->udp, sess->control_socket);
    sess->data_socket = udp_bind(ctx->udp, sess->control_port + 1);
    if (sess->data_socket < 0)
    {
        udp_close(ctx->udp, sess->control_socket);
        set_error(ctx, "Failed to bind data port");
        return -1;
    }
    sess->data_port = sess->control_port + 1;

    if (name)
        strncpy(sess->name, name, RTPMIDI_MAX_NAME - 1);
    else
        strncpy(sess->name, "Budo", RTPMIDI_MAX_NAME - 1);

    sess->ssrc = generate_ssrc();
    sess->state = RTPMIDI_STATE_LISTENING;
    sess->active = true;

    return handle;
}

bool rtpmidi_connect(RtpMidiContext *ctx, int session, const char *host, int port)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return false;

    RtpMidiSession *sess = &ctx->sessions[session];
    if (!sess->active)
        return false;
    if (!host || port <= 0)
        return false;

    sess->invite_token = generate_token();
    strncpy(sess->invite_host, host, UDP_MAX_HOST - 1);
    sess->invite_port = port;
    sess->invite_retries = 0;
    sess->state = RTPMIDI_STATE_INVITING_CTRL;

    send_invitation(ctx, sess, sess->control_socket, host, port);
    sess->invite_sent_time = get_time_us();
    sess->invite_retries = 1;

    return true;
}

void rtpmidi_destroy_session(RtpMidiContext *ctx, int session)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return;

    RtpMidiSession *sess = &ctx->sessions[session];
    if (!sess->active)
        return;

    for (int i = 0; i < RTPMIDI_MAX_PEERS; i++)
    {
        if (sess->peers[i].active)
        {
            send_end(ctx, sess, &sess->peers[i]);
            sess->peers[i].active = false;
        }
    }

    if (sess->control_socket >= 0)
        udp_close(ctx->udp, sess->control_socket);
    if (sess->data_socket >= 0)
        udp_close(ctx->udp, sess->data_socket);

    sess->active = false;
    sess->state = RTPMIDI_STATE_CLOSED;
    sess->peer_count = 0;
}

void rtpmidi_set_callback(RtpMidiContext *ctx, int session,
                          RtpMidiCallback callback, void *user_data)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return;

    RtpMidiSession *sess = &ctx->sessions[session];
    sess->callback = callback;
    sess->user_data = user_data;
}

bool rtpmidi_send_message(RtpMidiContext *ctx, int session,
                          uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return false;

    RtpMidiSession *sess = &ctx->sessions[session];
    if (!sess->active || sess->state != RTPMIDI_STATE_CONNECTED)
        return false;

    uint8_t midi[3];
    int len;

    uint8_t type = status & 0xF0;
    if (type == 0xC0 || type == 0xD0)
    {
        midi[0] = status;
        midi[1] = data1;
        len = 2;
    }
    else
    {
        midi[0] = status;
        midi[1] = data1;
        midi[2] = data2;
        len = 3;
    }

    return send_rtp_midi(ctx, sess, midi, len);
}

bool rtpmidi_send_raw(RtpMidiContext *ctx, int session,
                      const uint8_t *data, size_t length)
{
    if (!ctx || session < 0 || session >= RTPMIDI_MAX_SESSIONS)
        return false;

    RtpMidiSession *sess = &ctx->sessions[session];
    if (!sess->active || sess->state != RTPMIDI_STATE_CONNECTED)
        return false;
    if (!data || length == 0 || length > 512)
        return false;

    return send_rtp_midi(ctx, sess, data, (int)length);
}

void rtpmidi_poll(RtpMidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < RTPMIDI_MAX_SESSIONS; i++)
    {
        poll_session(ctx, &ctx->sessions[i]);
    }
}

int rtpmidi_get_session_count(RtpMidiContext *ctx)
{
    if (!ctx)
        return 0;
    int count = 0;
    for (int i = 0; i < RTPMIDI_MAX_SESSIONS; i++)
    {
        if (ctx->sessions[i].active)
            count++;
    }
    return count;
}

bool rtpmidi_get_session_info(RtpMidiContext *ctx, int index, RtpMidiSessionInfo *info)
{
    if (!ctx || !info || index < 0)
        return false;

    int count = 0;
    for (int i = 0; i < RTPMIDI_MAX_SESSIONS; i++)
    {
        if (ctx->sessions[i].active)
        {
            if (count == index)
            {
                info->handle = i;
                strncpy(info->name, ctx->sessions[i].name, RTPMIDI_MAX_NAME);
                info->port = ctx->sessions[i].control_port;
                info->state = ctx->sessions[i].state;
                info->peer_count = ctx->sessions[i].peer_count;
                return true;
            }
            count++;
        }
    }

    return false;
}

const char *rtpmidi_get_error(RtpMidiContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}