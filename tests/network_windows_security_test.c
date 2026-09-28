#include "network/network_wrapper.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    NetworkPolicy policy;
    network_policy_parse(&policy, "example.com");
    NetworkContext *context = network_create(&policy);
    assert(context);

    const char *bad_methods[] = {"", "GE T", "GET\r\nX-Evil: yes"};
    for (size_t i = 0; i < sizeof(bad_methods) / sizeof(bad_methods[0]); i++)
        assert(network_request(context, bad_methods[i], "https://example.com/",
                               NULL, 0, NULL, 0) == NULL);

    NetworkHeader bad_headers[] = {
        {"Bad Name", "value"},
        {"X-Test", "safe\r\nX-Evil: yes"},
        {"Host", "attacker.invalid"},
        {"Content-Length", "999"},
        {"Transfer-Encoding", "chunked"},
        {"Connection", "keep-alive"}};
    for (size_t i = 0; i < sizeof(bad_headers) / sizeof(bad_headers[0]); i++)
        assert(network_request(context, "GET", "https://example.com/",
                               &bad_headers[i], 1, NULL, 0) == NULL);

    assert(network_request(context, "GET", "https://blocked.invalid/",
                           NULL, 0, NULL, 0) == NULL);
    assert(strstr(network_get_error(context), "allowed domain list"));
    network_destroy(context);
    puts("Windows network security contract tests passed");
    return 0;
}