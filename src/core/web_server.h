#ifndef BUDO_WEB_SERVER_H
#define BUDO_WEB_SERVER_H

#ifdef __cplusplus
extern "C"
{
#endif

    int web_server_serve_directory(const char *root_dir, const char *host, int port);

#ifdef __cplusplus
}
#endif

#endif