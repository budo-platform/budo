#include "core/api_error.h"

#include <stdio.h>
#include <string.h>

_Static_assert(API_STATUS_OK == 0, "API_STATUS_OK value changed");
_Static_assert(API_STATUS_INVALID_ARGUMENT == 1,
               "API_STATUS_INVALID_ARGUMENT value changed");
_Static_assert(API_STATUS_INCOMPATIBLE_API == 2,
               "API_STATUS_INCOMPATIBLE_API value changed");
_Static_assert(API_STATUS_UNSUPPORTED == 3,
               "API_STATUS_UNSUPPORTED value changed");
_Static_assert(API_STATUS_OUT_OF_MEMORY == 4,
               "API_STATUS_OUT_OF_MEMORY value changed");
_Static_assert(API_STATUS_APPLICATION_ERROR == 5,
               "API_STATUS_APPLICATION_ERROR value changed");
_Static_assert(API_STATUS_INTERNAL_ERROR == 6,
               "API_STATUS_INTERNAL_ERROR value changed");
_Static_assert(API_STATUS_INVALID_STATE == 7,
               "API_STATUS_INVALID_STATE value changed");
_Static_assert(API_ERROR_CODE_CAPACITY == 64,
               "API error code capacity changed");
_Static_assert(API_ERROR_MESSAGE_CAPACITY == 256,
               "API error message capacity changed");
_Static_assert(sizeof(((ApiError *)0)->code) == API_ERROR_CODE_CAPACITY,
               "ApiError code must use inline storage");
_Static_assert(sizeof(((ApiError *)0)->message) == API_ERROR_MESSAGE_CAPACITY,
               "ApiError message must use inline storage");

typedef struct StatusCase
{
    ApiStatus status;
    const char *name;
} StatusCase;

typedef struct GuardedError
{
    unsigned char before[8];
    ApiError error;
    unsigned char after[8];
} GuardedError;

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "api_error_test: %s\n", message);
    return 1;
}

static int check_bytes(const unsigned char *bytes, size_t size,
                       unsigned char expected)
{
    size_t index;

    for (index = 0; index < size; index++)
    {
        if (bytes[index] != expected)
            return 0;
    }
    return 1;
}

static int test_status_names(void)
{
    static const StatusCase cases[] = {
        {API_STATUS_OK, "ok"},
        {API_STATUS_INVALID_ARGUMENT, "invalid_argument"},
        {API_STATUS_INCOMPATIBLE_API, "incompatible_api"},
        {API_STATUS_UNSUPPORTED, "unsupported"},
        {API_STATUS_OUT_OF_MEMORY, "out_of_memory"},
        {API_STATUS_APPLICATION_ERROR, "application_error"},
        {API_STATUS_INTERNAL_ERROR, "internal_error"},
        {API_STATUS_INVALID_STATE, "invalid_state"},
    };
    size_t index;
    int failed = 0;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
        failed |= check(strcmp(api_status_name(cases[index].status),
                               cases[index].name) == 0,
                        "status name differs");
    }
    failed |= check(strcmp(api_status_name((ApiStatus)-1), "unknown") == 0,
                    "negative status was not reported as unknown");
    failed |= check(strcmp(api_status_name((ApiStatus)99), "unknown") == 0,
                    "out-of-range status was not reported as unknown");
    return failed;
}

static int test_clear_and_set(void)
{
    ApiError error;
    char code[] = "sqlite.open_failed";
    char message[] = "Could not open the database";
    int failed = 0;

    memset(&error, 0xa5, sizeof(error));
    api_error_clear(&error);
    failed |= check(error.status == API_STATUS_OK,
                    "clear did not set the OK status");
    failed |= check(error.code[0] == '\0' && error.message[0] == '\0',
                    "clear did not empty diagnostic text");
    failed |= check(check_bytes((const unsigned char *)&error,
                                sizeof(error), 0),
                    "clear did not reset the complete value");
    failed |= check(!api_error_has_error(&error),
                    "a cleared value reports an error");

    api_error_set(&error, API_STATUS_APPLICATION_ERROR, code, message);
    code[0] = 'X';
    message[0] = 'X';
    failed |= check(error.status == API_STATUS_APPLICATION_ERROR,
                    "set did not preserve the status");
    failed |= check(strcmp(error.code, "sqlite.open_failed") == 0,
                    "code storage is not caller-owned");
    failed |= check(strcmp(error.message, "Could not open the database") == 0,
                    "message storage is not caller-owned");
    failed |= check(api_error_has_error(&error),
                    "a non-OK value does not report an error");

    api_error_set(&error, API_STATUS_INVALID_ARGUMENT, NULL, NULL);
    failed |= check(error.code[0] == '\0' && error.message[0] == '\0',
                    "NULL diagnostic text did not become empty text");

    api_error_set(&error, API_STATUS_OK, "ignored.code", "ignored message");
    failed |= check(error.status == API_STATUS_OK && error.code[0] == '\0' &&
                        error.message[0] == '\0',
                    "setting OK did not normalize to a clear value");
    failed |= check(check_bytes((const unsigned char *)&error,
                                sizeof(error), 0),
                    "setting OK did not reset the complete value");

    api_error_clear(NULL);
    api_error_set(NULL, API_STATUS_INTERNAL_ERROR, "ignored", "ignored");
    failed |= check(!api_error_has_error(NULL),
                    "a NULL error value reports an error");
    return failed;
}

static int test_truncation(void)
{
    GuardedError guarded;
    char code[API_ERROR_CODE_CAPACITY + 17];
    char message[API_ERROR_MESSAGE_CAPACITY + 17];
    size_t index;
    int failed = 0;

    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0x3c, sizeof(guarded.before));
    memset(guarded.after, 0xc3, sizeof(guarded.after));
    memset(code, 'c', sizeof(code) - 1);
    code[sizeof(code) - 1] = '\0';
    memset(message, 'm', sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';

    api_error_set(&guarded.error, API_STATUS_INTERNAL_ERROR, code, message);
    failed |= check(strlen(guarded.error.code) == API_ERROR_CODE_CAPACITY - 1,
                    "long code was not truncated at capacity");
    failed |= check(strlen(guarded.error.message) ==
                        API_ERROR_MESSAGE_CAPACITY - 1,
                    "long message was not truncated at capacity");
    for (index = 0; index < API_ERROR_CODE_CAPACITY - 1; index++)
        failed |= check(guarded.error.code[index] == 'c',
                        "truncated code content differs");
    for (index = 0; index < API_ERROR_MESSAGE_CAPACITY - 1; index++)
        failed |= check(guarded.error.message[index] == 'm',
                        "truncated message content differs");
    failed |= check(check_bytes(guarded.before, sizeof(guarded.before), 0x3c) &&
                        check_bytes(guarded.after, sizeof(guarded.after), 0xc3),
                    "set wrote outside the caller-owned value");
    return failed;
}

int main(void)
{
    int failed = 0;

    failed |= test_status_names();
    failed |= test_clear_and_set();
    failed |= test_truncation();
    if (failed)
        return 1;

    puts("api_error_test: ok");
    return 0;
}