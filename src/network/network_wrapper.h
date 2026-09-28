#ifndef NETWORK_WRAPPER_H
#define NETWORK_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NETWORK_MAX_DOMAINS 64

#define NETWORK_MAX_HEADERS 128

#define NETWORK_MAX_REQUEST_HEADERS_SIZE (64u * 1024u)
#define NETWORK_MAX_REQUEST_BODY_SIZE (16u * 1024u * 1024u)
#define NETWORK_MAX_URL_SIZE 8192u
#define NETWORK_MAX_RESPONSE_HEADERS_SIZE (64u * 1024u)
#define NETWORK_MAX_RESPONSE_BODY_SIZE (16u * 1024u * 1024u)
#define NETWORK_MAX_REDIRECTS 5
#define NETWORK_REQUEST_TIMEOUT_MS 30000u

    typedef struct
    {
        bool allow_all;                         
        int domain_count;                       
        char domains[NETWORK_MAX_DOMAINS][256]; 
    } NetworkPolicy;

    typedef struct
    {
        char *name;
        char *value;
    } NetworkHeader;

    typedef struct
    {
        int status;                                 
        char *status_text;                          
        char *url;                                  
        bool redirected;                            
        uint8_t *body;                              
        size_t body_len;                            
        NetworkHeader headers[NETWORK_MAX_HEADERS]; 
        int header_count;                           
    } NetworkResponse;

    typedef struct NetworkContext NetworkContext;

    NetworkContext *network_create(const NetworkPolicy *policy);

    void network_destroy(NetworkContext *ctx);

    bool network_is_enabled(NetworkContext *ctx);

    NetworkResponse *network_request(NetworkContext *ctx,
                                     const char *method,
                                     const char *url,
                                     const NetworkHeader *req_headers,
                                     int req_header_count,
                                     const uint8_t *req_body,
                                     size_t req_body_len);

    void network_response_free(NetworkResponse *resp);

    const char *network_get_error(NetworkContext *ctx);

    void network_policy_parse(NetworkPolicy *policy, const char *value);

    void network_policy_load_app_json(NetworkPolicy *policy, const char *project_dir);

    bool network_policy_allows_url(const NetworkPolicy *policy, const char *url,
                                   char *error, size_t error_size);

#define NETWORK_MAX_ASYNC 32

    typedef void (*NetworkAsyncCallback)(int request_id,
                                         NetworkResponse *response,
                                         const char *error,
                                         void *user_data);

    int network_request_async(NetworkContext *ctx,
                              const char *method,
                              const char *url,
                              const NetworkHeader *req_headers,
                              int req_header_count,
                              const uint8_t *req_body,
                              size_t req_body_len,
                              NetworkAsyncCallback callback,
                              void *user_data);

    void network_async_poll(NetworkContext *ctx);

    void network_shutdown(NetworkContext *ctx);

#ifdef BUDO_WEB

    void network_async_complete(NetworkContext *ctx, int request_id,
                                NetworkResponse *response, const char *error);

    uint32_t network_web_generation(NetworkContext *ctx);

    bool network_web_allow_all(NetworkContext *ctx);

    bool network_web_url_allowed(NetworkContext *ctx, const char *url);
#endif

#ifdef __cplusplus
}
#endif

#endif