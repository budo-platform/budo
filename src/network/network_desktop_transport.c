#include "network_desktop_transport.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET NetworkSocket;
typedef int NetworkSSize;
#define NETWORK_INVALID_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int NetworkSocket;
typedef ssize_t NetworkSSize;
#define NETWORK_INVALID_SOCKET (-1)
#endif

#if defined(__APPLE__) && !defined(BUDO_NETWORK_OPENSSL)
#define BUDO_NETWORK_SECURETRANSPORT 1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#else
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

struct NetworkDesktopConnection
{
    NetworkSocket socket_handle;
    bool use_tls;
    NetworkDesktopTransportHooks hooks;
#ifdef BUDO_NETWORK_SECURETRANSPORT
    SSLContextRef tls_context;
#else
    SSL *tls;
    SSL_CTX *tls_context;
#endif
};

static uint32_t remaining_ms(NetworkDesktopConnection *connection)
{
    return connection->hooks.remaining_ms
               ? connection->hooks.remaining_ms(connection->hooks.user_data)
               : 0;
}

static bool cancelled(NetworkDesktopConnection *connection)
{
    return connection->hooks.cancelled &&
           connection->hooks.cancelled(connection->hooks.user_data);
}

static int socket_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static void socket_close(NetworkSocket socket_handle)
{
    if (socket_handle == NETWORK_INVALID_SOCKET)
        return;
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

static NetworkSSize socket_read(NetworkSocket socket_handle, void *data, size_t length)
{
#ifdef _WIN32
    int amount = length > INT_MAX ? INT_MAX : (int)length;
    return recv(socket_handle, (char *)data, amount, 0);
#else
    return read(socket_handle, data, length);
#endif
}

static NetworkSSize socket_write(NetworkSocket socket_handle,
                                 const void *data, size_t length)
{
#ifdef _WIN32
    int amount = length > INT_MAX ? INT_MAX : (int)length;
    return send(socket_handle, (const char *)data, amount, 0);
#else
    return write(socket_handle, data, length);
#endif
}

static bool set_timeout(NetworkDesktopConnection *connection)
{
    uint32_t timeout_ms = remaining_ms(connection);
    if (timeout_ms == 0 || cancelled(connection))
        return false;
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    return setsockopt(connection->socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                      (const char *)&timeout, sizeof(timeout)) == 0 &&
           setsockopt(connection->socket_handle, SOL_SOCKET, SO_SNDTIMEO,
                      (const char *)&timeout, sizeof(timeout)) == 0;
#else
    struct timeval timeout = {(time_t)(timeout_ms / 1000u),
                              (suseconds_t)((timeout_ms % 1000u) * 1000u)};
    return setsockopt(connection->socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                      &timeout, sizeof(timeout)) == 0 &&
           setsockopt(connection->socket_handle, SOL_SOCKET, SO_SNDTIMEO,
                      &timeout, sizeof(timeout)) == 0;
#endif
}

static bool wait_readable(NetworkDesktopConnection *connection)
{
    uint32_t timeout_ms = remaining_ms(connection);
    if (timeout_ms == 0 || cancelled(connection))
        return false;
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(connection->socket_handle, &read_fds);
    struct timeval timeout = {(long)(timeout_ms / 1000u),
                              (long)((timeout_ms % 1000u) * 1000u)};
    return select((int)connection->socket_handle + 1, &read_fds, NULL, NULL,
                  &timeout) > 0;
}

static bool connect_with_deadline(NetworkDesktopConnection *connection,
                                  const struct sockaddr *address,
                                  socklen_t address_length)
{
    NetworkSocket socket_handle = connection->socket_handle;
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) != 0)
        return false;
    int result = connect(socket_handle, address, (int)address_length);
    int error = result == 0 ? 0 : socket_error();
    if (result != 0 && error != WSAEWOULDBLOCK && error != WSAEINPROGRESS)
        return false;
#else
    int flags = fcntl(socket_handle, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_handle, F_SETFL, flags | O_NONBLOCK) != 0)
        return false;
    int result = connect(socket_handle, address, address_length);
    int error = result == 0 ? 0 : socket_error();
    if (result != 0 && error != EINPROGRESS && error != EWOULDBLOCK)
        return false;
#endif
    if (result != 0)
    {
        uint32_t timeout_ms = remaining_ms(connection);
        if (timeout_ms == 0 || cancelled(connection))
            return false;
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(socket_handle, &write_fds);
        struct timeval timeout = {(long)(timeout_ms / 1000u),
                                  (long)((timeout_ms % 1000u) * 1000u)};
        if (select((int)socket_handle + 1, NULL, &write_fds, NULL, &timeout) <= 0)
            return false;
        socklen_t error_length = sizeof(error);
        if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR,
                       (char *)&error, &error_length) != 0 ||
            error != 0)
            return false;
    }
#ifdef _WIN32
    nonblocking = 0;
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) != 0)
        return false;
#else
    if (fcntl(socket_handle, F_SETFL, flags) != 0)
        return false;
#endif
    return set_timeout(connection);
}

#ifdef BUDO_NETWORK_SECURETRANSPORT
static OSStatus tls_read_callback(SSLConnectionRef connection, void *data,
                                  size_t *data_length)
{
    NetworkSSize amount = socket_read((NetworkSocket)(intptr_t)connection,
                                      data, *data_length);
    if (amount <= 0)
    {
        *data_length = 0;
        if (amount < 0 && (socket_error() == EINTR || socket_error() == EAGAIN ||
                           socket_error() == EWOULDBLOCK))
            return errSSLWouldBlock;
        return amount == 0 ? errSSLClosedGraceful : errSSLClosedAbort;
    }
    *data_length = (size_t)amount;
    return noErr;
}

static OSStatus tls_write_callback(SSLConnectionRef connection, const void *data,
                                   size_t *data_length)
{
    NetworkSSize amount = socket_write((NetworkSocket)(intptr_t)connection,
                                       data, *data_length);
    if (amount <= 0)
    {
        *data_length = 0;
        return errSSLClosedAbort;
    }
    *data_length = (size_t)amount;
    return noErr;
}
#endif

static bool tls_init(NetworkDesktopConnection *connection, const char *host)
{
    if (!connection->use_tls)
        return true;
#ifdef BUDO_NETWORK_SECURETRANSPORT
    connection->tls_context = SSLCreateContext(kCFAllocatorDefault,
                                               kSSLClientSide, kSSLStreamType);
    if (!connection->tls_context)
        return false;
    SSLSetIOFuncs(connection->tls_context, tls_read_callback, tls_write_callback);
    SSLSetConnection(connection->tls_context,
                     (SSLConnectionRef)(intptr_t)connection->socket_handle);
    SSLSetPeerDomainName(connection->tls_context, host, strlen(host));
    OSStatus handshake_status;
    do
    {
        handshake_status = SSLHandshake(connection->tls_context);
        if (handshake_status == errSSLWouldBlock && !wait_readable(connection))
            break;
    } while (handshake_status == errSSLWouldBlock);
    if (handshake_status != noErr)
    {
        CFRelease(connection->tls_context);
        connection->tls_context = NULL;
        return false;
    }
#else
    connection->tls_context = SSL_CTX_new(TLS_client_method());
    if (!connection->tls_context)
        return false;
    SSL_CTX_set_default_verify_paths(connection->tls_context);
    SSL_CTX_set_verify(connection->tls_context, SSL_VERIFY_PEER, NULL);
    connection->tls = SSL_new(connection->tls_context);
    if (!connection->tls)
        return false;
    SSL_set_fd(connection->tls, connection->socket_handle);
    X509_VERIFY_PARAM *verify = SSL_get0_param(connection->tls);
    unsigned char address[sizeof(struct in6_addr)];
    bool is_ip = inet_pton(AF_INET, host, address) == 1 ||
                 inet_pton(AF_INET6, host, address) == 1;
    if ((is_ip && X509_VERIFY_PARAM_set1_ip_asc(verify, host) != 1) ||
        (!is_ip && (X509_VERIFY_PARAM_set1_host(verify, host, 0) != 1 ||
                    SSL_set_tlsext_host_name(connection->tls, host) != 1)) ||
        SSL_connect(connection->tls) <= 0)
        return false;
#endif
    return true;
}

bool network_desktop_transport_init(void)
{
#ifdef _WIN32
    static INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;
    BOOL pending;
    void *ignored;
    if (!InitOnceBeginInitialize(&winsock_once, 0, &pending, &ignored))
        return false;
    if (pending)
    {
        WSADATA data;
        BOOL failed = WSAStartup(MAKEWORD(2, 2), &data) != 0;
        InitOnceComplete(&winsock_once, failed ? INIT_ONCE_INIT_FAILED : 0, NULL);
        if (failed)
            return false;
    }
#endif
#if !defined(BUDO_NETWORK_SECURETRANSPORT)
    static bool tls_initialized = false;
    if (!tls_initialized)
    {
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
        tls_initialized = true;
    }
#endif
    return true;
}

NetworkDesktopConnection *network_desktop_transport_open(
    const char *host, int port, bool use_tls,
    const NetworkDesktopTransportHooks *hooks,
    char *error, size_t error_size)
{
    NetworkDesktopConnection *connection = calloc(1, sizeof(*connection));
    if (!connection)
    {
        snprintf(error, error_size, "Out of memory");
        return NULL;
    }
    connection->socket_handle = NETWORK_INVALID_SOCKET;
    connection->use_tls = use_tls;
    if (hooks)
        connection->hooks = *hooks;

    if (remaining_ms(connection) == 0 || cancelled(connection))
    {
        snprintf(error, error_size, "HTTP request timed out or was cancelled");
        free(connection);
        return NULL;
    }

    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%d", port);
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    int resolve_error = getaddrinfo(host, port_text, &hints, &addresses);
    if (resolve_error != 0)
    {
        snprintf(error, error_size, "DNS resolution failed: %s",
                 gai_strerror(resolve_error));
        free(connection);
        return NULL;
    }

    connection->socket_handle = socket(addresses->ai_family, addresses->ai_socktype,
                                       addresses->ai_protocol);
    if (connection->socket_handle == NETWORK_INVALID_SOCKET)
    {
        snprintf(error, error_size, "Socket creation failed (error %d)", socket_error());
        freeaddrinfo(addresses);
        free(connection);
        return NULL;
    }
    if (connection->hooks.publish_socket)
        connection->hooks.publish_socket(connection->hooks.user_data,
                                         (intptr_t)connection->socket_handle);
    if (!connect_with_deadline(connection, addresses->ai_addr,
                               (socklen_t)addresses->ai_addrlen))
    {
        if (remaining_ms(connection) == 0)
            snprintf(error, error_size, "HTTP request timed out while connecting");
        else
            snprintf(error, error_size,
                     "Connection failed or request was cancelled (error %d)", socket_error());
        freeaddrinfo(addresses);
        network_desktop_transport_close(connection);
        return NULL;
    }
    freeaddrinfo(addresses);

    if (!tls_init(connection, host))
    {
        snprintf(error, error_size, "%s",
                 remaining_ms(connection) == 0
                     ? "HTTP request timed out during TLS handshake"
                     : "TLS handshake failed or request was cancelled");
        network_desktop_transport_close(connection);
        return NULL;
    }
    return connection;
}

static NetworkSSize transport_write(NetworkDesktopConnection *connection,
                                    const void *data, size_t length)
{
    if (!set_timeout(connection))
        return -1;
    if (!connection->use_tls)
        return socket_write(connection->socket_handle, data, length);
#ifdef BUDO_NETWORK_SECURETRANSPORT
    size_t processed = length;
    return SSLWrite(connection->tls_context, data, length, &processed) == noErr
               ? (NetworkSSize)processed
               : -1;
#else
    if (length > INT_MAX)
        length = INT_MAX;
    return SSL_write(connection->tls, data, (int)length);
#endif
}

bool network_desktop_transport_write_all(NetworkDesktopConnection *connection,
                                         const void *data, size_t length)
{
    const uint8_t *cursor = data;
    while (length > 0)
    {
        NetworkSSize written = transport_write(connection, cursor, length);
        if (written <= 0)
            return false;
        cursor += written;
        length -= (size_t)written;
    }
    return true;
}

ptrdiff_t network_desktop_transport_read(NetworkDesktopConnection *connection,
                                         void *data, size_t length)
{
    if (!set_timeout(connection))
        return -1;
    if (!connection->use_tls)
        return (ptrdiff_t)socket_read(connection->socket_handle, data, length);
#ifdef BUDO_NETWORK_SECURETRANSPORT
    for (;;)
    {
        size_t processed = length;
        OSStatus status = SSLRead(connection->tls_context, data, length, &processed);
        if (status == noErr || status == errSSLClosedGraceful || processed > 0)
            return (ptrdiff_t)processed;
        if (status != errSSLWouldBlock || remaining_ms(connection) == 0 ||
            cancelled(connection))
            return -1;
        if (!wait_readable(connection))
            return -1;
    }
#else
    if (length > INT_MAX)
        length = INT_MAX;
    return (ptrdiff_t)SSL_read(connection->tls, data, (int)length);
#endif
}

void network_desktop_transport_close(NetworkDesktopConnection *connection)
{
    if (!connection)
        return;
    if (connection->use_tls)
    {
#ifdef BUDO_NETWORK_SECURETRANSPORT
        if (connection->tls_context)
        {
            if (!cancelled(connection))
                SSLClose(connection->tls_context);
            CFRelease(connection->tls_context);
        }
#else
        if (connection->tls)
        {
            if (!cancelled(connection))
                SSL_shutdown(connection->tls);
            SSL_free(connection->tls);
        }
        if (connection->tls_context)
            SSL_CTX_free(connection->tls_context);
#endif
    }
    if (connection->socket_handle != NETWORK_INVALID_SOCKET)
    {
        bool owned = !connection->hooks.release_socket ||
                     connection->hooks.release_socket(
                         connection->hooks.user_data,
                         (intptr_t)connection->socket_handle);
        if (owned)
            socket_close(connection->socket_handle);
    }
    free(connection);
}

void network_desktop_transport_abort(intptr_t socket_handle)
{
    NetworkSocket socket_value = (NetworkSocket)socket_handle;
    if (socket_value == NETWORK_INVALID_SOCKET)
        return;
#ifdef _WIN32
    shutdown(socket_value, SD_BOTH);
#else
    shutdown(socket_value, SHUT_RDWR);
#endif
    socket_close(socket_value);
}