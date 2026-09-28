#ifndef BUDO_SQLITE_SERVICE_H
#define BUDO_SQLITE_SERVICE_H

#include "core/api_error.h"
#include "sqlite_wrapper.h"

int sqlite_service_open(SqliteContext *context, const char *name,
                        ApiError *error);
bool sqlite_service_execute(SqliteContext *context, int handle,
                            const char *sql, ApiError *error);
int sqlite_service_run(SqliteContext *context, int handle, const char *sql,
                       const SqliteParam *params, int param_count,
                       ApiError *error);
SqliteResult *sqlite_service_query(SqliteContext *context, int handle,
                                   const char *sql,
                                   const SqliteParam *params, int param_count,
                                   ApiError *error);

#endif