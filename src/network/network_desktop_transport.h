#ifndef NETWORK_DESKTOP_TRANSPORT_H
#define NETWORK_DESKTOP_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct NetworkDesktopConnection NetworkDesktopConnection;

typedef struct
{
    uint32_t (*remaining_ms)(void *user_data);
    bool (*cancelled)(void *user_data);
    void (*publish_socket)(void *user_data, intptr_t socket_handle);
    bool (*release_socket)(void *user_data, intptr_t socket_handle);
    void *user_data;
} NetworkDesktopTransportHooks;

bool network_desktop_transport_init(void);

NetworkDesktopConnection *network_desktop_transport_open(
    const char *host, int port, bool use_tls,
    const NetworkDesktopTransportHooks *hooks,
    char *error, size_t error_size);

bool network_desktop_transport_write_all(NetworkDesktopConnection *connection,
                                         const void *data, size_t length);

ptrdiff_t network_desktop_transport_read(NetworkDesktopConnection *connection,
                                         void *data, size_t length);

void network_desktop_transport_close(NetworkDesktopConnection *connection);

void network_desktop_transport_abort(intptr_t socket_handle);

#endif