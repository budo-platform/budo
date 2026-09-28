#ifndef TESTS_NETWORK_MOCK_H
#define TESTS_NETWORK_MOCK_H

#include "network/network_wrapper.h"

void network_mock_reset(void);
int network_mock_find_request(NetworkContext *ctx, const char *url);
bool network_mock_complete(int handle, int status, const char *body);
int network_mock_live_count(void);
int network_mock_destroy_count(void);
int network_mock_discarded_completion_count(void);

#endif