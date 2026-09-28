#ifndef TESTS_UDP_MOCK_H
#define TESTS_UDP_MOCK_H

#include "network/udp_wrapper.h"

void udp_mock_reset(void);
bool udp_mock_enqueue(UdpContext *ctx, int handle, const char *host, int port,
                      const uint8_t *data, int length);
int udp_mock_live_count(void);
int udp_mock_destroy_count(void);
bool udp_mock_is_live(UdpContext *ctx);
int udp_mock_send_count(UdpContext *ctx);
bool udp_mock_last_send_equals(UdpContext *ctx, const char *host, int port,
                               const uint8_t *data, int length);

#endif