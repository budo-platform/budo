#include "native_compile.h"

#include "budo/version.h"
#include "native/native_build_config.h"
#include "native/native_cache.h"
#include "native/native_project.h"
#include "native/native_sdk.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define BUDO_EXECUTABLE_SUFFIX ".exe"
#else
#include <sys/wait.h>
#include <unistd.h>
#define BUDO_EXECUTABLE_SUFFIX ""
#endif

static bool regular_file_exists(const char *path)
{
    struct stat info;
    if (!path || !path[0] || stat(path, &info) != 0)
        return false;
#ifdef _WIN32
    return (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    return S_ISREG(info.st_mode);
#endif
}

static bool resolve_command(const char *command, char *output, size_t size)
{
    const char *path;
    char *copy, *part;
    if (strchr(command, '/') || strchr(command, '\\'))
    {
        if (!regular_file_exists(command) || strlen(command) >= size)
            return false;
        snprintf(output, size, "%s", command);
        return true;
    }
    path = getenv("PATH");
    if (!path || !(copy = malloc(strlen(path) + 1)))
        return false;
    strcpy(copy, path);
    part = strtok(copy,
#ifdef _WIN32
                  ";"
#else
                  ":"
#endif
    );
    while (part)
    {
        char candidate[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (snprintf(candidate, sizeof(candidate), "%s/%s", part, command) <
                (int)sizeof(candidate) &&
            regular_file_exists(candidate))
        {
            snprintf(output, size, "%s", candidate);
            free(copy);
            return true;
        }
#ifdef _WIN32
        if (snprintf(candidate, sizeof(candidate), "%s/%s.exe", part, command) <
                (int)sizeof(candidate) &&
            regular_file_exists(candidate))
        {
            snprintf(output, size, "%s", candidate);
            free(copy);
            return true;
        }
#endif
        part = strtok(NULL,
#ifdef _WIN32
                      ";"
#else
                      ":"
#endif
        );
    }
    free(copy);
    return false;
}

static bool host_tool(const char *environment_name, const char *fallback,
                      char *output, size_t size)
{
    const char *value = getenv(environment_name);
    return resolve_command(value && value[0] ? value : fallback, output, size);
}

static int run_process_in_directory(const char *const arguments[],
                                    const char *working_directory)
{
    size_t index;

    if (working_directory)
        fprintf(stderr, "+ (cd %s &&", working_directory);
    else
        fprintf(stderr, "+");
    for (index = 0; arguments[index]; index++)
        fprintf(stderr, " %s", arguments[index]);
    if (working_directory)
        fputc(')', stderr);
    fputc('\n', stderr);
    fflush(stderr);

#ifdef _WIN32
    {
        char previous_directory[BUDO_NATIVE_PROJECT_MAX_PATH];
        intptr_t result;
        if (working_directory)
        {
            int spawn_error;
            if (!_getcwd(previous_directory, sizeof(previous_directory)) ||
                _chdir(working_directory) != 0)
            {
                fprintf(stderr, "Error: cannot enter native runtime directory '%s': %s\n",
                        working_directory, strerror(errno));
                return 127;
            }
            result = _spawnvp(_P_WAIT, arguments[0],
                              (const char *const *)arguments);
            spawn_error = errno;
            if (_chdir(previous_directory) != 0)
                fprintf(stderr, "Warning: cannot restore working directory '%s': %s\n",
                        previous_directory, strerror(errno));
            errno = spawn_error;
        }
        else
            result = _spawnvp(_P_WAIT, arguments[0],
                              (const char *const *)arguments);
        if (result == -1)
        {
            fprintf(stderr, "Error: cannot execute '%s': %s\n",
                    arguments[0], strerror(errno));
            return 127;
        }
        return (int)result;
    }
#else
    {
        pid_t child = fork();
        int status;
        if (child < 0)
        {
            fprintf(stderr, "Error: cannot start '%s': %s\n",
                    arguments[0], strerror(errno));
            return 127;
        }
        if (child == 0)
        {
            if (working_directory && chdir(working_directory) != 0)
            {
                fprintf(stderr, "Error: cannot enter native runtime directory '%s': %s\n",
                        working_directory, strerror(errno));
                _exit(127);
            }
            execvp(arguments[0], (char *const *)arguments);
            fprintf(stderr, "Error: cannot execute '%s': %s\n",
                    arguments[0], strerror(errno));
            _exit(127);
        }
        while (waitpid(child, &status, 0) < 0)
        {
            if (errno != EINTR)
                return 127;
        }
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            fprintf(stderr, "Error: '%s' terminated by signal %d\n",
                    arguments[0], WTERMSIG(status));
        return 128;
    }
#endif
}

static int run_process(const char *const arguments[])
{
    return run_process_in_directory(arguments, NULL);
}

static void write_json_string(FILE *file, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    fputc('"', file);
    while (*cursor)
    {
        if (*cursor == '"' || *cursor == '\\')
            fputc('\\', file);
        if (*cursor == '\n')
            fputs("\\n", file);
        else
            fputc(*cursor, file);
        cursor++;
    }
    fputc('"', file);
}

static void hash_string(NativeCacheHash *hash, const char *name, const char *value)
{
    native_cache_hash_component(hash, name, value, strlen(value));
}

static bool hash_project(const NativeProject *project,
                         char digest[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    NativeCacheHash hash;
    size_t index;
    char value[64];
    native_cache_hash_init(&hash);
    hash_string(&hash, "domain", "budo-native-project-input-v1");
    hash_string(&hash, "project-root", project->project_root);
    hash_string(&hash, "name", project->name);
    hash_string(&hash, "output", project->output_name);
    hash_string(&hash, "sdk-version", project->sdk_version);
    snprintf(value, sizeof(value), "%d", project->c_standard);
    hash_string(&hash, "c-standard", value);
    if (project->manifest_path[0] &&
        !native_cache_hash_file_component(&hash, "manifest", "budo-native.json",
                                          project->manifest_path))
        return false;
    for (index = 0; index < project->sources.count; index++)
    {
        char path[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (snprintf(path, sizeof(path), "%s/%s", project->project_root,
                     project->sources.items[index]) >= (int)sizeof(path) ||
            !native_cache_hash_file_component(&hash, "source",
                                              project->sources.items[index], path))
            return false;
    }

    for (index = 0; index < project->sources.count; index++)
    {
        char relative[BUDO_NATIVE_PROJECT_MAX_PATH];
        char directory[BUDO_NATIVE_PROJECT_MAX_PATH];
        char *slash;
        snprintf(relative, sizeof(relative), "%s", project->sources.items[index]);
        slash = strrchr(relative, '/');
        if (slash)
            *slash = '\0';
        else
            snprintf(relative, sizeof(relative), ".");
        if (snprintf(directory, sizeof(directory), "%s/%s", project->project_root,
                     relative) >= (int)sizeof(directory) ||
            !native_cache_hash_header_tree_component(&hash, relative, directory))
            return false;
    }
    for (index = 0; index < project->include_directories.count; index++)
    {
        char path[BUDO_NATIVE_PROJECT_MAX_PATH];
        if (snprintf(path, sizeof(path), "%s/%s", project->project_root,
                     project->include_directories.items[index]) >= (int)sizeof(path) ||
            !native_cache_hash_tree_component(&hash, "include-tree",
                                              project->include_directories.items[index],
                                              path))
            return false;
    }
    for (index = 0; index < project->definitions.count; index++)
        hash_string(&hash, "definition", project->definitions.items[index]);
    for (index = 0; index < project->modules.count; index++)
        hash_string(&hash, "module", project->modules.items[index]);
    native_cache_hash_finish(&hash, digest);
    return true;
}

static bool hash_sdk(const char *sdk_directory,
                     char digest[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    NativeCacheHash hash;
    FILE *targets;
    char *contents = NULL;
    long length;
    char *cursor;
    bool ok = true;
    native_cache_hash_init(&hash);
    if (sdk_directory)
    {
        char manifest[BUDO_NATIVE_PROJECT_MAX_PATH];
        hash_string(&hash, "domain", "budo-native-relocatable-sdk-v1");
        if (snprintf(manifest, sizeof(manifest), "%s/share/budo/budo-native-sdk.json",
                     sdk_directory) >= (int)sizeof(manifest) ||
            !native_cache_hash_file_component(&hash, "manifest",
                                              "share/budo/budo-native-sdk.json",
                                              manifest) ||
            !native_cache_hash_tree_component(&hash, "sdk-tree", "sdk", sdk_directory))
            return false;
        native_cache_hash_finish(&hash, digest);
        return true;
    }
    hash_string(&hash, "domain", "budo-native-local-sdk-v1");
    hash_string(&hash, "version", BUDO_VERSION_STRING);
    hash_string(&hash, "build-id", BUDO_NATIVE_SDK_BUILD_ID);
    if (!native_cache_hash_file_component(&hash, "targets", "BudoNativeTargets.cmake",
                                          BUDO_LOCAL_NATIVE_TARGETS_FILE) ||
        !native_cache_hash_file_component(&hash, "skia", "skia",
                                          BUDO_LOCAL_SKIA_LIBRARY))
        return false;
    targets = fopen(BUDO_LOCAL_NATIVE_TARGETS_FILE, "rb");
    if (!targets || fseek(targets, 0, SEEK_END) != 0 ||
        (length = ftell(targets)) < 0 || fseek(targets, 0, SEEK_SET) != 0)
    {
        if (targets)
            fclose(targets);
        return false;
    }
    contents = malloc((size_t)length + 1);
    if (!contents || fread(contents, 1, (size_t)length, targets) != (size_t)length)
        ok = false;
    fclose(targets);
    if (!ok)
    {
        free(contents);
        return false;
    }
    contents[length] = '\0';

    cursor = contents;
    while ((cursor = strchr(cursor, '/')) != NULL)
    {
        char candidate[BUDO_NATIVE_PROJECT_MAX_PATH];
        size_t size = 0;
        struct stat info;
        while (cursor[size] && cursor[size] != ';' && cursor[size] != '"' &&
               cursor[size] != '\n' && size + 1 < sizeof(candidate))
            size++;
        memcpy(candidate, cursor, size);
        candidate[size] = '\0';
        if (stat(candidate, &info) == 0)
        {
            if (S_ISREG(info.st_mode) &&
                !native_cache_hash_file_component(&hash, "sdk-artifact", candidate,
                                                  candidate))
                ok = false;
            else if (S_ISDIR(info.st_mode) && size >= 8 &&
                     strcmp(candidate + size - 8, "/include") == 0 &&
                     !native_cache_hash_tree_component(&hash, "sdk-headers", candidate,
                                                       candidate))
                ok = false;
        }
        cursor += size ? size : 1;
    }
    free(contents);
    if (!ok)
        return false;
    native_cache_hash_finish(&hash, digest);
    return true;
}

static bool hash_toolchain(const char *c_compiler, const char *cxx_compiler,
                           const char *generator, const char *configuration,
                           const char *sanitizers, bool relocatable_sdk,
                           char digest[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    NativeCacheHash hash;
    native_cache_hash_init(&hash);
    hash_string(&hash, "domain", "budo-native-toolchain-v1");
    hash_string(&hash, "platform", BUDO_LOCAL_SYSTEM_NAME);
    hash_string(&hash, "architecture", BUDO_LOCAL_SYSTEM_PROCESSOR);
    hash_string(&hash, "configuration", configuration);
    hash_string(&hash, "sanitizers", sanitizers);
    hash_string(&hash, "generator", generator);
    hash_string(&hash, "compiler-id",
                relocatable_sdk ? "host-resolved" : BUDO_LOCAL_C_COMPILER_ID);
    hash_string(&hash, "compiler-version",
                relocatable_sdk ? "validated-by-sdk-cmake-package"
                                : BUDO_LOCAL_C_COMPILER_VERSION);
    hash_string(&hash, "c-compiler-command", c_compiler);
    hash_string(&hash, "cxx-compiler-command", cxx_compiler);
    if (regular_file_exists(c_compiler) &&
        !native_cache_hash_file_component(&hash, "c-compiler", c_compiler, c_compiler))
        return false;
    if (regular_file_exists(cxx_compiler) &&
        !native_cache_hash_file_component(&hash, "cxx-compiler", cxx_compiler,
                                          cxx_compiler))
        return false;
    native_cache_hash_finish(&hash, digest);
    return true;
}

static void combine_build_key(const char *project_digest, const char *sdk_digest,
                              const char *toolchain_digest,
                              char key[BUDO_NATIVE_CACHE_HEX_SIZE])
{
    NativeCacheHash hash;
    native_cache_hash_init(&hash);
    hash_string(&hash, "domain", "budo-native-build-cache-v1");
    hash_string(&hash, "project", project_digest);
    hash_string(&hash, "sdk", sdk_digest);
    hash_string(&hash, "toolchain", toolchain_digest);
    native_cache_hash_finish(&hash, key);
}

static const char *cache_miss_reason(const char *generated_directory,
                                     const char *project_digest,
                                     const char *sdk_digest,
                                     const char *toolchain_digest)
{
    char path[BUDO_NATIVE_PROJECT_MAX_PATH];
    FILE *file;
    char *contents;
    long length;
    const char *reason = "no previous build provenance";
    if (snprintf(path, sizeof(path), "%s/budo-native-build.json",
                 generated_directory) >= (int)sizeof(path) ||
        !(file = fopen(path, "rb")))
        return reason;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 ||
        !(contents = malloc((size_t)length + 1)))
    {
        fclose(file);
        return "previous provenance is unreadable";
    }
    if (fread(contents, 1, (size_t)length, file) != (size_t)length)
        reason = "previous provenance is unreadable";
    else
    {
        contents[length] = '\0';
        if (!strstr(contents, project_digest))
            reason = "project sources, headers, manifest, or flags changed";
        else if (!strstr(contents, sdk_digest))
            reason = "SDK contents changed";
        else if (!strstr(contents, toolchain_digest))
            reason = "compiler, target, generator, or configuration changed";
        else
            reason = "cached result is absent";
    }
    free(contents);
    fclose(file);
    return reason;
}

static bool write_provenance(const NativeProject *project,
                             const char *generated_directory,
                             const char *binary_path, const char *configuration,
                             const char *build_key, const char *project_digest,
                             const char *sdk_digest, const char *toolchain_digest,
                             const char *output_digest, const char *cache_status,
                             const char *invalidation_reason,
                             const char *sdk_directory, const char *sdk_source,
                             const NativeSdkSelection *sdk_selection,
                             const char *c_compiler, const char *generator,
                             const char *sanitizers, bool relocatable_sdk)
{
    char path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char temporary[BUDO_NATIVE_PROJECT_MAX_PATH];
    FILE *file;
    int length = snprintf(path, sizeof(path), "%s/budo-native-build.json",
                          generated_directory);
    if (length < 0 || (size_t)length >= sizeof(path))
        return false;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >=
        (int)sizeof(temporary))
        return false;
    file = fopen(temporary, "wb");
    if (!file)
        return false;

    fputs("{\n  \"schema_version\": 2,\n  \"cache_schema\": 1,\n  \"budo_version\": ", file);
    write_json_string(file, BUDO_VERSION_STRING);
    fputs(",\n  \"sdk_version\": ", file);
    write_json_string(file, project->sdk_version);
    fputs(",\n  \"sdk_build_id\": ", file);
    write_json_string(file, BUDO_NATIVE_SDK_BUILD_ID);
    fputs(",\n  \"platform\": ", file);
    write_json_string(file, BUDO_LOCAL_SYSTEM_NAME);
    fputs(",\n  \"architecture\": ", file);
    write_json_string(file, BUDO_LOCAL_SYSTEM_PROCESSOR);
    fputs(",\n  \"target_tuple\": ", file);
    write_json_string(file, BUDO_LOCAL_TARGET_TUPLE);
    fputs(",\n  \"sdk_source\": ", file);
    write_json_string(file, sdk_source);
    fputs(",\n  \"sdk_directory\": ", file);
    write_json_string(file, sdk_directory ? sdk_directory : "");
    fputs(",\n  \"sdk_manifest_sha256\": ", file);
    write_json_string(file, sdk_selection ? sdk_selection->manifest_sha256 : "");
    fputs(",\n  \"release_metadata_sha256\": ", file);
    write_json_string(file, sdk_selection ? sdk_selection->release_metadata_sha256 : "");
    fputs(",\n  \"sdk_archive_sha256\": ", file);
    write_json_string(file, sdk_selection ? sdk_selection->archive_sha256 : "");
    fputs(",\n  \"build_type\": ", file);
    write_json_string(file, configuration);
    fputs(",\n  \"sanitizers\": ", file);
    write_json_string(file, sanitizers);
    fputs(",\n  \"compiler\": {\n    \"path\": ", file);
    write_json_string(file, c_compiler);
    fputs(",\n    \"id\": ", file);
    write_json_string(file, relocatable_sdk ? "host-resolved"
                                            : BUDO_LOCAL_C_COMPILER_ID);
    fputs(",\n    \"version\": ", file);
    write_json_string(file, relocatable_sdk ? "validated by BudoNativeConfig.cmake"
                                            : BUDO_LOCAL_C_COMPILER_VERSION);
    fputs("\n  },\n  \"cmake_generator\": ", file);
    write_json_string(file, generator);
    fputs(",\n  \"native_targets_file\": ", file);
    write_json_string(file, BUDO_LOCAL_NATIVE_TARGETS_FILE);
    fputs(",\n  \"skia_library\": ", file);
    write_json_string(file, BUDO_LOCAL_SKIA_LIBRARY);
    fputs(",\n  \"build_key\": ", file);
    write_json_string(file, build_key);
    fputs(",\n  \"inputs\": {\n    \"project\": ", file);
    write_json_string(file, project_digest);
    fputs(",\n    \"sdk\": ", file);
    write_json_string(file, sdk_digest);
    fputs(",\n    \"toolchain\": ", file);
    write_json_string(file, toolchain_digest);
    fputs("\n  },\n  \"cache\": {\n    \"status\": ", file);
    write_json_string(file, cache_status);
    fputs(",\n    \"invalidation_reason\": ", file);
    write_json_string(file, invalidation_reason);
    fputs("\n  },\n  \"output_digest\": ", file);
    write_json_string(file, output_digest);
    fputs(",\n  \"output\": ", file);
    write_json_string(file, binary_path);
    fputs("\n}\n", file);
    if (fclose(file) != 0)
        return false;
#ifdef _WIN32
    remove(path);
#endif
    return rename(temporary, path) == 0;
}

static void report_native_build_failure(const char *stage, int exit_code,
                                        const char *build_directory,
                                        const char *sdk_source,
                                        const char *sdk_directory,
                                        const char *c_compiler,
                                        const char *configuration)
{
    fprintf(stderr,
            "\nNative build diagnostic\n"
            "  Failed stage: %s (exit status %d)\n"
            "  Configuration: %s\n"
            "  C compiler: %s\n"
            "  SDK: %s%s%s\n"
            "  Build directory: %s\n"
            "  Compilation database: %s/compile_commands.json\n"
            "Remediation: review the compiler/CMake error above, verify that the SDK "
            "matches this OS, architecture, and compiler family, then rerun the command. "
            "If generated state is damaged, remove only the build directory shown above.\n",
            stage, exit_code, configuration, c_compiler, sdk_source,
            sdk_directory ? " at " : "", sdk_directory ? sdk_directory : "",
            build_directory, build_directory);
}

int native_compile_project(const char *input_path,
                           const NativeCompileOptions *options)
{
    NativeProject project;
    NativeProjectError error;
    char generated_directory[BUDO_NATIVE_PROJECT_MAX_PATH];
    char cmake_build_directory[BUDO_NATIVE_PROJECT_MAX_PATH];
    char binary_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char build_type_option[BUDO_NATIVE_PROJECT_MAX_VALUE];
    char compiler_option[BUDO_NATIVE_PROJECT_MAX_PATH + 32];
    char cxx_compiler_option[BUDO_NATIVE_PROJECT_MAX_PATH + 32];
    char skia_option[BUDO_NATIVE_PROJECT_MAX_PATH + 32];
    char sanitizer_option[96];
    char sdk_package_directory[BUDO_NATIVE_PROJECT_MAX_PATH];
    char sdk_manifest_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char runtime_directory[BUDO_NATIVE_PROJECT_MAX_PATH];
    char project_lock_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char project_digest[BUDO_NATIVE_CACHE_HEX_SIZE];
    char sdk_digest[BUDO_NATIVE_CACHE_HEX_SIZE];
    char toolchain_digest[BUDO_NATIVE_CACHE_HEX_SIZE];
    char build_key[BUDO_NATIVE_CACHE_HEX_SIZE];
    char output_digest[BUDO_NATIVE_CACHE_HEX_SIZE];
    NativeCacheLock project_lock;
    NativeSdkSelection sdk_selection = {0};
    bool project_locked = false;
    const char *cache_status = "miss";
    const char *sdk_directory = NULL;
    const char *sdk_input = BUDO_LOCAL_NATIVE_TARGETS_FILE;
    const char *sdk_source = "local";
    const char *c_compiler = BUDO_LOCAL_C_COMPILER;
    const char *cxx_compiler = BUDO_LOCAL_CXX_COMPILER;
    const char *cmake_command = BUDO_LOCAL_CMAKE_COMMAND;
    const char *generator = BUDO_LOCAL_CMAKE_GENERATOR;
    bool relocatable_sdk = false;
    bool offline = (options && options->offline) ||
                   (getenv("BUDO_NATIVE_SDK_OFFLINE") &&
                    strcmp(getenv("BUDO_NATIVE_SDK_OFFLINE"), "0") != 0);
    char sdk_error[512] = "";
    char host_c_compiler[BUDO_NATIVE_PROJECT_MAX_PATH];
    char host_cxx_compiler[BUDO_NATIVE_PROJECT_MAX_PATH];
    char host_cmake[BUDO_NATIVE_PROJECT_MAX_PATH];
    const char *invalidation_reason = "no matching cached result";
    const char *configuration = "RelWithDebInfo";
    const char *sanitizers = "";
    int result;

    if (options)
    {
        if (options->configuration == NATIVE_BUILD_DEBUG)
            configuration = "Debug";
        else if (options->configuration == NATIVE_BUILD_RELEASE)
            configuration = "Release";
        if ((options->sanitizers & (NATIVE_SANITIZER_ADDRESS |
                                    NATIVE_SANITIZER_UNDEFINED)) ==
            (NATIVE_SANITIZER_ADDRESS | NATIVE_SANITIZER_UNDEFINED))
            sanitizers = "address,undefined";
        else if (options->sanitizers & NATIVE_SANITIZER_ADDRESS)
            sanitizers = "address";
        else if (options->sanitizers & NATIVE_SANITIZER_UNDEFINED)
            sanitizers = "undefined";
    }

    if (!native_project_load(input_path, &project, &error))
    {
        fprintf(stderr, "Error: %s\n", error.message);
        return 1;
    }
    if (strcmp(project.sdk_version, BUDO_VERSION_STRING) != 0)
    {
        fprintf(stderr,
                "Error: native project requests Budo SDK %s, but this CLI provides %s.\n",
                project.sdk_version, BUDO_VERSION_STRING);
        fprintf(stderr,
                "Select the requested SDK/CLI, or intentionally update sdk_version and rebuild the project.\n");
        return 1;
    }
    if (options && options->sdk_directory && options->sdk_directory[0])
    {
        sdk_directory = options->sdk_directory;
        sdk_source = "explicit";
    }
    else if (getenv("BUDO_NATIVE_SDK_DIR") && getenv("BUDO_NATIVE_SDK_DIR")[0])
    {
        sdk_directory = getenv("BUDO_NATIVE_SDK_DIR");
        sdk_source = "environment";
    }
    else if (getenv("BUDO_NATIVE_SDK_DISABLE_LOCAL") &&
             strcmp(getenv("BUDO_NATIVE_SDK_DISABLE_LOCAL"), "0") != 0)
    {
        if (!native_sdk_resolve(offline, &sdk_selection, sdk_error,
                                sizeof(sdk_error)))
        {
            fprintf(stderr, "Error: cannot resolve native SDK: %s\n", sdk_error);
            return 1;
        }
        sdk_directory = sdk_selection.directory;
        sdk_source = "downloaded";
    }
    else if (!regular_file_exists(BUDO_LOCAL_NATIVE_TARGETS_FILE) ||
             !regular_file_exists(BUDO_LOCAL_SKIA_LIBRARY))
    {
        if (!native_sdk_resolve(offline, &sdk_selection, sdk_error,
                                sizeof(sdk_error)))
        {
            fprintf(stderr, "Error: cannot resolve native SDK: %s\n", sdk_error);
            return 1;
        }
        sdk_directory = sdk_selection.directory;
        sdk_source = "downloaded";
    }
    if (sdk_directory)
    {
        if (snprintf(sdk_package_directory, sizeof(sdk_package_directory),
                     "%s/lib/cmake/BudoNative", sdk_directory) >=
                (int)sizeof(sdk_package_directory) ||
            snprintf(sdk_manifest_path, sizeof(sdk_manifest_path),
                     "%s/share/budo/budo-native-sdk.json", sdk_directory) >=
                (int)sizeof(sdk_manifest_path) ||
            !regular_file_exists(sdk_manifest_path) ||
            (strcmp(sdk_source, "downloaded") != 0 &&
             !native_sdk_use_directory(sdk_directory, sdk_source, &sdk_selection,
                                       sdk_error, sizeof(sdk_error))))
        {
            fprintf(stderr, "Error: invalid native SDK directory '%s': %s\n",
                    sdk_directory, sdk_error[0] ? sdk_error : "missing SDK package");
            return 1;
        }
        sdk_input = sdk_package_directory;
        relocatable_sdk = true;
        if (!host_tool("CC",
#ifdef _WIN32
                       "cl",
#else
                       "cc",
#endif
                       host_c_compiler, sizeof(host_c_compiler)) ||
            !host_tool("CXX",
#ifdef _WIN32
                       "cl",
#else
                       "c++",
#endif
                       host_cxx_compiler,
                       sizeof(host_cxx_compiler)) ||
            !host_tool("BUDO_CMAKE", "cmake", host_cmake, sizeof(host_cmake)))
        {
            fprintf(stderr,
                    "Error: cannot resolve host C/C++ compilers and CMake for the relocatable SDK.\n");
            return 1;
        }
        c_compiler = host_c_compiler;
        cxx_compiler = host_cxx_compiler;
        cmake_command = host_cmake;

        generator = "host-default";
    }
    else if (!regular_file_exists(BUDO_LOCAL_NATIVE_TARGETS_FILE))
    {
        fprintf(stderr,
                "Error: local native SDK target manifest is missing: %s\n"
                "Reconfigure and build Budo before using `budo compile`.\n",
                BUDO_LOCAL_NATIVE_TARGETS_FILE);
        return 1;
    }
    if (!sdk_directory && !regular_file_exists(BUDO_LOCAL_SKIA_LIBRARY))
    {
        fprintf(stderr, "Error: local native SDK Skia library is missing: %s\n",
                BUDO_LOCAL_SKIA_LIBRARY);
        return 1;
    }
    if (strchr(cmake_command, '/') && !regular_file_exists(cmake_command))
    {
        fprintf(stderr, "Error: local SDK CMake executable is missing: %s\n",
                cmake_command);
        return 1;
    }
    if ((!relocatable_sdk && !regular_file_exists(c_compiler)) ||
        (!relocatable_sdk && !regular_file_exists(cxx_compiler)))
    {
        fprintf(stderr,
                "Error: the compiler toolchain used by this local SDK is unavailable.\n"
                "Expected C compiler: %s\nExpected C++ compiler: %s\n",
                c_compiler, cxx_compiler);
        return 1;
    }

    if (options && options->build_directory)
    {
        if (strlen(options->build_directory) >= sizeof(generated_directory))
        {
            fprintf(stderr, "Error: native build directory path is too long.\n");
            return 1;
        }
        snprintf(generated_directory, sizeof(generated_directory), "%s",
                 options->build_directory);
    }
    else if (snprintf(generated_directory, sizeof(generated_directory),
                      "%s/.budo/native", project.project_root) < 0 ||
             strlen(project.project_root) + strlen("/.budo/native") >=
                 sizeof(generated_directory))
    {
        fprintf(stderr, "Error: default native build directory path is too long.\n");
        return 1;
    }

    if (!native_cache_make_directories(generated_directory) ||
        snprintf(project_lock_path, sizeof(project_lock_path), "%s/.build.lock",
                 generated_directory) >= (int)sizeof(project_lock_path) ||
        !native_cache_lock_acquire(project_lock_path, &project_lock))
    {
        fprintf(stderr, "Error: cannot lock native build directory '%s'.\n",
                generated_directory);
        return 1;
    }
    project_locked = true;

    if (!native_project_generate(&project, generated_directory,
                                 sdk_input, &error))
    {
        fprintf(stderr, "Error: %s\n", error.message);
        result = 1;
        goto cleanup;
    }
    {
        int build_length = snprintf(cmake_build_directory,
                                    sizeof(cmake_build_directory), "%s/build",
                                    generated_directory);
        int binary_length = snprintf(binary_path, sizeof(binary_path),
                                     "%s/build/bin/%s%s", generated_directory,
                                     project.output_name, BUDO_EXECUTABLE_SUFFIX);
        int runtime_length = snprintf(runtime_directory, sizeof(runtime_directory),
                                      "%s/build/bin", generated_directory);
        if (build_length < 0 ||
            (size_t)build_length >= sizeof(cmake_build_directory) ||
            binary_length < 0 || (size_t)binary_length >= sizeof(binary_path) ||
            runtime_length < 0 || (size_t)runtime_length >= sizeof(runtime_directory))
        {
            fprintf(stderr, "Error: native build output path is too long.\n");
            result = 1;
            goto cleanup;
        }
    }

    {
        int build_type_length = snprintf(build_type_option,
                                         sizeof(build_type_option),
                                         "-DCMAKE_BUILD_TYPE=%s", configuration);
        int compiler_length = snprintf(compiler_option, sizeof(compiler_option),
                                       "-DCMAKE_C_COMPILER=%s",
                                       c_compiler);
        int cxx_compiler_length = snprintf(cxx_compiler_option,
                                           sizeof(cxx_compiler_option),
                                           "-DCMAKE_CXX_COMPILER=%s",
                                           cxx_compiler);
        int skia_length = sdk_directory
                              ? snprintf(skia_option, sizeof(skia_option), "%s", "")
                              : snprintf(skia_option, sizeof(skia_option),
                                         "-DBUDO_NATIVE_SKIA_LIBRARY=%s",
                                         BUDO_LOCAL_SKIA_LIBRARY);
        int sanitizer_length = snprintf(sanitizer_option,
                                        sizeof(sanitizer_option),
                                        "-DBUDO_NATIVE_SANITIZERS=%s",
                                        sanitizers);
        if (build_type_length < 0 ||
            (size_t)build_type_length >= sizeof(build_type_option) ||
            compiler_length < 0 ||
            (size_t)compiler_length >= sizeof(compiler_option) ||
            cxx_compiler_length < 0 ||
            (size_t)cxx_compiler_length >= sizeof(cxx_compiler_option) ||
            skia_length < 0 || (size_t)skia_length >= sizeof(skia_option) ||
            sanitizer_length < 0 ||
            (size_t)sanitizer_length >= sizeof(sanitizer_option))
        {
            fprintf(stderr, "Error: local native SDK configuration is too long.\n");
            result = 1;
            goto cleanup;
        }
    }

    fprintf(stderr,
            "Warning: native Budo projects are trusted, unsandboxed code.\n"
            "Compiling %s with %s %s for %s/%s.\n",
            project.name, relocatable_sdk ? "host compiler" : BUDO_LOCAL_C_COMPILER_ID,
            relocatable_sdk ? "(validated by SDK)" : BUDO_LOCAL_C_COMPILER_VERSION,
            BUDO_LOCAL_SYSTEM_NAME,
            BUDO_LOCAL_SYSTEM_PROCESSOR);
    {
        if (relocatable_sdk)
        {
            const char *configure_arguments[] = {
                cmake_command, "-S", generated_directory,
                "-B", cmake_build_directory, build_type_option,
                compiler_option, cxx_compiler_option, sanitizer_option, NULL};
            result = run_process(configure_arguments);
        }
        else
        {
            const char *configure_arguments[] = {
                cmake_command, "-S", generated_directory,
                "-B", cmake_build_directory, "-G", generator,
                build_type_option, compiler_option, cxx_compiler_option,
                sanitizer_option, skia_option[0] ? skia_option : NULL, NULL};
            result = run_process(configure_arguments);
        }
    }
    if (result != 0)
    {
        fprintf(stderr, "Error: native project configuration failed (%d).\n", result);
        report_native_build_failure("CMake configuration", result,
                                    cmake_build_directory, sdk_source,
                                    sdk_directory, c_compiler, configuration);
        goto cleanup;
    }

    if (!hash_project(&project, project_digest) || !hash_sdk(sdk_directory, sdk_digest) ||
        !hash_toolchain(c_compiler, cxx_compiler, generator, configuration,
                        sanitizers, relocatable_sdk,
                        toolchain_digest))
    {
        fprintf(stderr, "Error: cannot fingerprint native build inputs.\n");
        result = 1;
        goto cleanup;
    }
    combine_build_key(project_digest, sdk_digest, toolchain_digest, build_key);
    invalidation_reason = cache_miss_reason(generated_directory, project_digest,
                                            sdk_digest, toolchain_digest);
    result = native_cache_lookup(build_key, binary_path, output_digest);
    if (result == 1)
    {
        cache_status = "hit";
        invalidation_reason = "none";
        fprintf(stderr, "Native build cache hit: %s\n", build_key);
    }
    else
    {
        if (result < 0)
        {
            cache_status = "recovered-corrupt-entry";
            invalidation_reason = "cached executable failed integrity validation";
            fprintf(stderr, "Native build cache entry is corrupt; rebuilding: %s\n",
                    build_key);
            native_cache_remove(build_key);
        }
        else
            fprintf(stderr, "Native build cache miss: %s\n", build_key);
        {
            const char *build_arguments[] = {
                cmake_command, "--build", cmake_build_directory,
                "--config", configuration, NULL};
            result = run_process(build_arguments);
        }
        if (result != 0)
        {
            fprintf(stderr, "Error: native project compilation failed (%d).\n", result);
            report_native_build_failure("compilation or linking", result,
                                        cmake_build_directory, sdk_source,
                                        sdk_directory, c_compiler, configuration);
            goto cleanup;
        }
    }
    if (!regular_file_exists(binary_path))
    {
        fprintf(stderr, "Error: build succeeded but output was not found: %s\n",
                binary_path);
        result = 1;
        goto cleanup;
    }
    if (!native_project_stage_assets(&project, runtime_directory, &error))
    {
        fprintf(stderr, "Error: %s\n", error.message);
        result = 1;
        goto cleanup;
    }
    if (!native_cache_file_digest(binary_path, output_digest))
    {
        fprintf(stderr, "Error: cannot fingerprint native build output.\n");
        result = 1;
        goto cleanup;
    }
    if (strcmp(cache_status, "hit") != 0)
    {
        char cache_record[1024];
        snprintf(cache_record, sizeof(cache_record),
                 "{\n  \"schema_version\": 1,\n  \"build_key\": \"%s\",\n"
                 "  \"output_digest\": \"%s\",\n  \"platform\": \"%s\",\n"
                 "  \"architecture\": \"%s\",\n  \"configuration\": \"%s\",\n"
                 "  \"sanitizers\": \"%s\"\n}\n",
                 build_key, output_digest, BUDO_LOCAL_SYSTEM_NAME,
                 BUDO_LOCAL_SYSTEM_PROCESSOR, configuration, sanitizers);
        if (!native_cache_publish(build_key, binary_path, output_digest,
                                  cache_record))
            fprintf(stderr, "Warning: cannot publish native build cache entry.\n");
    }
    if (!write_provenance(&project, generated_directory, binary_path, configuration,
                          build_key, project_digest, sdk_digest, toolchain_digest,
                          output_digest, cache_status, invalidation_reason,
                          sdk_directory, sdk_source,
                          sdk_directory ? &sdk_selection : NULL,
                          c_compiler, generator, sanitizers, relocatable_sdk))
    {
        fprintf(stderr, "Error: cannot write native build provenance: %s\n",
                strerror(errno));
        result = 1;
        goto cleanup;
    }

    printf("Native application: %s\n", binary_path);
    printf("Compile commands: %s/build/compile_commands.json\n",
           generated_directory);
    printf("Build provenance: %s/budo-native-build.json\n",
           generated_directory);

    if (options && options->run_after_build)
    {
        char runtime_executable[BUDO_NATIVE_PROJECT_MAX_PATH];
        const char *run_arguments[] = {runtime_executable, NULL};
#ifdef _WIN32
        int runtime_executable_length = snprintf(
            runtime_executable, sizeof(runtime_executable), ".\\%s%s",
            project.output_name, BUDO_EXECUTABLE_SUFFIX);
#else
        int runtime_executable_length = snprintf(
            runtime_executable, sizeof(runtime_executable), "./%s%s",
            project.output_name, BUDO_EXECUTABLE_SUFFIX);
#endif
        if (runtime_executable_length < 0 ||
            (size_t)runtime_executable_length >= sizeof(runtime_executable))
        {
            fprintf(stderr, "Error: native runtime executable path is too long.\n");
            result = 1;
            goto cleanup;
        }
        native_cache_lock_release(&project_lock);
        project_locked = false;
        return run_process_in_directory(run_arguments, runtime_directory);
    }
    result = 0;

cleanup:
    if (project_locked)
        native_cache_lock_release(&project_lock);
    return result;
}

int native_compile_cache_inspect(bool json)
{
    return native_cache_inspect(json);
}

int native_compile_cache_clean_all(void)
{
    return native_cache_clean_all();
}

int native_compile_sdk_cache_inspect(bool json)
{
    return native_sdk_cache_inspect(json);
}

int native_compile_sdk_cache_clean_all(void)
{
    return native_sdk_cache_clean_all();
}