#include "sqlite_service.h"

#include <stddef.h>

static bool sqlite_service_validate(SqliteContext *context, int handle,
                                    const char *sql, ApiError *error)
{
    api_error_clear(error);
    if (!context)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "sqlite.invalid_state", "SQLite context is unavailable");
        return false;
    }
    if (handle < 0 || !sql || !sql[0])
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "sqlite.invalid_argument",
                      "Database handle and SQL are required");
        return false;
    }
    return true;
}

static void sqlite_service_wrapper_error(SqliteContext *context,
                                         const char *code, ApiError *error)
{
    const char *message = sqlite_get_error(context);
    api_error_set(error, API_STATUS_APPLICATION_ERROR, code,
                  message && message[0] ? message : "SQLite operation failed");
}

int sqlite_service_open(SqliteContext *context, const char *name,
                        ApiError *error)
{
    int result;
    api_error_clear(error);
    if (!context)
    {
        api_error_set(error, API_STATUS_INVALID_STATE,
                      "sqlite.invalid_state", "SQLite context is unavailable");
        return -1;
    }
    if (!sqlite_is_valid_db_name(name))
    {
        api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                      "sqlite.invalid_name", "Database name is invalid");
        return -1;
    }
    result = sqlite_open(context, name);
    if (result < 0)
        sqlite_service_wrapper_error(context, "sqlite.open_failed", error);
    return result;
}

bool sqlite_service_execute(SqliteContext *context, int handle,
                            const char *sql, ApiError *error)
{
    if (!sqlite_service_validate(context, handle, sql, error))
        return false;
    if (sqlite_execute(context, handle, sql))
        return true;
    sqlite_service_wrapper_error(context, "sqlite.execute_failed", error);
    return false;
}

int sqlite_service_run(SqliteContext *context, int handle, const char *sql,
                       const SqliteParam *params, int param_count,
                       ApiError *error)
{
    int result;
    if (!sqlite_service_validate(context, handle, sql, error) ||
        param_count < 0 || param_count > SQLITE_MAX_PARAMS)
    {
        if (!api_error_has_error(error))
            api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                          "sqlite.invalid_parameters",
                          "SQLite parameter count is invalid");
        return -1;
    }
    result = sqlite_run(context, handle, sql, params, param_count);
    if (result < 0)
        sqlite_service_wrapper_error(context, "sqlite.run_failed", error);
    return result;
}

SqliteResult *sqlite_service_query(SqliteContext *context, int handle,
                                   const char *sql,
                                   const SqliteParam *params, int param_count,
                                   ApiError *error)
{
    SqliteResult *result;
    if (!sqlite_service_validate(context, handle, sql, error) ||
        param_count < 0 || param_count > SQLITE_MAX_PARAMS)
    {
        if (!api_error_has_error(error))
            api_error_set(error, API_STATUS_INVALID_ARGUMENT,
                          "sqlite.invalid_parameters",
                          "SQLite parameter count is invalid");
        return (SqliteResult *)0;
    }
    result = sqlite_query(context, handle, sql, params, param_count);
    if (!result)
        sqlite_service_wrapper_error(context, "sqlite.query_failed", error);
    return result;
}