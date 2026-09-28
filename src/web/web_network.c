#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten.h>
#include "network/network_wrapper.h"

static NetworkContext *g_network_web_ctx = NULL;
static uint32_t g_network_web_generation = 0;

void web_network_set_context(NetworkContext *ctx)
{
    g_network_web_ctx = ctx;
    g_network_web_generation = network_web_generation(ctx);
}

EM_JS(void, web_cancel_fetches, (uint32_t generation), {
    if (!Module._budoFetchControllers)
        return;
    Module._budoFetchControllers.forEach(function(entry, key) {
        if (entry.generation === generation)
        {
            clearTimeout(entry.timeout);
            entry.controller.abort();
            Module._budoFetchControllers.delete(key);
        }
    });
});

void web_network_context_destroyed(NetworkContext *ctx, uint32_t generation)
{
    web_cancel_fetches(generation);
    if (g_network_web_ctx == ctx && g_network_web_generation == generation)
    {
        g_network_web_ctx = NULL;
        g_network_web_generation = 0;
    }
}

EMSCRIPTEN_KEEPALIVE
void web_network_on_success(uint32_t generation, int request_id,
                            int status,
                            const char *status_text,
                            const char *url,
                            int redirected,
                            const uint8_t *body, int body_len,
                            const char *headers_flat)
{
    if (!g_network_web_ctx || generation != g_network_web_generation)
        return;
    if (body_len < 0 || (size_t)body_len > NETWORK_MAX_RESPONSE_BODY_SIZE ||
        (headers_flat && strlen(headers_flat) > NETWORK_MAX_RESPONSE_HEADERS_SIZE))
    {
        network_async_complete(g_network_web_ctx, request_id, NULL,
                               "Web HTTP response exceeds configured size limits");
        return;
    }

    NetworkResponse *resp = calloc(1, sizeof(NetworkResponse));
    if (!resp)
    {
        network_async_complete(g_network_web_ctx, request_id, NULL, "Out of memory");
        return;
    }

    resp->status = status;
    resp->status_text = strdup(status_text ? status_text : "");
    resp->url = strdup(url ? url : "");
    resp->redirected = (redirected != 0);
    if (!resp->status_text || !resp->url)
    {
        network_response_free(resp);
        network_async_complete(g_network_web_ctx, request_id, NULL, "Out of memory");
        return;
    }

    if (body && body_len > 0)
    {
        resp->body = malloc((size_t)body_len);
        if (!resp->body)
        {
            network_response_free(resp);
            network_async_complete(g_network_web_ctx, request_id, NULL, "Out of memory");
            return;
        }
        memcpy(resp->body, body, (size_t)body_len);
        resp->body_len = (size_t)body_len;
    }

    if (headers_flat && headers_flat[0])
    {
        const char *p = headers_flat;
        while (*p && resp->header_count < NETWORK_MAX_HEADERS)
        {
            const char *colon = strchr(p, ':');
            const char *eol = strchr(p, '\n');
            if (!eol)
                eol = p + strlen(p);
            if (colon && colon < eol)
            {
                size_t name_len = (size_t)(colon - p);
                const char *val = colon + 1;
                while (val < eol && *val == ' ')
                    val++;
                size_t val_len = (size_t)(eol - val);

                resp->headers[resp->header_count].name = strndup(p, name_len);
                resp->headers[resp->header_count].value = strndup(val, val_len);
                if (!resp->headers[resp->header_count].name ||
                    !resp->headers[resp->header_count].value)
                {
                    free(resp->headers[resp->header_count].name);
                    free(resp->headers[resp->header_count].value);
                    resp->headers[resp->header_count].name = NULL;
                    resp->headers[resp->header_count].value = NULL;
                    network_response_free(resp);
                    network_async_complete(g_network_web_ctx, request_id, NULL, "Out of memory");
                    return;
                }
                resp->header_count++;
            }
            p = (*eol) ? eol + 1 : eol;
        }
    }

    network_async_complete(g_network_web_ctx, request_id, resp, NULL);
}

EMSCRIPTEN_KEEPALIVE
void web_network_on_error(uint32_t generation, int request_id, const char *error)
{
    if (!g_network_web_ctx || generation != g_network_web_generation)
        return;

    network_async_complete(g_network_web_ctx, request_id, NULL,
                           error ? error : "fetch failed");
}

EMSCRIPTEN_KEEPALIVE
int web_network_is_url_allowed(uint32_t generation, const char *url)
{
    return g_network_web_ctx && generation == g_network_web_generation && url &&
           network_web_url_allowed(g_network_web_ctx, url);
}

EM_JS(void, web_start_fetch, (int request_id, uint32_t generation, int allow_all, const char *url_ptr, const char *method_ptr, const char *headers_ptr, const void *body_ptr, int body_len, int max_response_headers, int max_response_body, int max_url, int max_redirects, int timeout_ms), {
    var url = UTF8ToString(url_ptr);
    var method = UTF8ToString(method_ptr);
    var headersStr = headers_ptr ? UTF8ToString(headers_ptr) : "";

    if (!Module._budoFetchControllers)
        Module._budoFetchControllers = new Map();
    var key = generation + ':' + request_id;
    var controller = new AbortController();
    var timedOut = false;
    var timeout = setTimeout(function() {
        timedOut = true;
        controller.abort();
    }, timeout_ms);
    var didRedirect = false;
    Module._budoFetchControllers.set(key, {generation : generation, controller : controller,
                                           timeout : timeout});

     var opts = {method : method, signal : controller.signal,
                     redirect : allow_all ? 'follow' : 'manual'};

    var setupError = null;
    try {
        
        if (headersStr)
        {
            var hdrs = new Headers();
            headersStr.split('\n').forEach(function(line) {
                var idx = line.indexOf(':');
                if (idx > 0)
                {
                    hdrs.append(line.substring(0, idx).trim(),
                                line.substring(idx + 1).trim());
                }
            });
            opts.headers = hdrs;
        }

        if (body_ptr && body_len > 0)
        {
            opts.body = HEAPU8.slice(body_ptr, body_ptr + body_len);
        }
    } catch (err) {
        setupError = err;
    }

    function fetchHop(currentUrl, currentOpts, redirectCount) {
        return fetch(currentUrl, currentOpts).then(function(response) {
            if (allow_all)
                return response;
            var isRedirect = response.status === 301 || response.status === 302 ||
                             response.status === 303 || response.status === 307 ||
                             response.status === 308;
            if (response.type === 'opaqueredirect')
                throw new Error('HTTP redirect blocked: browser did not expose the redirect target');
            if (!isRedirect)
                return response;
            if (redirectCount >= max_redirects)
                throw new Error('HTTP redirect limit exceeded');
            var location = response.headers.get('location');
            if (!location)
                throw new Error('HTTP redirect blocked: missing redirect target');
            var nextUrl = new URL(location, response.url || currentUrl);
            if (nextUrl.protocol !== 'http:' && nextUrl.protocol !== 'https:')
                throw new Error('HTTP redirect blocked: unsupported target scheme');
            if (new TextEncoder().encode(nextUrl.href).byteLength >= max_url)
                throw new Error('HTTP redirect target exceeds URL size limit');
            var nextUrlPtr = stringToNewUTF8(nextUrl.href);
            var targetAllowed = nextUrlPtr && _web_network_is_url_allowed(generation, nextUrlPtr);
            if (nextUrlPtr) _free(nextUrlPtr);
            if (!targetAllowed)
                throw new Error('HTTP redirect blocked by network policy');
            didRedirect = true;
            var nextOpts = Object.assign({}, currentOpts);
            if (currentOpts.headers)
                nextOpts.headers = new Headers(currentOpts.headers);
            if (new URL(currentUrl).origin !== nextUrl.origin && nextOpts.headers)
            {
                nextOpts.headers.delete('authorization');
                nextOpts.headers.delete('cookie');
                nextOpts.headers.delete('proxy-authorization');
            }
            if (response.status === 303 || ((response.status === 301 || response.status === 302) &&
                                           currentOpts.method.toUpperCase() === 'POST'))
            {
                nextOpts.method = 'GET';
                delete nextOpts.body;
                if (nextOpts.headers)
                {
                    Array.from(nextOpts.headers.keys()).forEach(function(name) {
                        if (name.toLowerCase().indexOf('content-') === 0 ||
                            name.toLowerCase() === 'expect' || name.toLowerCase() === 'trailer')
                            nextOpts.headers.delete(name);
                    });
                }
            }
            return fetchHop(nextUrl.href, nextOpts, redirectCount + 1);
        });
    }

    (setupError ? Promise.reject(setupError) : fetchHop(url, opts, 0))
        .then(function(response) {
            
            var headerLines = [];
            response.headers.forEach(function(value, name) {
                headerLines.push(name + ': ' + value);
            });
            var headersFlat = headerLines.join('\n');
            if (new TextEncoder().encode(headersFlat).byteLength > max_response_headers)
                throw new Error('HTTP response headers exceed size limit');
            if (new TextEncoder().encode(response.statusText).byteLength > 1024 ||
                new TextEncoder().encode(response.url).byteLength >= max_url)
                throw new Error('HTTP response status or final URL exceeds size limit');

            var contentLength = response.headers.get('content-length');
            if (contentLength !== null && Number(contentLength) > max_response_body)
                throw new Error('HTTP response body exceeds size limit');

            var reader = response.body ? response.body.getReader() : null;
            var chunks = [];
            var total = 0;
            function readChunk() {
                if (!reader)
                    return Promise.resolve(new Uint8Array(0));
                return reader.read().then(function(part) {
                    if (part.done)
                    {
                        var body = new Uint8Array(total);
                        var offset = 0;
                        chunks.forEach(function(chunk) {
                            body.set(chunk, offset);
                            offset += chunk.byteLength;
                        });
                        return body;
                    }
                    if (part.value.byteLength > max_response_body - total)
                    {
                        reader.cancel();
                        throw new Error('HTTP response body exceeds size limit');
                    }
                    total += part.value.byteLength;
                    chunks.push(part.value);
                    return readChunk();
                });
            }

            return readChunk().then(function(body) {
                return {
                    status : response.status,
                    statusText : response.statusText,
                    url : response.url,
                    redirected : (didRedirect || response.redirected) ? 1 : 0,
                    headers : headersFlat,
                    body : body
                };
            });
        })
        .then(function(result) {
            
            var bodyPtr = 0;
            var bodyLen = result.body.byteLength;
            if (bodyLen > 0)
            {
                bodyPtr = _malloc(bodyLen);
                if (!bodyPtr)
                    throw new Error('Out of memory while copying HTTP response body');
                HEAPU8.set(result.body, bodyPtr);
            }
            var statusTextPtr = stringToNewUTF8(result.statusText);
            var urlPtr = stringToNewUTF8(result.url);
            var headersPtr = stringToNewUTF8(result.headers);
            if (!statusTextPtr || !urlPtr || !headersPtr)
            {
                if (statusTextPtr) _free(statusTextPtr);
                if (urlPtr) _free(urlPtr);
                if (headersPtr) _free(headersPtr);
                if (bodyPtr) _free(bodyPtr);
                throw new Error('Out of memory while copying HTTP response metadata');
            }

            Module._budoFetchControllers.delete(key);
            clearTimeout(timeout);
            _web_network_on_success(generation, request_id, result.status,
                                    statusTextPtr, urlPtr,
                                    result.redirected,
                                    bodyPtr, bodyLen, headersPtr);

            _free(statusTextPtr);
            _free(urlPtr);
            _free(headersPtr);
            if (bodyPtr)
                _free(bodyPtr);
        })
        .catch(function(err) {
            Module._budoFetchControllers.delete(key);
            clearTimeout(timeout);
            var message = timedOut ? 'HTTP request timed out' : (err.message || String(err));
            var msgPtr = stringToNewUTF8(message);
            _web_network_on_error(generation, request_id, msgPtr);
            _free(msgPtr);
        });
});

void web_network_start_fetch(int request_id,
                             uint32_t generation,
                             const char *method,
                             const char *url,
                             const NetworkHeader *req_headers,
                             int req_header_count,
                             const uint8_t *req_body,
                             size_t req_body_len)
{
    
    char headers_buf[NETWORK_MAX_REQUEST_HEADERS_SIZE + 1] = "";
    size_t offset = 0;
    for (int i = 0; i < req_header_count; i++)
    {
        size_t remaining = sizeof(headers_buf) - offset;
        int n = snprintf(headers_buf + offset, remaining,
                         "%s: %s\n", req_headers[i].name, req_headers[i].value);
        if (n < 0 || (size_t)n >= remaining)
        {
            if (g_network_web_ctx && generation == g_network_web_generation)
                network_async_complete(g_network_web_ctx, request_id, NULL,
                                       "HTTP request headers exceed size limit");
            return;
        }
        offset += (size_t)n;
    }

    web_start_fetch(request_id, generation, network_web_allow_all(g_network_web_ctx),
                    url, method ? method : "GET",
                    headers_buf, req_body, (int)req_body_len,
                    (int)NETWORK_MAX_RESPONSE_HEADERS_SIZE,
                    (int)NETWORK_MAX_RESPONSE_BODY_SIZE,
                    (int)NETWORK_MAX_URL_SIZE,
                    NETWORK_MAX_REDIRECTS,
                    (int)NETWORK_REQUEST_TIMEOUT_MS);
}