#include "sqlite_wrapper.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include "web/web_sqlite.h"
#endif

struct SqliteContext
{
    char project_dir[1024];
    sqlite3 *dbs[SQLITE_MAX_DBS];
    char error[512];
};

SqliteContext *sqlite_create(const char *project_dir)
{
    SqliteContext *ctx = (SqliteContext *)calloc(1, sizeof(SqliteContext));
    if (!ctx)
        return NULL;

    if (project_dir)
    {
        snprintf(ctx->project_dir, sizeof(ctx->project_dir), "%s", project_dir);
    }

    return ctx;
}

void sqlite_destroy(SqliteContext *ctx)
{
    if (!ctx)
        return;

    for (int i = 0; i < SQLITE_MAX_DBS; i++)
    {
        if (ctx->dbs[i])
        {
            sqlite3_close(ctx->dbs[i]);
            ctx->dbs[i] = NULL;
        }
    }

    free(ctx);
}

bool sqlite_is_valid_db_name(const char *name)
{
    if (!name || !name[0])
        return false;
    for (const char *p = name; *p; p++)
    {
        char c = *p;
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
            !(c >= '0' && c <= '9') && c != '_' && c != '-')
        {
            return false;
        }
    }
    return true;
}

int sqlite_open(SqliteContext *ctx, const char *name)
{
    if (!ctx || !name)
        return -1;

    if (!sqlite_is_valid_db_name(name))
    {
        snprintf(ctx->error, sizeof(ctx->error),
                 "Invalid database name: only alphanumeric, underscore, and hyphen allowed");
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < SQLITE_MAX_DBS; i++)
    {
        if (!ctx->dbs[i])
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Too many open databases (max %d)", SQLITE_MAX_DBS);
        return -1;
    }

    char db_path[2048];
    snprintf(db_path, sizeof(db_path), "%s/%s.db", ctx->project_dir, name);

    int rc = sqlite3_open(db_path, &ctx->dbs[slot]);
    if (rc != SQLITE_OK)
    {
        snprintf(ctx->error, sizeof(ctx->error), "Failed to open database: %s",
                 ctx->dbs[slot] ? sqlite3_errmsg(ctx->dbs[slot]) : "unknown error");
        if (ctx->dbs[slot])
        {
            sqlite3_close(ctx->dbs[slot]);
            ctx->dbs[slot] = NULL;
        }
        return -1;
    }

    sqlite3_exec(ctx->dbs[slot], "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    sqlite3_exec(ctx->dbs[slot], "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    return slot;
}

void sqlite_close(SqliteContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= SQLITE_MAX_DBS || !ctx->dbs[handle])
        return;

    sqlite3_close(ctx->dbs[handle]);
    ctx->dbs[handle] = NULL;

#ifdef __EMSCRIPTEN__
    web_sqlite_sync();
#endif
}

static bool bind_params(sqlite3_stmt *stmt, const SqliteParam *params, int param_count,
                        char *error, int error_size)
{
    for (int i = 0; i < param_count; i++)
    {
        int rc;
        switch (params[i].type)
        {
        case SQLITE_VAL_NULL:
            rc = sqlite3_bind_null(stmt, i + 1);
            break;
        case SQLITE_VAL_INTEGER:
            rc = sqlite3_bind_int64(stmt, i + 1, params[i].value.integer);
            break;
        case SQLITE_VAL_FLOAT:
            rc = sqlite3_bind_double(stmt, i + 1, params[i].value.real);
            break;
        case SQLITE_VAL_TEXT:
            rc = sqlite3_bind_text(stmt, i + 1, params[i].value.text.data,
                                   params[i].value.text.length, SQLITE_TRANSIENT);
            break;
        default:
            rc = sqlite3_bind_null(stmt, i + 1);
            break;
        }
        if (rc != SQLITE_OK)
        {
            snprintf(error, error_size, "Failed to bind parameter %d", i + 1);
            return false;
        }
    }
    return true;
}

bool sqlite_execute(SqliteContext *ctx, int handle, const char *sql)
{
    if (!ctx || handle < 0 || handle >= SQLITE_MAX_DBS || !ctx->dbs[handle] || !sql)
        return false;

    char *errmsg = NULL;
    int rc = sqlite3_exec(ctx->dbs[handle], sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK)
    {
        snprintf(ctx->error, sizeof(ctx->error), "SQL error: %s", errmsg ? errmsg : "unknown");
        if (errmsg)
            sqlite3_free(errmsg);
        return false;
    }

    ctx->error[0] = '\0';

#ifdef __EMSCRIPTEN__
    web_sqlite_sync();
#endif

    return true;
}

int sqlite_run(SqliteContext *ctx, int handle, const char *sql,
               const SqliteParam *params, int param_count)
{
    if (!ctx || handle < 0 || handle >= SQLITE_MAX_DBS || !ctx->dbs[handle] || !sql)
        return -1;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(ctx->dbs[handle], sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        snprintf(ctx->error, sizeof(ctx->error), "SQL prepare error: %s",
                 sqlite3_errmsg(ctx->dbs[handle]));
        return -1;
    }

    if (params && param_count > 0)
    {
        if (!bind_params(stmt, params, param_count, ctx->error, sizeof(ctx->error)))
        {
            sqlite3_finalize(stmt);
            return -1;
        }
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        snprintf(ctx->error, sizeof(ctx->error), "SQL execution error: %s",
                 sqlite3_errmsg(ctx->dbs[handle]));
        return -1;
    }

    ctx->error[0] = '\0';

#ifdef __EMSCRIPTEN__
    web_sqlite_sync();
#endif

    return sqlite3_changes(ctx->dbs[handle]);
}

SqliteResult *sqlite_query(SqliteContext *ctx, int handle, const char *sql,
                           const SqliteParam *params, int param_count)
{
    SqliteResult *result = (SqliteResult *)calloc(1, sizeof(SqliteResult));
    if (!result)
        return NULL;

    if (!ctx || handle < 0 || handle >= SQLITE_MAX_DBS || !ctx->dbs[handle] || !sql)
    {
        result->error = strdup("Invalid handle or SQL");
        return result;
    }

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(ctx->dbs[handle], sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        result->error = strdup(sqlite3_errmsg(ctx->dbs[handle]));
        snprintf(ctx->error, sizeof(ctx->error), "%s", result->error);
        return result;
    }

    if (params && param_count > 0)
    {
        if (!bind_params(stmt, params, param_count, ctx->error, sizeof(ctx->error)))
        {
            result->error = strdup(ctx->error);
            sqlite3_finalize(stmt);
            return result;
        }
    }

    int col_count = sqlite3_column_count(stmt);
    result->column_count = col_count;

    if (col_count > 0)
    {
        result->column_names = (const char **)calloc(col_count, sizeof(char *));
        for (int i = 0; i < col_count; i++)
        {
            const char *name = sqlite3_column_name(stmt, i);
            result->column_names[i] = strdup(name ? name : "");
        }
    }

    int capacity = 64;
    result->rows = (SqliteRow *)calloc(capacity, sizeof(SqliteRow));
    result->row_count = 0;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        if (result->row_count >= capacity)
        {
            capacity *= 2;
            result->rows = (SqliteRow *)realloc(result->rows, capacity * sizeof(SqliteRow));
        }

        SqliteRow *row = &result->rows[result->row_count];
        row->column_count = col_count;
        row->column_names = result->column_names;
        row->values = (SqliteValue *)calloc(col_count, sizeof(SqliteValue));

        for (int i = 0; i < col_count; i++)
        {
            int col_type = sqlite3_column_type(stmt, i);
            switch (col_type)
            {
            case SQLITE_INTEGER:
                row->values[i].type = SQLITE_VAL_INTEGER;
                row->values[i].value.integer = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                row->values[i].type = SQLITE_VAL_FLOAT;
                row->values[i].value.real = sqlite3_column_double(stmt, i);
                break;
            case SQLITE_TEXT:
            {
                const char *text = (const char *)sqlite3_column_text(stmt, i);
                int len = sqlite3_column_bytes(stmt, i);
                row->values[i].type = SQLITE_VAL_TEXT;
                char *copy = (char *)malloc(len + 1);
                memcpy(copy, text, len);
                copy[len] = '\0';
                row->values[i].value.text.data = copy;
                row->values[i].value.text.length = len;
                break;
            }
            case SQLITE_BLOB:
                
                row->values[i].type = SQLITE_VAL_NULL;
                break;
            case SQLITE_NULL:
            default:
                row->values[i].type = SQLITE_VAL_NULL;
                break;
            }
        }

        result->row_count++;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        result->error = strdup(sqlite3_errmsg(ctx->dbs[handle]));
        snprintf(ctx->error, sizeof(ctx->error), "%s", result->error);
    }
    else
    {
        ctx->error[0] = '\0';
    }

    return result;
}

void sqlite_result_free(SqliteResult *result)
{
    if (!result)
        return;

    if (result->column_names)
    {
        for (int i = 0; i < result->column_count; i++)
        {
            free((void *)result->column_names[i]);
        }
        free(result->column_names);
    }

    for (int r = 0; r < result->row_count; r++)
    {
        SqliteRow *row = &result->rows[r];
        if (row->values)
        {
            for (int c = 0; c < row->column_count; c++)
            {
                if (row->values[c].type == SQLITE_VAL_TEXT)
                {
                    free((void *)row->values[c].value.text.data);
                }
            }
            free(row->values);
        }
    }

    free(result->rows);
    free(result->error);
    free(result);
}

const char *sqlite_get_error(SqliteContext *ctx)
{
    if (!ctx)
        return "";
    return ctx->error;
}

int64_t sqlite_last_insert_id(SqliteContext *ctx, int handle)
{
    if (!ctx || handle < 0 || handle >= SQLITE_MAX_DBS || !ctx->dbs[handle])
        return 0;
    return sqlite3_last_insert_rowid(ctx->dbs[handle]);
}