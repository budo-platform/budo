#include "network/network_request_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool has_header(const NetworkRequestPlan *plan, const char *name)
{
    for (int i = 0; i < plan->header_count; i++)
        if (strcmp(plan->headers[i].name, name) == 0)
            return true;
    return false;
}

int main(void)
{
    NetworkHeader headers[] = {
        {"Authorization", "secret"},
        {"Cookie", "secret"},
        {"Proxy-Authorization", "secret"},
        {"Content-Type", "text/plain"},
        {"Content-Language", "en"},
        {"Expect", "100-continue"},
        {"Trailer", "X-Checksum"},
        {"X-Keep", "yes"}};
    const uint8_t body[] = "body";
    NetworkRequestPlan plan;
    char error[128];

    assert(network_request_validate("GET", 16, "https://example.com/",
                                    NULL, 0, NULL, 0,
                                    error, sizeof(error)));
    assert(!network_request_validate("GE T", 16, "https://example.com/",
                                     NULL, 0, NULL, 0,
                                     error, sizeof(error)));
    assert(strcmp(error, "Invalid HTTP method") == 0);
    assert(!network_request_validate("GET", 16,
                                     "https://example.com/bad path",
                                     NULL, 0, NULL, 0,
                                     error, sizeof(error)));
    assert(strstr(error, "whitespace") != NULL);
    NetworkHeader managed = {"Host", "attacker.invalid"};
    assert(!network_request_validate("GET", 16, "https://example.com/",
                                     &managed, 1, NULL, 0,
                                     error, sizeof(error)));
    assert(strstr(error, "managed by the transport") != NULL);
    assert(!network_request_validate("POST", 16, "https://example.com/",
                                     NULL, 0, NULL, 1,
                                     error, sizeof(error)));
    assert(strstr(error, "body/headers") != NULL);

    assert(network_request_plan_init(&plan, "POST",
                                     "https://example.com/path/start",
                                     headers, 8, body, sizeof(body) - 1));
    assert(network_request_plan_redirect(&plan, 307, "next"));
    assert(strcmp(plan.url, "https://example.com/path/next") == 0);
    assert(strcmp(plan.method, "POST") == 0);
    assert(plan.body == body && plan.body_len == sizeof(body) - 1);
    assert(plan.header_count == 8);

    assert(network_request_plan_redirect(&plan, 302,
                                         "https://other.example/final"));
    assert(strcmp(plan.method, "GET") == 0);
    assert(plan.body == NULL && plan.body_len == 0);
    assert(!has_header(&plan, "Authorization"));
    assert(!has_header(&plan, "Cookie"));
    assert(!has_header(&plan, "Proxy-Authorization"));
    assert(!has_header(&plan, "Content-Type"));
    assert(!has_header(&plan, "Content-Language"));
    assert(!has_header(&plan, "Expect"));
    assert(!has_header(&plan, "Trailer"));
    assert(has_header(&plan, "X-Keep"));

    assert(network_request_plan_init(&plan, "PUT", "http://example.com/base",
                                     &headers[3], 1, body, sizeof(body) - 1));
    assert(network_request_plan_redirect(&plan, 303, "/root"));
    assert(strcmp(plan.url, "http://example.com/root") == 0);
    assert(strcmp(plan.method, "GET") == 0);
    assert(plan.header_count == 0);

    assert(network_redirect_status(301));
    assert(network_redirect_status(308));
    assert(!network_redirect_status(300));
    assert(!network_request_plan_redirect(&plan, 200, "/ignored"));
    assert(!network_request_plan_redirect(&plan, 302, ""));

    puts("network request core tests passed");
    return 0;
}