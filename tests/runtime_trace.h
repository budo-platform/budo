#ifndef TESTS_RUNTIME_TRACE_H
#define TESTS_RUNTIME_TRACE_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_TRACE_RESULT_CAPACITY 256

typedef struct RuntimeTraceRecord
{
    const char *operation;
    char result[RUNTIME_TRACE_RESULT_CAPACITY];
    char error_kind[32];
    char error_code[64];
} RuntimeTraceRecord;

typedef struct RuntimeTrace
{
    RuntimeTraceRecord *records;
    size_t count;
} RuntimeTrace;

static inline void runtime_trace_init(RuntimeTrace *trace,
                                      RuntimeTraceRecord *records,
                                      const char *const *operations,
                                      size_t count)
{
    memset(records, 0, sizeof(*records) * count);
    trace->records = records;
    trace->count = count;
    for (size_t index = 0; index < count; index++)
    {
        records[index].operation = operations[index];
        strcpy(records[index].result, "null");
        strcpy(records[index].error_kind, "none");
    }
}

static inline void runtime_trace_result(RuntimeTrace *trace, size_t index,
                                        const char *json)
{
    assert(index < trace->count);
    assert(strlen(json) < sizeof(trace->records[index].result));
    strcpy(trace->records[index].result, json);
}

static inline void runtime_trace_error(RuntimeTrace *trace, size_t index,
                                       const char *kind, const char *code)
{
    assert(index < trace->count);
    assert(strlen(kind) < sizeof(trace->records[index].error_kind));
    assert(strlen(code) < sizeof(trace->records[index].error_code));
    strcpy(trace->records[index].result, "null");
    strcpy(trace->records[index].error_kind, kind);
    strcpy(trace->records[index].error_code, code);
}

static inline void runtime_trace_print(const char *runtime,
                                       const RuntimeTraceRecord *record)
{
    printf("{\"runtime\":\"%s\",\"operation\":\"%s\","
           "\"result\":%s,\"errorKind\":\"%s\","
           "\"errorCode\":\"%s\"}\n",
           runtime, record->operation, record->result,
           record->error_kind, record->error_code);
}

static inline void runtime_trace_compare(const char *expected_runtime,
                                         const RuntimeTrace *expected,
                                         const char *actual_runtime,
                                         const RuntimeTrace *actual)
{
    assert(expected->count == actual->count);
    for (size_t index = 0; index < expected->count; index++)
    {
        const RuntimeTraceRecord *left = &expected->records[index];
        const RuntimeTraceRecord *right = &actual->records[index];
        if (strcmp(left->operation, right->operation) != 0 ||
            strcmp(left->result, right->result) != 0 ||
            strcmp(left->error_kind, right->error_kind) != 0 ||
            strcmp(left->error_code, right->error_code) != 0)
        {
            fprintf(stderr, "Conformance mismatch between %s and %s:\n",
                    expected_runtime, actual_runtime);
            runtime_trace_print(expected_runtime, left);
            runtime_trace_print(actual_runtime, right);
            abort();
        }
    }
}

#endif