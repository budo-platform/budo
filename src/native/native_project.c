#include "native_project.h"

#include "budo/version.h"
#include "core/path_util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define mkdir_one(path) _mkdir(path)
#else
#include <unistd.h>
#define mkdir_one(path) mkdir((path), 0777)
#endif

#define NATIVE_MANIFEST_NAME "budo-native.json"
#define NATIVE_MANIFEST_MAX_SIZE (1024u * 1024u)

typedef struct JsonCursor
{
    const char *current;
    const char *end;
} JsonCursor;

static void set_error(NativeProjectError *error, const char *format, ...)
{
    va_list args;
    if (!error)
        return;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
}

static bool is_regular_file(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool is_directory_path(const char *path)
{
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool is_symbolic_link(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    struct stat info;
    return path && lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
#endif
}

static bool join_path(char *destination, size_t size,
                      const char *left, const char *right)
{
    int written = snprintf(destination, size, "%s/%s", left, right);
    return written >= 0 && (size_t)written < size;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif
    return slash ? slash + 1 : path;
}

static bool parent_directory(const char *path, char *destination, size_t size)
{
    const char *base = path_basename(path);
    size_t length;
    if (base == path)
        return snprintf(destination, size, ".") > 0;
    length = (size_t)(base - path - 1);
    if (length == 0)
        length = 1;
    if (length >= size)
        return false;
    memcpy(destination, path, length);
    destination[length] = '\0';
    return true;
}

static bool canonical_path(const char *path, char *destination, size_t size)
{
    size_t length;
#ifdef _WIN32
    char resolved_buffer[BUDO_NATIVE_PROJECT_MAX_PATH];
    char *resolved;
#else
    char *resolved;
#endif
    if (!path)
    {
        errno = EINVAL;
        return false;
    }
#ifdef _WIN32
    resolved = _fullpath(resolved_buffer, path, sizeof(resolved_buffer));
#else

    resolved = realpath(path, NULL);
#endif
    if (!resolved)
        return false;
    length = strlen(resolved);
    if (length >= size)
    {
#ifndef _WIN32
        free(resolved);
#endif
        errno = ENAMETOOLONG;
        return false;
    }
    memcpy(destination, resolved, length + 1);
#ifndef _WIN32
    free(resolved);
#endif
    return true;
}

static bool path_is_within(const char *root, const char *candidate)
{
    size_t length = strlen(root);
#ifdef _WIN32
    if (_strnicmp(root, candidate, length) != 0)
        return false;
#else
    if (strncmp(root, candidate, length) != 0)
        return false;
#endif
    return candidate[length] == '\0' || candidate[length] == '/' ||
           candidate[length] == '\\';
}

static bool manifest_path_is_safe(const char *path)
{
    const unsigned char *p = (const unsigned char *)path;
    if (!path_is_safe_relative(path))
        return false;
    while (*p)
    {
        if (*p < 0x20 || *p == '\\' || *p == ';' || *p == '$' || *p == '"')
            return false;
        p++;
    }
    return true;
}

static void cursor_skip_ws(JsonCursor *cursor)
{
    while (cursor->current < cursor->end &&
           isspace((unsigned char)*cursor->current))
        cursor->current++;
}

static bool cursor_take(JsonCursor *cursor, char value)
{
    cursor_skip_ws(cursor);
    if (cursor->current >= cursor->end || *cursor->current != value)
        return false;
    cursor->current++;
    return true;
}

static bool cursor_string(JsonCursor *cursor, char *destination, size_t size)
{
    size_t length = 0;
    cursor_skip_ws(cursor);
    if (cursor->current >= cursor->end || *cursor->current != '"' || size == 0)
        return false;
    cursor->current++;
    while (cursor->current < cursor->end && *cursor->current != '"')
    {
        unsigned char value = (unsigned char)*cursor->current++;
        if (value == '\\')
        {
            if (cursor->current >= cursor->end)
                return false;
            value = (unsigned char)*cursor->current++;
            switch (value)
            {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                value = '\b';
                break;
            case 'f':
                value = '\f';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            default:
                return false;
            }
        }
        if (value < 0x20 || length + 1 >= size)
            return false;
        destination[length++] = (char)value;
    }
    if (cursor->current >= cursor->end || *cursor->current != '"')
        return false;
    cursor->current++;
    destination[length] = '\0';
    return true;
}

static bool cursor_integer(JsonCursor *cursor, int *value)
{
    char *end = NULL;
    long parsed;
    cursor_skip_ws(cursor);
    errno = 0;
    parsed = strtol(cursor->current, &end, 10);
    if (errno || end == cursor->current || end > cursor->end)
        return false;
    if (end < cursor->end && !isspace((unsigned char)*end) &&
        *end != ',' && *end != '}')
        return false;
    *value = (int)parsed;
    cursor->current = end;
    return true;
}

static bool cursor_string_array(JsonCursor *cursor, char *items,
                                size_t *count, size_t item_size)
{
    if (!cursor_take(cursor, '['))
        return false;
    cursor_skip_ws(cursor);
    if (cursor_take(cursor, ']'))
        return true;
    for (;;)
    {
        if (*count >= BUDO_NATIVE_PROJECT_MAX_ITEMS ||
            !cursor_string(cursor, items + *count * item_size, item_size))
            return false;
        (*count)++;
        cursor_skip_ws(cursor);
        if (cursor_take(cursor, ']'))
            return true;
        if (!cursor_take(cursor, ','))
            return false;
    }
}

enum ManifestField
{
    FIELD_SCHEMA_VERSION,
    FIELD_NAME,
    FIELD_SOURCES,
    FIELD_INCLUDE_DIRECTORIES,
    FIELD_ASSETS,
    FIELD_DEFINITIONS,
    FIELD_MODULES,
    FIELD_C_STANDARD,
    FIELD_SDK_VERSION,
    FIELD_COUNT
};

static int manifest_field(const char *key)
{
    static const char *names[FIELD_COUNT] = {
        "schema_version", "name", "sources", "include_directories",
        "assets", "definitions", "modules", "c_standard", "sdk_version"};
    int index;
    for (index = 0; index < FIELD_COUNT; index++)
        if (strcmp(key, names[index]) == 0)
            return index;
    return -1;
}

static bool parse_manifest(const char *json, NativeProject *project,
                           NativeProjectError *error)
{
    JsonCursor cursor = {json, json + strlen(json)};
    bool seen[FIELD_COUNT] = {false};
    char key[64];
    if (!cursor_take(&cursor, '{'))
    {
        set_error(error, "Native manifest must contain one JSON object");
        return false;
    }
    cursor_skip_ws(&cursor);
    if (cursor_take(&cursor, '}'))
    {
        set_error(error, "Native manifest is empty");
        return false;
    }
    for (;;)
    {
        int field;
        if (!cursor_string(&cursor, key, sizeof(key)) || !cursor_take(&cursor, ':'))
        {
            set_error(error, "Malformed native manifest object");
            return false;
        }
        field = manifest_field(key);
        if (field < 0)
        {
            set_error(error, "Unknown native manifest field '%s'", key);
            return false;
        }
        if (seen[field])
        {
            set_error(error, "Duplicate native manifest field '%s'", key);
            return false;
        }
        seen[field] = true;
        switch (field)
        {
        case FIELD_SCHEMA_VERSION:
            if (!cursor_integer(&cursor, &project->schema_version))
                goto invalid_value;
            break;
        case FIELD_NAME:
            if (!cursor_string(&cursor, project->name, sizeof(project->name)))
                goto invalid_value;
            break;
        case FIELD_SOURCES:
            if (!cursor_string_array(&cursor, (char *)project->sources.items,
                                     &project->sources.count,
                                     sizeof(project->sources.items[0])))
                goto invalid_value;
            break;
        case FIELD_INCLUDE_DIRECTORIES:
            if (!cursor_string_array(&cursor, (char *)project->include_directories.items,
                                     &project->include_directories.count,
                                     sizeof(project->include_directories.items[0])))
                goto invalid_value;
            break;
        case FIELD_ASSETS:
            if (!cursor_string_array(&cursor, (char *)project->assets.items,
                                     &project->assets.count,
                                     sizeof(project->assets.items[0])))
                goto invalid_value;
            break;
        case FIELD_DEFINITIONS:
            if (!cursor_string_array(&cursor, (char *)project->definitions.items,
                                     &project->definitions.count,
                                     sizeof(project->definitions.items[0])))
                goto invalid_value;
            break;
        case FIELD_MODULES:
            if (!cursor_string_array(&cursor, (char *)project->modules.items,
                                     &project->modules.count,
                                     sizeof(project->modules.items[0])))
                goto invalid_value;
            break;
        case FIELD_C_STANDARD:
            if (!cursor_integer(&cursor, &project->c_standard))
                goto invalid_value;
            break;
        case FIELD_SDK_VERSION:
            if (!cursor_string(&cursor, project->sdk_version,
                               sizeof(project->sdk_version)))
                goto invalid_value;
            break;
        default:
            goto invalid_value;
        }
        cursor_skip_ws(&cursor);
        if (cursor_take(&cursor, '}'))
            break;
        if (!cursor_take(&cursor, ','))
        {
            set_error(error, "Expected ',' between native manifest fields");
            return false;
        }
    }
    cursor_skip_ws(&cursor);
    if (cursor.current != cursor.end)
    {
        set_error(error, "Unexpected data after native manifest object");
        return false;
    }
    if (!seen[FIELD_SCHEMA_VERSION] || !seen[FIELD_SOURCES])
    {
        set_error(error, "Native manifest requires schema_version and sources");
        return false;
    }
    return true;

invalid_value:
    set_error(error, "Invalid value for native manifest field '%s'", key);
    return false;
}

static bool definition_is_safe(const char *definition)
{
    const unsigned char *p = (const unsigned char *)definition;
    bool after_equals = false;
    if (!definition[0] || !(isalpha(*p) || *p == '_'))
        return false;
    for (; *p; p++)
    {
        if (*p == '=')
        {
            if (after_equals)
                return false;
            after_equals = true;
        }
        else if (!(isalnum(*p) || *p == '_' ||
                   (after_equals && (*p == '.' || *p == '+' || *p == '-' || *p == '/'))))
            return false;
    }
    return true;
}

static void make_output_name(const char *name, char *output, size_t size)
{
    size_t index = 0;
    const unsigned char *p = (const unsigned char *)name;
    while (*p && index + 1 < size)
    {
        if (isalnum(*p) || *p == '_' || *p == '-' || *p == '.')
            output[index++] = (char)*p;
        else if (index > 0 && output[index - 1] != '_')
            output[index++] = '_';
        p++;
    }
    while (index > 0 && output[index - 1] == '_')
        index--;
    output[index] = '\0';
    if (!output[0])
        snprintf(output, size, "budo-native-app");
}

static bool validate_path_list(const NativeProject *project,
                               const NativeProjectPathList *list,
                               const char *kind, bool require_file,
                               bool require_directory,
                               NativeProjectError *error)
{
    size_t index;
    for (index = 0; index < list->count; index++)
    {
        char joined[BUDO_NATIVE_PROJECT_MAX_PATH];
        char resolved[BUDO_NATIVE_PROJECT_MAX_PATH];
        const char *relative = list->items[index];
        if (!manifest_path_is_safe(relative))
        {
            set_error(error, "Invalid project-relative %s path '%s'", kind, relative);
            return false;
        }
        if (!join_path(joined, sizeof(joined), project->project_root, relative) ||
            !canonical_path(joined, resolved, sizeof(resolved)))
        {
            set_error(error, "%s path does not exist: '%s'", kind, relative);
            return false;
        }
        if (!path_is_within(project->project_root, resolved))
        {
            set_error(error, "%s path escapes the project root: '%s'", kind, relative);
            return false;
        }
        if ((require_file && !is_regular_file(resolved)) ||
            (require_directory && !is_directory_path(resolved)))
        {
            set_error(error, "%s path has the wrong type: '%s'", kind, relative);
            return false;
        }
    }
    return true;
}

static bool validate_project(NativeProject *project, NativeProjectError *error)
{
    size_t index;
    if (project->schema_version != BUDO_NATIVE_PROJECT_SCHEMA_VERSION)
    {
        set_error(error, "Unsupported native project schema_version %d (expected %d)",
                  project->schema_version, BUDO_NATIVE_PROJECT_SCHEMA_VERSION);
        return false;
    }
    if (project->sources.count == 0)
    {
        set_error(error, "Native project must declare at least one C source");
        return false;
    }
    if (project->c_standard != 11 && project->c_standard != 17 &&
        project->c_standard != 23)
    {
        set_error(error, "Unsupported C standard %d (use 11, 17, or 23)",
                  project->c_standard);
        return false;
    }
    if (!project->name[0])
        snprintf(project->name, sizeof(project->name), "%s",
                 path_basename(project->project_root));
    make_output_name(project->name, project->output_name,
                     sizeof(project->output_name));
    if (!project->sdk_version[0])
        snprintf(project->sdk_version, sizeof(project->sdk_version), "%s",
                 BUDO_VERSION_STRING);
    if (!validate_path_list(project, &project->sources, "source", true, false, error) ||
        !validate_path_list(project, &project->include_directories, "include directory",
                            false, true, error) ||
        !validate_path_list(project, &project->assets, "asset", false, false, error))
        return false;
    for (index = 0; index < project->sources.count; index++)
    {
        size_t length = strlen(project->sources.items[index]);
        if (length < 3 || strcmp(project->sources.items[index] + length - 2, ".c") != 0)
        {
            set_error(error, "Native source must use the .c extension: '%s'",
                      project->sources.items[index]);
            return false;
        }
    }
    for (index = 0; index < project->definitions.count; index++)
    {
        if (!definition_is_safe(project->definitions.items[index]))
        {
            set_error(error, "Invalid compile definition '%s'",
                      project->definitions.items[index]);
            return false;
        }
    }
    if (project->modules.count == 0)
    {
        project->modules.count = 1;
        snprintf(project->modules.items[0], sizeof(project->modules.items[0]), "core");
    }
    for (index = 0; index < project->modules.count; index++)
    {
        if (strcmp(project->modules.items[index], "core") != 0)
        {
            set_error(error, "Unsupported native module '%s'; Part 4 supports only 'core'",
                      project->modules.items[index]);
            return false;
        }
    }
    return true;
}

static char *read_text_file(const char *path, size_t maximum, NativeProjectError *error)
{
    FILE *file = fopen(path, "rb");
    char *contents;
    long length;
    size_t read_count;
    if (!file)
    {
        set_error(error, "Cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (size_t)length > maximum || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        set_error(error, "Cannot read native manifest '%s'", path);
        return NULL;
    }
    contents = malloc((size_t)length + 1);
    if (!contents)
    {
        fclose(file);
        set_error(error, "Out of memory reading native manifest");
        return NULL;
    }
    read_count = fread(contents, 1, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length)
    {
        free(contents);
        set_error(error, "Short read from native manifest '%s'", path);
        return NULL;
    }
    contents[length] = '\0';
    return contents;
}

bool native_project_load(const char *input_path, NativeProject *project,
                         NativeProjectError *error)
{
    char resolved[BUDO_NATIVE_PROJECT_MAX_PATH];
    char manifest[BUDO_NATIVE_PROJECT_MAX_PATH];
    char *json = NULL;
    if (error)
        error->message[0] = '\0';
    if (!input_path || !project)
    {
        set_error(error, "Native project input is required");
        return false;
    }
    memset(project, 0, sizeof(*project));
    project->schema_version = BUDO_NATIVE_PROJECT_SCHEMA_VERSION;
    project->c_standard = 11;
    snprintf(project->sdk_version, sizeof(project->sdk_version), "%s",
             BUDO_VERSION_STRING);
    if (!canonical_path(input_path, resolved, sizeof(resolved)))
    {
        set_error(error, "Cannot resolve native project '%s': %s",
                  input_path, strerror(errno));
        return false;
    }
    if (is_regular_file(resolved))
    {
        const char *base = path_basename(resolved);
        size_t length = strlen(base);
        if (length < 3 || strcmp(base + length - 2, ".c") != 0 ||
            !parent_directory(resolved, project->project_root,
                              sizeof(project->project_root)))
        {
            set_error(error, "Single-file native input must be a .c file");
            return false;
        }
        project->single_file = true;
        project->sources.count = 1;
        snprintf(project->sources.items[0], sizeof(project->sources.items[0]), "%s", base);
        snprintf(project->name, sizeof(project->name), "%.*s", (int)(length - 2), base);
        return validate_project(project, error);
    }
    if (!is_directory_path(resolved))
    {
        set_error(error, "Native project input is neither a directory nor a .c file");
        return false;
    }
    snprintf(project->project_root, sizeof(project->project_root), "%s", resolved);
    if (!join_path(manifest, sizeof(manifest), resolved, NATIVE_MANIFEST_NAME))
    {
        set_error(error, "Native project manifest path is too long");
        return false;
    }
    if (is_regular_file(manifest))
    {
        snprintf(project->manifest_path, sizeof(project->manifest_path), "%s", manifest);
        json = read_text_file(manifest, NATIVE_MANIFEST_MAX_SIZE, error);
        if (!json)
            return false;
        if (!parse_manifest(json, project, error))
        {
            free(json);
            return false;
        }
        free(json);
    }
    else
    {
        char main_source[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (!join_path(main_source, sizeof(main_source), resolved, "main.c") ||
            !is_regular_file(main_source))
        {
            set_error(error, "Native directory requires %s or main.c", NATIVE_MANIFEST_NAME);
            return false;
        }
        project->sources.count = 1;
        snprintf(project->sources.items[0], sizeof(project->sources.items[0]), "main.c");
        project->single_file = true;
    }
    return validate_project(project, error);
}

static bool make_directory_recursive(const char *path, NativeProjectError *error)
{
    char buffer[BUDO_NATIVE_PROJECT_MAX_PATH];
    char *p;
    if (!path || strlen(path) >= sizeof(buffer))
    {
        set_error(error, "Generated build directory path is too long");
        return false;
    }
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (p = buffer + 1; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            char saved = *p;
            *p = '\0';
            if (mkdir_one(buffer) != 0 && errno != EEXIST)
            {
                set_error(error, "Cannot create directory '%s': %s", buffer, strerror(errno));
                return false;
            }
            *p = saved;
        }
    }
    if (mkdir_one(buffer) != 0 && errno != EEXIST)
    {
        set_error(error, "Cannot create directory '%s': %s", buffer, strerror(errno));
        return false;
    }
    return true;
}

static void write_cmake_bracket(FILE *file, const char *value)
{
    fprintf(file, "[==[%s]==]", value);
}

static void write_json_string(FILE *file, const char *value)
{
    const unsigned char *p = (const unsigned char *)value;
    fputc('"', file);
    while (*p)
    {
        if (*p == '"' || *p == '\\')
            fputc('\\', file);
        if (*p == '\n')
            fputs("\\n", file);
        else
            fputc(*p, file);
        p++;
    }
    fputc('"', file);
}

static void write_json_path_list(FILE *file, const NativeProjectPathList *list)
{
    size_t index;
    fputc('[', file);
    for (index = 0; index < list->count; index++)
    {
        if (index)
            fputs(", ", file);
        write_json_string(file, list->items[index]);
    }
    fputc(']', file);
}

static void write_json_value_list(FILE *file, const NativeProjectValueList *list)
{
    size_t index;
    fputc('[', file);
    for (index = 0; index < list->count; index++)
    {
        if (index)
            fputs(", ", file);
        write_json_string(file, list->items[index]);
    }
    fputc(']', file);
}

static bool write_generated_cmake(const NativeProject *project, const char *path,
                                  const char *targets_file, NativeProjectError *error)
{
    FILE *file = fopen(path, "wb");
    size_t index;
    if (!file)
    {
        set_error(error, "Cannot write generated CMake project '%s': %s",
                  path, strerror(errno));
        return false;
    }
    fputs("cmake_minimum_required(VERSION 3.20)\n"
          "project(budo_generated_native C CXX)\n"
          "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
          "set(BUDO_NATIVE_SANITIZERS \"\" CACHE STRING \"Application sanitizers: address, undefined, or address,undefined\")\n"
          "if(BUDO_NATIVE_SANITIZERS)\n"
          "  if(NOT CMAKE_C_COMPILER_ID MATCHES \"Clang|GNU\")\n"
          "    message(FATAL_ERROR \"Native application sanitizers require Clang or GCC; selected compiler is ${CMAKE_C_COMPILER_ID}. Choose a supported compiler or omit --sanitize.\")\n"
          "  endif()\n"
          "  if(NOT BUDO_NATIVE_SANITIZERS MATCHES \"^(address|undefined|address,undefined)$\")\n"
          "    message(FATAL_ERROR \"Unsupported BUDO_NATIVE_SANITIZERS value: ${BUDO_NATIVE_SANITIZERS}\")\n"
          "  endif()\n"
          "endif()\n\n"
          "set(BUDO_NATIVE_PROJECT_ROOT ",
          file);
    write_cmake_bracket(file, project->project_root);
    fputs(")\nset(BUDO_NATIVE_SDK_INPUT ", file);
    write_cmake_bracket(file, targets_file);
    fputs(" CACHE PATH \"Budo native SDK package directory or development target manifest\")\n"
          "if(IS_DIRECTORY \"${BUDO_NATIVE_SDK_INPUT}\")\n"
          "  find_package(BudoNative CONFIG REQUIRED PATHS \"${BUDO_NATIVE_SDK_INPUT}\" NO_DEFAULT_PATH)\n"
          "else()\n"
          "set(BUDO_NATIVE_TARGETS_FILE \"${BUDO_NATIVE_SDK_INPUT}\")\n"
          "if(NOT EXISTS \"${BUDO_NATIVE_TARGETS_FILE}\")\n"
          "  message(FATAL_ERROR \"BUDO_NATIVE_TARGETS_FILE does not exist: ${BUDO_NATIVE_TARGETS_FILE}\")\n"
          "endif()\n"
          "find_package(Threads REQUIRED)\n"
          "find_package(SDL2 REQUIRED)\n"
          "if(UNIX AND NOT APPLE)\n"
          "  find_package(OpenGL REQUIRED)\n"
          "  find_package(Freetype REQUIRED)\n"
          "  find_package(Fontconfig REQUIRED)\n"
          "endif()\n"
          "set(BUDO_NATIVE_SKIA_LIBRARY \"\" CACHE FILEPATH \"Local SDK Skia library (required by checkout exports)\")\n"
          "if(BUDO_NATIVE_SKIA_LIBRARY AND NOT TARGET skia)\n"
          "  add_library(skia STATIC IMPORTED)\n"
          "  set_target_properties(skia PROPERTIES IMPORTED_LOCATION \"${BUDO_NATIVE_SKIA_LIBRARY}\")\n"
          "endif()\n"
          "include(\"${BUDO_NATIVE_TARGETS_FILE}\")\n"
          "endif()\n\n"
          "add_executable(budo_native_app)\n"
          "target_sources(budo_native_app PRIVATE\n",
          file);
    for (index = 0; index < project->sources.count; index++)
    {
        fputs("  \"${BUDO_NATIVE_PROJECT_ROOT}/", file);
        fputs(project->sources.items[index], file);
        fputs("\"\n", file);
    }
    fputs(")\n", file);
    fprintf(file, "set_property(TARGET budo_native_app PROPERTY C_STANDARD %d)\n",
            project->c_standard);
    fputs("set_property(TARGET budo_native_app PROPERTY C_STANDARD_REQUIRED ON)\n"
          "set_property(TARGET budo_native_app PROPERTY C_EXTENSIONS OFF)\n"
          "set_target_properties(budo_native_app PROPERTIES\n"
          "  OUTPUT_NAME ",
          file);
    write_cmake_bracket(file, project->output_name);
    fputs("\n  RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\"\n)\n", file);
    fputs("foreach(BUDO_CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)\n"
          "  set_property(TARGET budo_native_app PROPERTY \"RUNTIME_OUTPUT_DIRECTORY_${BUDO_CONFIG}\" \"${CMAKE_BINARY_DIR}/bin\")\n"
          "endforeach()\n",
          file);
    if (project->include_directories.count)
    {
        fputs("target_include_directories(budo_native_app PRIVATE\n", file);
        for (index = 0; index < project->include_directories.count; index++)
            fprintf(file, "  \"${BUDO_NATIVE_PROJECT_ROOT}/%s\"\n",
                    project->include_directories.items[index]);
        fputs(")\n", file);
    }
    if (project->definitions.count)
    {
        fputs("target_compile_definitions(budo_native_app PRIVATE\n", file);
        for (index = 0; index < project->definitions.count; index++)
            fprintf(file, "  %s\n", project->definitions.items[index]);
        fputs(")\n", file);
    }
    fputs("target_link_libraries(budo_native_app PRIVATE budo::native_launcher)\n"
          "if(BUDO_NATIVE_SANITIZERS)\n"
          "  string(REPLACE \",\" \";\" BUDO_SANITIZER_LIST \"${BUDO_NATIVE_SANITIZERS}\")\n"
          "  list(JOIN BUDO_SANITIZER_LIST \",\" BUDO_SANITIZER_FLAGS)\n"
          "  target_compile_options(budo_native_app PRIVATE \"-fsanitize=${BUDO_SANITIZER_FLAGS}\" -fno-omit-frame-pointer)\n"
          "  target_link_options(budo_native_app PRIVATE \"-fsanitize=${BUDO_SANITIZER_FLAGS}\")\n"
          "endif()\n"
          "if(COMMAND budo_native_configure_application)\n"
          "  budo_native_configure_application(budo_native_app)\n"
          "endif()\n",
          file);
    for (index = 0; index < project->assets.count; index++)
    {
        char joined[BUDO_NATIVE_PROJECT_MAX_PATH];
        join_path(joined, sizeof(joined), project->project_root,
                  project->assets.items[index]);
        if (is_directory_path(joined))
        {
            fprintf(file,
                    "add_custom_command(TARGET budo_native_app POST_BUILD\n"
                    "  COMMAND ${CMAKE_COMMAND} -E make_directory \"$<TARGET_FILE_DIR:budo_native_app>/%s\"\n"
                    "  COMMAND ${CMAKE_COMMAND} -E copy_directory \"${BUDO_NATIVE_PROJECT_ROOT}/%s\" \"$<TARGET_FILE_DIR:budo_native_app>/%s\"\n)\n",
                    project->assets.items[index], project->assets.items[index],
                    project->assets.items[index]);
        }
        else
        {
            char parent[BUDO_NATIVE_PROJECT_MAX_PATH];
            parent_directory(project->assets.items[index], parent, sizeof(parent));
            fprintf(file,
                    "add_custom_command(TARGET budo_native_app POST_BUILD\n"
                    "  COMMAND ${CMAKE_COMMAND} -E make_directory \"$<TARGET_FILE_DIR:budo_native_app>/%s\"\n"
                    "  COMMAND ${CMAKE_COMMAND} -E copy_if_different \"${BUDO_NATIVE_PROJECT_ROOT}/%s\" \"$<TARGET_FILE_DIR:budo_native_app>/%s\"\n)\n",
                    strcmp(parent, ".") == 0 ? "" : parent,
                    project->assets.items[index], project->assets.items[index]);
        }
    }
    if (fclose(file) != 0)
    {
        set_error(error, "Cannot finish generated CMake project '%s'", path);
        return false;
    }
    return true;
}

static bool write_project_model(const NativeProject *project, const char *path,
                                const char *targets_file, NativeProjectError *error)
{
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        set_error(error, "Cannot write generated project model '%s': %s",
                  path, strerror(errno));
        return false;
    }
    fprintf(file, "{\n  \"schema_version\": %d,\n  \"name\": ", project->schema_version);
    write_json_string(file, project->name);
    fputs(",\n  \"output_name\": ", file);
    write_json_string(file, project->output_name);
    fputs(",\n  \"project_root\": ", file);
    write_json_string(file, project->project_root);
    fputs(",\n  \"manifest\": ", file);
    write_json_string(file, project->manifest_path);
    fprintf(file, ",\n  \"single_file\": %s,\n  \"c_standard\": %d,\n  \"sdk_version\": ",
            project->single_file ? "true" : "false", project->c_standard);
    write_json_string(file, project->sdk_version);
    fputs(",\n  \"native_targets_file\": ", file);
    write_json_string(file, targets_file);
    fputs(",\n  \"sources\": ", file);
    write_json_path_list(file, &project->sources);
    fputs(",\n  \"include_directories\": ", file);
    write_json_path_list(file, &project->include_directories);
    fputs(",\n  \"assets\": ", file);
    write_json_path_list(file, &project->assets);
    fputs(",\n  \"definitions\": ", file);
    write_json_value_list(file, &project->definitions);
    fputs(",\n  \"modules\": ", file);
    write_json_value_list(file, &project->modules);
    fputs(",\n  \"runtime_output\": ", file);
    {
        char output[BUDO_NATIVE_PROJECT_MAX_PATH];
        snprintf(output, sizeof(output), "bin/%s", project->output_name);
        write_json_string(file, output);
    }
    fputs("\n}\n", file);
    if (fclose(file) != 0)
    {
        set_error(error, "Cannot finish generated project model '%s'", path);
        return false;
    }
    return true;
}

bool native_project_generate(const NativeProject *project,
                             const char *generated_directory,
                             const char *native_targets_file,
                             NativeProjectError *error)
{
    char cmake_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char model_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    if (error)
        error->message[0] = '\0';
    if (!project || !generated_directory || !native_targets_file ||
        !native_targets_file[0])
    {
        set_error(error, "Project, generated directory, and native target manifest are required");
        return false;
    }
    if (!is_regular_file(native_targets_file) && !is_directory_path(native_targets_file))
    {
        set_error(error, "Native SDK input does not exist: '%s'", native_targets_file);
        return false;
    }
    if (!make_directory_recursive(generated_directory, error) ||
        !join_path(cmake_path, sizeof(cmake_path), generated_directory, "CMakeLists.txt") ||
        !join_path(model_path, sizeof(model_path), generated_directory,
                   "budo-native-project.json"))
        return false;
    return write_generated_cmake(project, cmake_path, native_targets_file, error) &&
           write_project_model(project, model_path, native_targets_file, error);
}

static bool copy_file_if_different(const char *source, const char *destination,
                                   NativeProjectError *error)
{
    FILE *input = fopen(source, "rb");
    FILE *output;
    unsigned char buffer[16384];
    size_t amount;
    char parent[BUDO_NATIVE_PROJECT_MAX_PATH];
    if (!input)
    {
        set_error(error, "Cannot read asset '%s': %s", source, strerror(errno));
        return false;
    }
    if (!parent_directory(destination, parent, sizeof(parent)) ||
        !make_directory_recursive(parent, error))
    {
        fclose(input);
        return false;
    }
    output = fopen(destination, "wb");
    if (!output)
    {
        set_error(error, "Cannot write staged asset '%s': %s", destination,
                  strerror(errno));
        fclose(input);
        return false;
    }
    while ((amount = fread(buffer, 1, sizeof(buffer), input)) != 0)
    {
        if (fwrite(buffer, 1, amount, output) != amount)
        {
            set_error(error, "Cannot finish staged asset '%s'", destination);
            fclose(input);
            fclose(output);
            return false;
        }
    }
    if (ferror(input) || fclose(input) != 0 || fclose(output) != 0)
    {
        set_error(error, "Cannot finish staged asset '%s'", destination);
        return false;
    }
    return true;
}

static bool has_native_binary_suffix(const char *name)
{
    static const char *suffixes[] = {".a", ".so", ".o", ".obj", ".dylib", ".dll", ".lib"};
    size_t name_length = strlen(name);
    size_t index;
    for (index = 0; index < sizeof(suffixes) / sizeof(suffixes[0]); index++)
    {
        size_t suffix_length = strlen(suffixes[index]);
        if (name_length >= suffix_length &&
            strcmp(name + name_length - suffix_length, suffixes[index]) == 0)
            return true;
    }
    return false;
}

static bool copy_build_tree(const char *source, const char *destination,
                            NativeProjectError *error)
{
    struct stat info;
    DIR *directory;
    struct dirent *entry;
    if (is_symbolic_link(source))
    {
        set_error(error, "Native build inputs may not contain symlinks: '%s'", source);
        return false;
    }
    if (stat(source, &info) != 0)
    {
        set_error(error, "Cannot inspect native build input '%s': %s",
                  source, strerror(errno));
        return false;
    }
    if (S_ISREG(info.st_mode))
    {
        if (has_native_binary_suffix(path_basename(source)))
        {
            set_error(error, "Prebuilt native files are not supported: '%s'", source);
            return false;
        }
        return copy_file_if_different(source, destination, error);
    }
    if (!S_ISDIR(info.st_mode) || !make_directory_recursive(destination, error))
    {
        set_error(error, "Unsupported native build input type: '%s'", source);
        return false;
    }
    directory = opendir(source);
    if (!directory)
    {
        set_error(error, "Cannot read native include directory '%s': %s",
                  source, strerror(errno));
        return false;
    }
    while ((entry = readdir(directory)) != NULL)
    {
        char child_source[BUDO_NATIVE_PROJECT_MAX_PATH];
        char child_destination[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!join_path(child_source, sizeof(child_source), source, entry->d_name) ||
            !join_path(child_destination, sizeof(child_destination), destination,
                       entry->d_name) ||
            !copy_build_tree(child_source, child_destination, error))
        {
            closedir(directory);
            return false;
        }
    }
    closedir(directory);
    return true;
}

static bool write_android_cmake(const NativeProject *project, const char *path,
                                NativeProjectError *error)
{
    FILE *file = fopen(path, "wb");
    size_t index;
    if (!file)
    {
        set_error(error, "Cannot write Android native source manifest '%s': %s",
                  path, strerror(errno));
        return false;
    }
    fputs("# Generated by Budo; do not edit.\nset(BUDO_HAS_NATIVE_APP ON)\n"
          "set(BUDO_NATIVE_APP_SOURCES\n",
          file);
    for (index = 0; index < project->sources.count; index++)
    {
        fputs("  \"${CMAKE_CURRENT_LIST_DIR}/", file);
        fputs(project->sources.items[index], file);
        fputs("\"\n", file);
    }
    fputs(")\nset(BUDO_NATIVE_APP_INCLUDE_DIRECTORIES\n", file);
    for (index = 0; index < project->include_directories.count; index++)
    {
        fputs("  \"${CMAKE_CURRENT_LIST_DIR}/", file);
        fputs(project->include_directories.items[index], file);
        fputs("\"\n", file);
    }
    fputs(")\nset(BUDO_NATIVE_APP_DEFINITIONS\n", file);
    for (index = 0; index < project->definitions.count; index++)
    {
        fputs("  ", file);
        write_cmake_bracket(file, project->definitions.items[index]);
        fputc('\n', file);
    }
    fprintf(file, ")\nset(BUDO_NATIVE_APP_C_STANDARD %d)\nset(BUDO_NATIVE_APP_NAME ",
            project->c_standard);
    write_cmake_bracket(file, project->name);
    fputs(")\n", file);
    if (fclose(file) != 0)
    {
        set_error(error, "Cannot finish Android native source manifest '%s'", path);
        return false;
    }
    return true;
}

bool native_project_generate_android(const NativeProject *project,
                                     const char *staging_directory,
                                     NativeProjectError *error)
{
    char manifest_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    size_t index;
    if (error)
        error->message[0] = '\0';
    if (!project || !staging_directory || !staging_directory[0] ||
        !make_directory_recursive(staging_directory, error))
    {
        if (!project || !staging_directory || !staging_directory[0])
            set_error(error, "Native project and Android staging directory are required");
        return false;
    }
    for (index = 0; index < project->sources.count; index++)
    {
        char source[BUDO_NATIVE_PROJECT_MAX_PATH];
        char destination[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (!join_path(source, sizeof(source), project->project_root,
                       project->sources.items[index]) ||
            !join_path(destination, sizeof(destination), staging_directory,
                       project->sources.items[index]) ||
            !copy_build_tree(source, destination, error))
            return false;
    }
    for (index = 0; index < project->include_directories.count; index++)
    {
        char source[BUDO_NATIVE_PROJECT_MAX_PATH];
        char destination[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (!join_path(source, sizeof(source), project->project_root,
                       project->include_directories.items[index]) ||
            !join_path(destination, sizeof(destination), staging_directory,
                       project->include_directories.items[index]) ||
            !copy_build_tree(source, destination, error))
            return false;
    }
    if (!join_path(manifest_path, sizeof(manifest_path), staging_directory,
                   "budo-native-sources.cmake"))
    {
        set_error(error, "Android native source manifest path is too long");
        return false;
    }
    return write_android_cmake(project, manifest_path, error);
}

static bool copy_asset_tree(const char *source, const char *destination,
                            NativeProjectError *error)
{
    struct stat info;
    DIR *directory;
    struct dirent *entry;
    if (is_symbolic_link(source))
    {
        set_error(error, "Native assets may not contain symlinks: '%s'", source);
        return false;
    }
    if (stat(source, &info) != 0)
    {
        set_error(error, "Cannot inspect asset '%s': %s", source, strerror(errno));
        return false;
    }
    if (S_ISREG(info.st_mode))
        return copy_file_if_different(source, destination, error);
    if (!S_ISDIR(info.st_mode))
    {
        set_error(error, "Unsupported native asset type: '%s'", source);
        return false;
    }
    if (!make_directory_recursive(destination, error))
        return false;
    directory = opendir(source);
    if (!directory)
    {
        set_error(error, "Cannot read asset directory '%s': %s", source,
                  strerror(errno));
        return false;
    }
    while ((entry = readdir(directory)) != NULL)
    {
        char child_source[BUDO_NATIVE_PROJECT_MAX_PATH];
        char child_destination[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!join_path(child_source, sizeof(child_source), source, entry->d_name) ||
            !join_path(child_destination, sizeof(child_destination), destination,
                       entry->d_name) ||
            !copy_asset_tree(child_source, child_destination, error))
        {
            closedir(directory);
            return false;
        }
    }
    closedir(directory);
    return true;
}

bool native_project_stage_assets(const NativeProject *project,
                                 const char *runtime_directory,
                                 NativeProjectError *error)
{
    size_t index;
    if (error)
        error->message[0] = '\0';
    for (index = 0; index < project->assets.count; index++)
    {
        char source[BUDO_NATIVE_PROJECT_MAX_PATH];
        char destination[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (!join_path(source, sizeof(source), project->project_root,
                       project->assets.items[index]) ||
            !join_path(destination, sizeof(destination), runtime_directory,
                       project->assets.items[index]) ||
            !copy_asset_tree(source, destination, error))
            return false;
    }
    return true;
}