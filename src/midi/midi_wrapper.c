#include "midi_wrapper.h"
#include "midi_topology.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__) && !defined(__ANDROID__)
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#define MIDI_PLATFORM_COREMIDI 1

#elif defined(__ANDROID__)
#define MIDI_PLATFORM_ANDROID 1

#elif defined(__linux__)
#include <alsa/asoundlib.h>
#define MIDI_PLATFORM_ALSA 1

#elif defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#define MIDI_PLATFORM_WINMM 1

#else
#define MIDI_PLATFORM_STUB 1
#endif

typedef struct
{
    bool active;
    int device_index;
    MidiCallback callback;
    void *user_data;
    MidiSysExCallback sysex_callback;
    void *sysex_user_data;

#if MIDI_PLATFORM_COREMIDI
    MIDIEndpointRef endpoint;
    MIDIPortRef port;
#elif MIDI_PLATFORM_ALSA
    snd_rawmidi_t *rawmidi;
    int thread_running;
    pthread_t thread;
#elif MIDI_PLATFORM_WINMM
    HMIDIIN handle;
#elif MIDI_PLATFORM_ANDROID
    int android_handle;
#endif
} MidiInputDevice;

typedef struct
{
    bool active;
    int device_index;

#if MIDI_PLATFORM_COREMIDI
    MIDIEndpointRef endpoint;
    MIDIPortRef port;
#elif MIDI_PLATFORM_ALSA
    snd_rawmidi_t *rawmidi;
#elif MIDI_PLATFORM_WINMM
    HMIDIOUT handle;
#elif MIDI_PLATFORM_ANDROID
    int android_handle;
#endif
} MidiOutputDevice;

struct MidiContext
{
    MidiInputDevice inputs[MIDI_MAX_DEVICES];
    MidiOutputDevice outputs[MIDI_MAX_DEVICES];
    int input_handle_count;
    int output_handle_count;

    char error_msg[256];

#if MIDI_PLATFORM_COREMIDI
    MIDIClientRef client;
#endif
};

static void set_error(MidiContext *ctx, const char *msg)
{
    if (ctx && msg)
    {
        strncpy(ctx->error_msg, msg, sizeof(ctx->error_msg) - 1);
        ctx->error_msg[sizeof(ctx->error_msg) - 1] = '\0';
    }
}

static uint64_t midi_now_us(void)
{
#if MIDI_PLATFORM_COREMIDI
    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0)
        mach_timebase_info(&timebase);
    uint64_t ticks = mach_absolute_time();
    return (ticks * timebase.numer) / timebase.denom / 1000;
#elif MIDI_PLATFORM_WINMM
    return (uint64_t)GetTickCount64() * 1000;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
    return 0;
#endif
}

#if MIDI_PLATFORM_COREMIDI
static uint64_t coremidi_timestamp_us(MIDITimeStamp timestamp)
{
    if (timestamp == 0)
        return midi_now_us();

    static mach_timebase_info_data_t timebase;
    if (timebase.denom == 0)
        mach_timebase_info(&timebase);
    return ((uint64_t)timestamp * timebase.numer) / timebase.denom / 1000;
}
#endif

#if MIDI_PLATFORM_COREMIDI

static void coremidi_read_proc(const MIDIPacketList *pktlist, void *readProcRefCon, void *srcConnRefCon)
{
    MidiInputDevice *dev = (MidiInputDevice *)readProcRefCon;
    if (!dev || !dev->active || !dev->callback)
        return;

    const MIDIPacket *packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++)
    {
        for (UInt16 j = 0; j < packet->length;)
        {
            MidiMessage msg = {0};
            uint8_t status = packet->data[j];

            if (status >= 0xF8)
            {
                j++;
                continue;
            }

            msg.status = status;
            msg.timestamp = coremidi_timestamp_us(packet->timeStamp);

            int msg_len = 0;
            if (status >= 0xC0 && status < 0xE0)
            {
                msg_len = 2; 
            }
            else if (status >= 0x80 && status < 0xF0)
            {
                msg_len = 3; 
            }
            else if (status == 0xF0)
            {
                
                UInt16 start = j;
                while (j < packet->length && packet->data[j] != 0xF7)
                    j++;
                if (j < packet->length)
                    j++; 

                if (dev->sysex_callback)
                {
                    size_t sysex_len = j - start;
                    dev->sysex_callback(dev->device_index, &packet->data[start], sysex_len, dev->sysex_user_data);
                }
                continue;
            }
            else
            {
                j++;
                continue;
            }

            if (j + msg_len <= packet->length)
            {
                if (msg_len >= 2)
                    msg.data1 = packet->data[j + 1];
                if (msg_len >= 3)
                    msg.data2 = packet->data[j + 2];

                dev->callback(dev->device_index, &msg, dev->user_data);
            }
            j += msg_len;
        }
        packet = MIDIPacketNext(packet);
    }
}

MidiContext *midi_create(void)
{
    MidiContext *ctx = (MidiContext *)calloc(1, sizeof(MidiContext));
    if (!ctx)
        return NULL;

    CFStringRef name = CFSTR("Budo");
    OSStatus status = MIDIClientCreate(name, NULL, NULL, &ctx->client);
    if (status != noErr)
    {
        set_error(ctx, "Failed to create MIDI client");
        free(ctx);
        return NULL;
    }

    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->inputs[i].active)
        {
            midi_close_input(ctx, i);
        }
    }

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->outputs[i].active)
        {
            midi_close_output(ctx, i);
        }
    }

    MIDIClientDispose(ctx->client);
    free(ctx);
}

int midi_get_input_count(MidiContext *ctx)
{
    (void)ctx;
    return (int)MIDIGetNumberOfSources();
}

int midi_get_output_count(MidiContext *ctx)
{
    (void)ctx;
    return (int)MIDIGetNumberOfDestinations();
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_input_count(ctx))
        return false;

    MIDIEndpointRef endpoint = MIDIGetSource(index);
    CFStringRef name = NULL;
    MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name);

    info->id = index;
    info->is_input = true;
    info->is_output = false;

    if (name)
    {
        CFStringGetCString(name, info->name, MIDI_MAX_DEVICE_NAME, kCFStringEncodingUTF8);
        CFRelease(name);
    }
    else
    {
        snprintf(info->name, MIDI_MAX_DEVICE_NAME, "MIDI Input %d", index);
    }

    return true;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_output_count(ctx))
        return false;

    MIDIEndpointRef endpoint = MIDIGetDestination(index);
    CFStringRef name = NULL;
    MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name);

    info->id = index;
    info->is_input = false;
    info->is_output = true;

    if (name)
    {
        CFStringGetCString(name, info->name, MIDI_MAX_DEVICE_NAME, kCFStringEncodingUTF8);
        CFRelease(name);
    }
    else
    {
        snprintf(info->name, MIDI_MAX_DEVICE_NAME, "MIDI Output %d", index);
    }

    return true;
}

void midi_refresh_devices(MidiContext *ctx)
{
    
    (void)ctx;
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    if (!ctx || index < 0 || index >= midi_get_input_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->inputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free input slots");
        return -1;
    }

    MidiInputDevice *dev = &ctx->inputs[handle];
    dev->device_index = index;
    dev->callback = callback;
    dev->user_data = user_data;
    dev->endpoint = MIDIGetSource(index);

    CFStringRef portName = CFStringCreateWithFormat(NULL, NULL, CFSTR("Input%d"), handle);
    OSStatus status = MIDIInputPortCreate(ctx->client, portName, coremidi_read_proc, dev, &dev->port);
    CFRelease(portName);

    if (status != noErr)
    {
        set_error(ctx, "Failed to create input port");
        return -1;
    }

    status = MIDIPortConnectSource(dev->port, dev->endpoint, NULL);
    if (status != noErr)
    {
        MIDIPortDispose(dev->port);
        set_error(ctx, "Failed to connect to MIDI source");
        return -1;
    }

    dev->active = true;
    return handle;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiInputDevice *dev = &ctx->inputs[handle];
    if (!dev->active)
        return;

    MIDIPortDisconnectSource(dev->port, dev->endpoint);
    MIDIPortDispose(dev->port);
    dev->active = false;
}

int midi_open_output(MidiContext *ctx, int index)
{
    if (!ctx || index < 0 || index >= midi_get_output_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->outputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free output slots");
        return -1;
    }

    MidiOutputDevice *dev = &ctx->outputs[handle];
    dev->device_index = index;
    dev->endpoint = MIDIGetDestination(index);

    CFStringRef portName = CFStringCreateWithFormat(NULL, NULL, CFSTR("Output%d"), handle);
    OSStatus status = MIDIOutputPortCreate(ctx->client, portName, &dev->port);
    CFRelease(portName);

    if (status != noErr)
    {
        set_error(ctx, "Failed to create output port");
        return -1;
    }

    dev->active = true;
    return handle;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return;

    MIDIPortDispose(dev->port);
    dev->active = false;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    Byte buffer[256];
    MIDIPacketList *pktlist = (MIDIPacketList *)buffer;
    MIDIPacket *packet = MIDIPacketListInit(pktlist);

    Byte msg[3];
    int len = 3;

    if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
    {
        len = 2;
        msg[0] = status;
        msg[1] = data1;
    }
    else
    {
        msg[0] = status;
        msg[1] = data1;
        msg[2] = data2;
    }

    packet = MIDIPacketListAdd(pktlist, sizeof(buffer), packet, 0, len, msg);
    if (!packet)
    {
        set_error(ctx, "Failed to create MIDI packet");
        return false;
    }

    OSStatus err = MIDISend(dev->port, dev->endpoint, pktlist);
    return err == noErr;
}

bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES || !data || length == 0)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    Byte buffer[1024];
    MIDIPacketList *pktlist = (MIDIPacketList *)buffer;
    MIDIPacket *packet = MIDIPacketListInit(pktlist);

    packet = MIDIPacketListAdd(pktlist, sizeof(buffer), packet, 0, length, data);
    if (!packet)
    {
        set_error(ctx, "Failed to create MIDI packet");
        return false;
    }

    OSStatus err = MIDISend(dev->port, dev->endpoint, pktlist);
    return err == noErr;
}

#elif MIDI_PLATFORM_ALSA

#include <pthread.h>

static void *alsa_input_thread(void *arg)
{
    MidiInputDevice *dev = (MidiInputDevice *)arg;
    unsigned char buffer[3];

    while (dev->thread_running)
    {
        int err = snd_rawmidi_read(dev->rawmidi, buffer, sizeof(buffer));
        if (err > 0 && dev->callback)
        {
            MidiMessage msg = {0};
            msg.status = buffer[0];
            if (err >= 2)
                msg.data1 = buffer[1];
            if (err >= 3)
                msg.data2 = buffer[2];
            msg.timestamp = midi_now_us();

            dev->callback(dev->device_index, &msg, dev->user_data);
        }
        else if (err == -EAGAIN)
        {
            usleep(1000); 
        }
    }
    return NULL;
}

MidiContext *midi_create(void)
{
    MidiContext *ctx = (MidiContext *)calloc(1, sizeof(MidiContext));
    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->inputs[i].active)
            midi_close_input(ctx, i);
        if (ctx->outputs[i].active)
            midi_close_output(ctx, i);
    }

    free(ctx);
}

int midi_get_input_count(MidiContext *ctx)
{
    (void)ctx;
    int count = 0;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        snd_ctl_t *ctl;
        char hw[32];
        snprintf(hw, sizeof(hw), "hw:%d", card);

        if (snd_ctl_open(&ctl, hw, 0) >= 0)
        {
            int device = -1;
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
            {
                snd_rawmidi_info_t *info;
                snd_rawmidi_info_alloca(&info);
                snd_rawmidi_info_set_device(info, device);
                snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);

                if (snd_ctl_rawmidi_info(ctl, info) >= 0)
                {
                    count += snd_rawmidi_info_get_subdevices_count(info);
                }
            }
            snd_ctl_close(ctl);
        }
    }

    return count;
}

int midi_get_output_count(MidiContext *ctx)
{
    (void)ctx;
    int count = 0;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        snd_ctl_t *ctl;
        char hw[32];
        snprintf(hw, sizeof(hw), "hw:%d", card);

        if (snd_ctl_open(&ctl, hw, 0) >= 0)
        {
            int device = -1;
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
            {
                snd_rawmidi_info_t *info;
                snd_rawmidi_info_alloca(&info);
                snd_rawmidi_info_set_device(info, device);
                snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);

                if (snd_ctl_rawmidi_info(ctl, info) >= 0)
                {
                    count += snd_rawmidi_info_get_subdevices_count(info);
                }
            }
            snd_ctl_close(ctl);
        }
    }

    return count;
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0)
        return false;

    int count = 0;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        snd_ctl_t *ctl;
        char hw[32];
        snprintf(hw, sizeof(hw), "hw:%d", card);

        if (snd_ctl_open(&ctl, hw, 0) >= 0)
        {
            int device = -1;
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
            {
                snd_rawmidi_info_t *rawinfo;
                snd_rawmidi_info_alloca(&rawinfo);
                snd_rawmidi_info_set_device(rawinfo, device);
                snd_rawmidi_info_set_stream(rawinfo, SND_RAWMIDI_STREAM_INPUT);

                if (snd_ctl_rawmidi_info(ctl, rawinfo) >= 0)
                {
                    int subs = snd_rawmidi_info_get_subdevices_count(rawinfo);
                    for (int s = 0; s < subs; s++)
                    {
                        if (count == index)
                        {
                            snd_rawmidi_info_set_subdevice(rawinfo, s);
                            snd_ctl_rawmidi_info(ctl, rawinfo);

                            info->id = index;
                            info->is_input = true;
                            info->is_output = false;
                            snprintf(info->name, MIDI_MAX_DEVICE_NAME, "%s",
                                     snd_rawmidi_info_get_name(rawinfo));

                            snd_ctl_close(ctl);
                            return true;
                        }
                        count++;
                    }
                }
            }
            snd_ctl_close(ctl);
        }
    }

    return false;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0)
        return false;

    int count = 0;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        snd_ctl_t *ctl;
        char hw[32];
        snprintf(hw, sizeof(hw), "hw:%d", card);

        if (snd_ctl_open(&ctl, hw, 0) >= 0)
        {
            int device = -1;
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
            {
                snd_rawmidi_info_t *rawinfo;
                snd_rawmidi_info_alloca(&rawinfo);
                snd_rawmidi_info_set_device(rawinfo, device);
                snd_rawmidi_info_set_stream(rawinfo, SND_RAWMIDI_STREAM_OUTPUT);

                if (snd_ctl_rawmidi_info(ctl, rawinfo) >= 0)
                {
                    int subs = snd_rawmidi_info_get_subdevices_count(rawinfo);
                    for (int s = 0; s < subs; s++)
                    {
                        if (count == index)
                        {
                            snd_rawmidi_info_set_subdevice(rawinfo, s);
                            snd_ctl_rawmidi_info(ctl, rawinfo);

                            info->id = index;
                            info->is_input = false;
                            info->is_output = true;
                            snprintf(info->name, MIDI_MAX_DEVICE_NAME, "%s",
                                     snd_rawmidi_info_get_name(rawinfo));

                            snd_ctl_close(ctl);
                            return true;
                        }
                        count++;
                    }
                }
            }
            snd_ctl_close(ctl);
        }
    }

    return false;
}

void midi_refresh_devices(MidiContext *ctx)
{
    (void)ctx;
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

static bool get_alsa_device_name(int index, bool is_input, char *device_name, size_t size)
{
    int count = 0;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0)
    {
        snd_ctl_t *ctl;
        char hw[32];
        snprintf(hw, sizeof(hw), "hw:%d", card);

        if (snd_ctl_open(&ctl, hw, 0) >= 0)
        {
            int device = -1;
            while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0)
            {
                snd_rawmidi_info_t *info;
                snd_rawmidi_info_alloca(&info);
                snd_rawmidi_info_set_device(info, device);
                snd_rawmidi_info_set_stream(info, is_input ? SND_RAWMIDI_STREAM_INPUT : SND_RAWMIDI_STREAM_OUTPUT);

                if (snd_ctl_rawmidi_info(ctl, info) >= 0)
                {
                    int subs = snd_rawmidi_info_get_subdevices_count(info);
                    for (int s = 0; s < subs; s++)
                    {
                        if (count == index)
                        {
                            snprintf(device_name, size, "hw:%d,%d,%d", card, device, s);
                            snd_ctl_close(ctl);
                            return true;
                        }
                        count++;
                    }
                }
            }
            snd_ctl_close(ctl);
        }
    }

    return false;
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    if (!ctx || index < 0)
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->inputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
        return -1;

    char device_name[64];
    if (!get_alsa_device_name(index, true, device_name, sizeof(device_name)))
    {
        set_error(ctx, "Device not found");
        return -1;
    }

    MidiInputDevice *dev = &ctx->inputs[handle];
    int err = snd_rawmidi_open(&dev->rawmidi, NULL, device_name, SND_RAWMIDI_NONBLOCK);
    if (err < 0)
    {
        set_error(ctx, snd_strerror(err));
        return -1;
    }

    dev->device_index = index;
    dev->callback = callback;
    dev->user_data = user_data;
    dev->thread_running = 1;
    dev->active = true;

    if (pthread_create(&dev->thread, NULL, alsa_input_thread, dev) != 0)
    {
        snd_rawmidi_close(dev->rawmidi);
        dev->active = false;
        set_error(ctx, "Failed to create input thread");
        return -1;
    }

    return handle;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiInputDevice *dev = &ctx->inputs[handle];
    if (!dev->active)
        return;

    dev->thread_running = 0;
    pthread_join(dev->thread, NULL);
    snd_rawmidi_close(dev->rawmidi);
    dev->active = false;
}

int midi_open_output(MidiContext *ctx, int index)
{
    if (!ctx || index < 0)
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->outputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
        return -1;

    char device_name[64];
    if (!get_alsa_device_name(index, false, device_name, sizeof(device_name)))
    {
        set_error(ctx, "Device not found");
        return -1;
    }

    MidiOutputDevice *dev = &ctx->outputs[handle];
    int err = snd_rawmidi_open(NULL, &dev->rawmidi, device_name, 0);
    if (err < 0)
    {
        set_error(ctx, snd_strerror(err));
        return -1;
    }

    dev->device_index = index;
    dev->active = true;
    return handle;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return;

    snd_rawmidi_close(dev->rawmidi);
    dev->active = false;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    unsigned char msg[3];
    int len = 3;

    if ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0)
    {
        len = 2;
        msg[0] = status;
        msg[1] = data1;
    }
    else
    {
        msg[0] = status;
        msg[1] = data1;
        msg[2] = data2;
    }

    return snd_rawmidi_write(dev->rawmidi, msg, len) == len;
}

bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES || !data || length == 0)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    return snd_rawmidi_write(dev->rawmidi, data, length) == (ssize_t)length;
}

#elif MIDI_PLATFORM_WINMM

static void CALLBACK winmm_midi_in_callback(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance,
                                            DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    MidiInputDevice *dev = (MidiInputDevice *)dwInstance;
    if (!dev || !dev->active || !dev->callback)
        return;

    if (wMsg == MIM_DATA)
    {
        MidiMessage msg = {0};
        msg.status = (uint8_t)(dwParam1 & 0xFF);
        msg.data1 = (uint8_t)((dwParam1 >> 8) & 0xFF);
        msg.data2 = (uint8_t)((dwParam1 >> 16) & 0xFF);
        msg.timestamp = (uint64_t)dwParam2 * 1000;

        dev->callback(dev->device_index, &msg, dev->user_data);
    }
}

MidiContext *midi_create(void)
{
    return (MidiContext *)calloc(1, sizeof(MidiContext));
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->inputs[i].active)
            midi_close_input(ctx, i);
        if (ctx->outputs[i].active)
            midi_close_output(ctx, i);
    }

    free(ctx);
}

int midi_get_input_count(MidiContext *ctx)
{
    (void)ctx;
    return midiInGetNumDevs();
}

int midi_get_output_count(MidiContext *ctx)
{
    (void)ctx;
    return midiOutGetNumDevs();
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_input_count(ctx))
        return false;

    MIDIINCAPS caps;
    if (midiInGetDevCaps(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return false;

    info->id = index;
    info->is_input = true;
    info->is_output = false;
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, info->name, MIDI_MAX_DEVICE_NAME, NULL, NULL);

    return true;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_output_count(ctx))
        return false;

    MIDIOUTCAPS caps;
    if (midiOutGetDevCaps(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return false;

    info->id = index;
    info->is_input = false;
    info->is_output = true;
    WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, info->name, MIDI_MAX_DEVICE_NAME, NULL, NULL);

    return true;
}

void midi_refresh_devices(MidiContext *ctx)
{
    (void)ctx;
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    if (!ctx || index < 0 || index >= midi_get_input_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->inputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
        return -1;

    MidiInputDevice *dev = &ctx->inputs[handle];
    dev->device_index = index;
    dev->callback = callback;
    dev->user_data = user_data;

    MMRESULT result = midiInOpen(&dev->handle, index, (DWORD_PTR)winmm_midi_in_callback,
                                 (DWORD_PTR)dev, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        set_error(ctx, "Failed to open MIDI input");
        return -1;
    }

    midiInStart(dev->handle);
    dev->active = true;
    return handle;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiInputDevice *dev = &ctx->inputs[handle];
    if (!dev->active)
        return;

    midiInStop(dev->handle);
    midiInClose(dev->handle);
    dev->active = false;
}

int midi_open_output(MidiContext *ctx, int index)
{
    if (!ctx || index < 0 || index >= midi_get_output_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->outputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
        return -1;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    dev->device_index = index;

    MMRESULT result = midiOutOpen(&dev->handle, index, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR)
    {
        set_error(ctx, "Failed to open MIDI output");
        return -1;
    }

    dev->active = true;
    return handle;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return;

    midiOutClose(dev->handle);
    dev->active = false;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    DWORD msg = status | (data1 << 8) | (data2 << 16);
    return midiOutShortMsg(dev->handle, msg) == MMSYSERR_NOERROR;
}

bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES || !data || length == 0)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    MIDIHDR hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.lpData = (LPSTR)data;
    hdr.dwBufferLength = (DWORD)length;

    if (midiOutPrepareHeader(dev->handle, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR)
        return false;

    bool success = midiOutLongMsg(dev->handle, &hdr, sizeof(hdr)) == MMSYSERR_NOERROR;
    midiOutUnprepareHeader(dev->handle, &hdr, sizeof(hdr));

    return success;
}

#elif MIDI_PLATFORM_ANDROID

extern bool android_midi_is_available(void);
extern int android_midi_get_input_count(void);
extern int android_midi_get_output_count(void);
extern bool android_midi_get_input_name(int index, char *name, int max_len);
extern bool android_midi_get_output_name(int index, char *name, int max_len);
extern uint64_t android_midi_get_device_generation(void);
extern int android_midi_open_input(int index, void (*callback)(int, uint8_t, uint8_t, uint8_t, uint64_t, void *), void *user_data);
extern void android_midi_close_input(int handle);
extern int android_midi_open_output(int index);
extern void android_midi_close_output(int handle);
extern bool android_midi_send_message(int handle, uint8_t status, uint8_t data1, uint8_t data2);
extern bool android_midi_send_raw(int handle, const uint8_t *data, int length);
extern bool android_midi_set_sysex_callback(int handle, void (*callback)(int, const uint8_t *, int, void *), void *user_data);

static void android_midi_callback_adapter(int device_id, uint8_t status, uint8_t data1, uint8_t data2, uint64_t timestamp, void *user_data)
{
    MidiInputDevice *dev = (MidiInputDevice *)user_data;
    if (dev && dev->active && dev->callback)
    {
        MidiMessage msg = {0};
        msg.status = status;
        msg.data1 = data1;
        msg.data2 = data2;
        msg.timestamp = timestamp > 0 ? timestamp : midi_now_us();
        dev->callback(dev->device_index, &msg, dev->user_data);
    }
}

MidiContext *midi_create(void)
{
    MidiContext *ctx = (MidiContext *)calloc(1, sizeof(MidiContext));
    if (!ctx)
        return NULL;

    if (!android_midi_is_available())
    {
        set_error(ctx, "Android MIDI backend is not initialized");
    }

    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->inputs[i].active)
        {
            midi_close_input(ctx, i);
        }
    }

    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (ctx->outputs[i].active)
        {
            midi_close_output(ctx, i);
        }
    }

    free(ctx);
}

int midi_get_input_count(MidiContext *ctx)
{
    (void)ctx;
    return android_midi_get_input_count();
}

int midi_get_output_count(MidiContext *ctx)
{
    (void)ctx;
    return android_midi_get_output_count();
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_input_count(ctx))
        return false;

    info->id = index;
    info->is_input = true;
    info->is_output = false;

    if (!android_midi_get_input_name(index, info->name, MIDI_MAX_DEVICE_NAME))
    {
        snprintf(info->name, MIDI_MAX_DEVICE_NAME, "MIDI Input %d", index);
    }

    return true;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    if (!ctx || !info || index < 0 || index >= midi_get_output_count(ctx))
        return false;

    info->id = index;
    info->is_input = false;
    info->is_output = true;

    if (!android_midi_get_output_name(index, info->name, MIDI_MAX_DEVICE_NAME))
    {
        snprintf(info->name, MIDI_MAX_DEVICE_NAME, "MIDI Output %d", index);
    }

    return true;
}

void midi_refresh_devices(MidiContext *ctx)
{
    (void)ctx;
    
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return android_midi_get_device_generation();
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    if (!ctx || index < 0 || index >= midi_get_input_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->inputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free input slots");
        return -1;
    }

    MidiInputDevice *dev = &ctx->inputs[handle];
    dev->device_index = index;
    dev->callback = callback;
    dev->user_data = user_data;

    int android_handle = android_midi_open_input(index, android_midi_callback_adapter, dev);
    if (android_handle < 0)
    {
        set_error(ctx, "Failed to open Android MIDI input");
        return -1;
    }

    dev->android_handle = android_handle;
    dev->active = true;

    return handle;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiInputDevice *dev = &ctx->inputs[handle];
    if (!dev->active)
        return;

    android_midi_close_input(dev->android_handle);
    dev->active = false;
}

int midi_open_output(MidiContext *ctx, int index)
{
    if (!ctx || index < 0 || index >= midi_get_output_count(ctx))
        return -1;

    int handle = -1;
    for (int i = 0; i < MIDI_MAX_DEVICES; i++)
    {
        if (!ctx->outputs[i].active)
        {
            handle = i;
            break;
        }
    }
    if (handle < 0)
    {
        set_error(ctx, "No free output slots");
        return -1;
    }

    MidiOutputDevice *dev = &ctx->outputs[handle];
    dev->device_index = index;

    int android_handle = android_midi_open_output(index);
    if (android_handle < 0)
    {
        set_error(ctx, "Failed to open Android MIDI output");
        return -1;
    }

    dev->android_handle = android_handle;
    dev->active = true;

    return handle;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return;

    android_midi_close_output(dev->android_handle);
    dev->active = false;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    return android_midi_send_message(dev->android_handle, status, data1, data2);
}

bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES || !data || length == 0)
        return false;

    MidiOutputDevice *dev = &ctx->outputs[handle];
    if (!dev->active)
        return false;

    return android_midi_send_raw(dev->android_handle, data, (int)length);
}

static void android_sysex_callback_adapter(int device_id, const uint8_t *data, int length, void *user_data)
{
    MidiInputDevice *dev = (MidiInputDevice *)user_data;
    if (dev && dev->active && dev->sysex_callback)
    {
        dev->sysex_callback(dev->device_index, data, (size_t)length, dev->sysex_user_data);
    }
}

#else

MidiContext *midi_create(void)
{
    MidiContext *ctx = (MidiContext *)calloc(1, sizeof(MidiContext));
    if (ctx)
        set_error(ctx, "MIDI not supported on this platform");
    return ctx;
}

void midi_destroy(MidiContext *ctx)
{
    free(ctx);
}

int midi_get_input_count(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

int midi_get_output_count(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

bool midi_get_input_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    (void)ctx;
    (void)index;
    (void)info;
    return false;
}

bool midi_get_output_info(MidiContext *ctx, int index, MidiDeviceInfo *info)
{
    (void)ctx;
    (void)index;
    (void)info;
    return false;
}

void midi_refresh_devices(MidiContext *ctx)
{
    (void)ctx;
}

uint64_t midi_get_device_generation(MidiContext *ctx)
{
    (void)ctx;
    return 0;
}

int midi_open_input(MidiContext *ctx, int index, MidiCallback callback, void *user_data)
{
    (void)ctx;
    (void)index;
    (void)callback;
    (void)user_data;
    return -1;
}

void midi_close_input(MidiContext *ctx, int handle)
{
    (void)ctx;
    (void)handle;
}

int midi_open_output(MidiContext *ctx, int index)
{
    (void)ctx;
    (void)index;
    return -1;
}

void midi_close_output(MidiContext *ctx, int handle)
{
    (void)ctx;
    (void)handle;
}

bool midi_send_message(MidiContext *ctx, int handle, uint8_t status, uint8_t data1, uint8_t data2)
{
    (void)ctx;
    (void)handle;
    (void)status;
    (void)data1;
    (void)data2;
    return false;
}

bool midi_send_raw(MidiContext *ctx, int handle, const uint8_t *data, size_t length)
{
    (void)ctx;
    (void)handle;
    (void)data;
    (void)length;
    return false;
}

#endif

uint64_t midi_get_device_topology_fingerprint(MidiContext *ctx)
{
    if (!ctx)
        return 0;
    int input_count = midi_get_input_count(ctx);
    int output_count = midi_get_output_count(ctx);
    if (input_count < 0)
        input_count = 0;
    if (output_count < 0)
        output_count = 0;
    if (input_count > MIDI_MAX_DEVICES)
        input_count = MIDI_MAX_DEVICES;
    if (output_count > MIDI_MAX_DEVICES)
        output_count = MIDI_MAX_DEVICES;
    MidiDeviceInfo inputs[MIDI_MAX_DEVICES];
    MidiDeviceInfo outputs[MIDI_MAX_DEVICES];
    size_t valid_inputs = 0;
    size_t valid_outputs = 0;
    for (int i = 0; i < input_count; i++)
    {
        if (midi_get_input_info(ctx, i, &inputs[valid_inputs]))
            valid_inputs++;
    }
    for (int i = 0; i < output_count; i++)
    {
        if (midi_get_output_info(ctx, i, &outputs[valid_outputs]))
            valid_outputs++;
    }
    return midi_topology_fingerprint(inputs, valid_inputs, outputs, valid_outputs);
}

bool midi_note_on(MidiContext *ctx, int handle, int channel, int note, int velocity)
{
    return midi_send_message(ctx, handle, MIDI_NOTE_ON | (channel & 0x0F),
                             note & 0x7F, velocity & 0x7F);
}

bool midi_note_off(MidiContext *ctx, int handle, int channel, int note, int velocity)
{
    return midi_send_message(ctx, handle, MIDI_NOTE_OFF | (channel & 0x0F),
                             note & 0x7F, velocity & 0x7F);
}

bool midi_control_change(MidiContext *ctx, int handle, int channel, int control, int value)
{
    return midi_send_message(ctx, handle, MIDI_CONTROL_CHANGE | (channel & 0x0F),
                             control & 0x7F, value & 0x7F);
}

bool midi_program_change(MidiContext *ctx, int handle, int channel, int program)
{
    return midi_send_message(ctx, handle, MIDI_PROGRAM_CHANGE | (channel & 0x0F),
                             program & 0x7F, 0);
}

bool midi_pitch_bend(MidiContext *ctx, int handle, int channel, int value)
{
    int bend = value + 8192;
    if (bend < 0)
        bend = 0;
    if (bend > 16383)
        bend = 16383;
    return midi_send_message(ctx, handle, MIDI_PITCH_BEND | (channel & 0x0F),
                             bend & 0x7F, (bend >> 7) & 0x7F);
}

bool midi_channel_pressure(MidiContext *ctx, int handle, int channel, int pressure)
{
    return midi_send_message(ctx, handle, MIDI_CHANNEL_PRESSURE | (channel & 0x0F),
                             pressure & 0x7F, 0);
}

bool midi_poly_pressure(MidiContext *ctx, int handle, int channel, int note, int pressure)
{
    return midi_send_message(ctx, handle, MIDI_POLY_PRESSURE | (channel & 0x0F),
                             note & 0x7F, pressure & 0x7F);
}

bool midi_set_sysex_callback(MidiContext *ctx, int handle, MidiSysExCallback callback, void *user_data)
{
    if (!ctx || handle < 0 || handle >= MIDI_MAX_DEVICES)
        return false;

    MidiInputDevice *dev = &ctx->inputs[handle];
    if (!dev->active)
        return false;

    dev->sysex_callback = callback;
    dev->sysex_user_data = user_data;

#if MIDI_PLATFORM_ANDROID
    android_midi_set_sysex_callback(dev->android_handle, android_sysex_callback_adapter, dev);
#endif

    return true;
}

MidiMessageType midi_get_type(uint8_t status)
{
    if (status >= 0xF0)
        return MIDI_SYSTEM;
    return (MidiMessageType)(status & 0xF0);
}

int midi_get_channel(uint8_t status)
{
    if (status >= 0xF0)
        return -1; 
    return status & 0x0F;
}

const char *midi_get_error(MidiContext *ctx)
{
    return ctx ? ctx->error_msg : "Invalid context";
}