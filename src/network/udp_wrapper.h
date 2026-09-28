#ifndef UDP_WRAPPER_H
#define UDP_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UDP_MAX_SOCKETS 32
#define UDP_MAX_PACKET 65507 
#define UDP_MAX_HOST 256
#define UDP_RECV_TRUNCATED (-2)

    typedef struct UdpContext UdpContext;

    typedef struct
    {
        char host[UDP_MAX_HOST]; 
        int port;                
        uint8_t *data;           
        int length;              
    } UdpDatagram;

    UdpContext *udp_create(void);

    void udp_destroy(UdpContext *ctx);

    int udp_bind(UdpContext *ctx, int port);

    int udp_get_port(UdpContext *ctx, int handle);

    bool udp_send(UdpContext *ctx, int handle, const char *host, int port,
                  const uint8_t *data, int length);

    int udp_recv(UdpContext *ctx, int handle, uint8_t *buf, int buf_len,
                 UdpDatagram *dgram);

    void udp_close(UdpContext *ctx, int handle);

    const char *udp_get_error(UdpContext *ctx);

#ifdef __cplusplus
}
#endif

#endif