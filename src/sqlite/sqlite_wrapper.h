#ifndef SQLITE_WRAPPER_H
#define SQLITE_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SQLITE_MAX_DBS 16

#define SQLITE_MAX_PARAMS 32

    typedef struct SqliteContext SqliteContext;

    typedef enum
    {
        SQLITE_VAL_NULL = 0,
        SQLITE_VAL_INTEGER,
        SQLITE_VAL_FLOAT,
        SQLITE_VAL_TEXT,
        SQLITE_VAL_BLOB
    } SqliteValueType;

    typedef struct
    {
        SqliteValueType type;
        union
        {
            int64_t integer;
            double real;
            struct
            {
                const char *data;
                int length;
            } text;
        } value;
    } SqliteValue;

    typedef struct
    {
        int column_count;
        const char **column_names;
        SqliteValue *values;
    } SqliteRow;

    typedef struct
    {
        int row_count;
        int column_count;
        const char **column_names;
        SqliteRow *rows;
        char *error;
    } SqliteResult;

    typedef struct
    {
        SqliteValueType type;
        union
        {
            int64_t integer;
            double real;
            struct
            {
                const char *data;
                int length;
            } text;
        } value;
    } SqliteParam;

    SqliteContext *sqlite_create(const char *project_dir);

    void sqlite_destroy(SqliteContext *ctx);

    int sqlite_open(SqliteContext *ctx, const char *name);

    bool sqlite_is_valid_db_name(const char *name);

    void sqlite_close(SqliteContext *ctx, int handle);

    bool sqlite_execute(SqliteContext *ctx, int handle, const char *sql);

    int sqlite_run(SqliteContext *ctx, int handle, const char *sql,
                   const SqliteParam *params, int param_count);

    SqliteResult *sqlite_query(SqliteContext *ctx, int handle, const char *sql,
                               const SqliteParam *params, int param_count);

    void sqlite_result_free(SqliteResult *result);

    const char *sqlite_get_error(SqliteContext *ctx);

    int64_t sqlite_last_insert_id(SqliteContext *ctx, int handle);

#ifdef __cplusplus
}
#endif

#endif