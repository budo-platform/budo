#ifndef WEB_SQLITE_H
#define WEB_SQLITE_H

void web_sqlite_init_fs(void (*callback)(void));

void web_sqlite_sync(void);

#endif