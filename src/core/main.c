#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

#include "core/version.h"
#include "core/window.h"
#include "core/input.h"
#include "core/application_driver.h"
#include "core/subsystem_composition.h"
#include "core/subsystem_registry.h"
#include "desktop/desktop_host.h"
#include "core/file_watcher.h"
#include "core/app_entrypoint.h"
#include "core/app_metadata.h"
#include "core/budo_init.h"
#include "graphics/js_canvas_bindings.h"
#include "graphics/wasm_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"
#include "graphics/skia_wrapper.h"
#include "audio/js_audio_bindings.h"
#include "audio/lua_audio_bindings.h"
#include "midi/js_midi_bindings.h"
#include "midi/lua_midi_bindings.h"
#include "sqlite/js_sqlite_bindings.h"
#include "sqlite/lua_sqlite_bindings.h"
#include "file/js_file_bindings.h"
#include "file/lua_file_bindings.h"
#ifdef BUDO_LLAMACPP
#include "llamacpp/js_llamacpp_bindings.h"
#include "llamacpp/lua_llamacpp_bindings.h"
#endif
#include "network/js_network_bindings.h"
#include "network/js_udp_bindings.h"
#include "network/lua_network_bindings.h"
#include "network/lua_udp_bindings.h"
#include "magneto/js_magneto_bindings.h"
#include "magneto/lua_magneto_bindings.h"
#include "core/managed_basics.h"
#include "core/managed_runtime.h"
#include "core/managed_subsystem_adapters.h"
#ifdef BUDO_NEURAL
#include "neural/neural_wrapper.h"
#include "neural/js_neural_bindings.h"
#include "neural/lua_neural_bindings.h"
#include "neural/wasm_neural_bindings.h"
#endif
#include "audio/wasm_audio_bindings.h"
#include "midi/wasm_midi_bindings.h"
#include "sqlite/wasm_sqlite_bindings.h"
#include "file/wasm_file_bindings.h"
#include "magneto/wasm_magneto_bindings.h"
#include "device/wasm_device_bindings.h"
#include "network/wasm_udp_bindings.h"
#include "midi/rtpmidi.h"
#include "core/web_export.h"
#include "core/web_server.h"
#include "core/android_package.h"
#include "core/embedded_resource.h"
#include "native/native_compile.h"

#include "embedded_devapi.h"
#include "embedded_types.h"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define DEFAULT_TITLE "Budo"

typedef enum
{
    RUNTIME_JAVASCRIPT,
    RUNTIME_WEBASSEMBLY,
    RUNTIME_LUA
} RuntimeType;

static bool ends_with(const char *str, const char *suffix)
{
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len)
        return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

static void print_usage(const char *program_name)
{
    fprintf(stderr,
            "Budo v" BUDO_VERSION_STRING
            " — a lightweight 2D runtime for JavaScript, TypeScript,\n"
            "Lua, and WebAssembly applications.\n"
            "\n"
            "Usage:\n"
            "  %s <command> [parameters] [--options]\n"
            "  %s <DIRECTORY|FILE>             (shorthand for `run <DIRECTORY|FILE>`)\n"
            "\n"
            "Commands:\n"
            "  run <DIRECTORY|FILE>            Run the application in DIRECTORY, or run\n"
            "                                  a single .js/.ts/.lua/.wat/.wasm file as\n"
            "                                  a one-file virtual app.\n"
            "  run --from-input <kind>         Read stdin as main.js/main.ts/main.lua,\n"
            "                                  main.wat, or main.wasm. <kind> is one of\n"
            "                                  js, ts, lua, wat, wasm.\n"
            "                                  Looks for main.ts → main.js → main.lua →\n"
            "                                  main.wasm → main.wat.\n"
            "  compile <DIRECTORY|FILE> [opts] Compile a trusted native C project using\n"
            "                                  the matching local Budo SDK.\n"
            "  cache native inspect [--json]  Inspect the native executable cache.\n"
            "  cache native clean --all      Remove native executable cache entries.\n"
            "  cache sdk inspect [--json]     Inspect verified downloaded SDKs.\n"
            "  cache sdk clean --all         Remove downloaded SDKs only.\n"
            "  init <DIRECTORY> [opts]         Bootstrap a JavaScript project, or use\n"
            "                                  --language c [--template canvas|gpu]\n"
            "                                  for a trusted native C starter. Creates the\n"
            "                                  directory if missing, then writes\n"
            "                                  budo-llm.md, budo.d.ts, jsconfig.json,\n"
            "                                  app.json (interactive — press Enter to\n"
            "                                  accept defaults) and a starter main.js.\n"
            "  web-serve <DIRECTORY> [opts]    Export and serve the application on\n"
            "                                  http://localhost:8080.\n"
            "  web-export <DIRECTORY> [-o DIR] Export the application as a self-contained\n"
            "                                  static web folder (default: dist/web).\n"
            "  android-apk <DIRECTORY> [opts]  Package the application as an Android APK\n"
            "                                  (Budo Pro feature).\n"
            "                                  (artifact lands in dist/).\n"
            "  android-aab <DIRECTORY> [opts]  Package the application as an Android App\n"
            "                                  Bundle (AAB) for Play Store upload\n"
            "                                  (Budo Pro feature).\n"
            "  budo.d.ts                   Print the bundled TypeScript declarations\n"
            "                                  to stdout.\n"
            "  budo-llm.md                      Print the bundled developer API reference\n"
            "                                  to stdout.\n"
            "  help                            Show this help page.\n"
            "\n"
            "Options for `run` (and the implicit shorthand):\n"
            "  --width <n>      Window width (default: %d)\n"
            "  --height <n>     Window height (default: %d)\n"
            "  --title <str>    Window title (default: %s)\n"
            "  --fullscreen     Start in fullscreen mode\n"
            "  --no-vsync       Disable vertical sync\n"
            "  --watch          Restart the app when files in the project change\n"
            "  --from-input <k> Read stdin as a virtual app main file (js, ts, lua, wat, wasm)\n"
            "  --file-root <p>  Root directory for sys.files (default: project dir)\n"
            "\n"
            "Options for `compile`:\n"
            "  --build-dir <p>  Generated project/build directory\n"
            "                   (default: <project>/.budo/native)\n"
            "  --sdk <p>        Verified unpacked target-specific native SDK directory\n"
            "                   (or set BUDO_NATIVE_SDK_DIR); otherwise resolve automatically\n"
            "  --offline        Do not access the network; require a verified cached SDK\n"
            "  --debug          Build application code with debug information\n"
            "  --release        Build optimized application code (default: RelWithDebInfo)\n"
            "  --sanitize <s>   Instrument application code with address, undefined,\n"
            "                   or address,undefined (Clang/GCC; not with --release)\n"
            "  --run            Run the executable after a successful compilation\n"
            "  --watch          Unsupported for native projects (always rejected)\n"
            "\n"
            "Options for `init`:\n"
            "  --language js|c  Project language (default: js)\n"
            "  --template <t>   Native C starter: canvas (default) or gpu\n"
            "\n"
            "Options for `web-serve`:\n"
            "  -l, --listen <[HOST:]PORT>\n"
            "                   Listen address (default: localhost:8080). HOST may be an\n"
            "                   IPv4 address, 'localhost', or '0.0.0.0'/'*' for all\n"
            "                   interfaces. Examples: --listen 8084, --listen 0.0.0.0:8084\n"
            "\n"
            "Options for `android-apk` / `android-aab` (Budo Pro):\n"
            "  --release        Build a release artifact (default for android-aab)\n"
            "  --debug          Build a debug artifact (default for android-apk)\n"
            "  -o, --output <DIR>\n"
            "                   Copy the final APK/AAB to <DIR> instead of dist/\n"
            "  --install        Install the produced APK on all adb devices (APK only)\n"
            "  --no-build       Stage the project but skip the gradle build\n"
            "  --clean          Wipe the staging directory before staging\n"
            "\n"
            "Examples:\n"
            "  %s init my-app\n"
            "  %s run examples/demo\n"
            "  %s compile examples/12_native_c\n"
            "  %s compile native-app --run\n"
            "  %s examples/demo                # equivalent to `run examples/demo`\n"
            "  %s sketch.js                    # run one file as a virtual app\n"
            "  echo 'sys.log(42)' | %s run --from-input js\n"
            "  %s web-serve examples/demo\n"
            "  %s web-serve examples/demo --listen 0.0.0.0:8084\n"
            "  %s web-export examples/demo -o dist/web\n"
            "  %s budo.d.ts > budo.d.ts\n"
            "  %s budo-llm.md | less\n",
            program_name, program_name,
            DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_TITLE,
            program_name, program_name, program_name, program_name,
            program_name, program_name, program_name, program_name, program_name,
            program_name, program_name,
            program_name);
}

typedef enum
{
    CMD_RUN,     
    CMD_COMPILE, 
    CMD_NATIVE_CACHE_INSPECT,
    CMD_NATIVE_CACHE_CLEAN,
    CMD_NATIVE_SDK_CACHE_INSPECT,
    CMD_NATIVE_SDK_CACHE_CLEAN,
    CMD_INIT,         
    CMD_WEB_SERVE,    
    CMD_WEB_EXPORT,   
    CMD_ANDROID_APK,  
    CMD_ANDROID_AAB,  
    CMD_PRINT_DTS,    
    CMD_PRINT_DEVAPI, 
    CMD_HELP          
} CommandKind;

typedef struct
{
    CommandKind command;
    const char *project_dir;
    const char *watch_root;
    char virtual_project_dir[PATH_MAX];
    char single_file_source[PATH_MAX];
    const char *from_input_language;
    char main_script[PATH_MAX];
    RuntimeType runtime_type;
    int width;
    int height;
    const char *title;
    bool fullscreen;
    bool vsync;
    bool watch;
    bool single_file_mode;
    bool from_input_mode;
    const char *file_root;
    const char *export_web_output;
    const char *native_build_directory;
    const char *native_sdk_directory;
    BudoInitOptions init_options;
    bool native_run_after_build;
    NativeBuildConfiguration native_configuration;
    unsigned native_sanitizers;
    bool native_cache_json;
    bool native_offline;
    
    const char *web_listen_host;
    int web_listen_port;
    char web_listen_host_buf[256];
    
    bool android_release;
    bool android_no_build;
    bool android_clean;
    bool android_install;
    const char *android_output_dir;
} AppConfig;

static bool is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out = NULL;
    unsigned char buffer[8192];
    size_t n;
    bool ok = false;

    if (!in)
        return false;

    out = fopen(dst, "wb");
    if (!out)
        goto done;

    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (fwrite(buffer, 1, n, out) != n)
            goto done;
    }

    ok = !ferror(in) && fflush(out) == 0;

done:
    if (out)
        fclose(out);
    fclose(in);
    return ok;
}

static const char *single_file_main_name(const char *path, RuntimeType *runtime)
{
    if (ends_with(path, ".ts"))
    {
        *runtime = RUNTIME_JAVASCRIPT;
        return "main.ts";
    }
    if (ends_with(path, ".js"))
    {
        *runtime = RUNTIME_JAVASCRIPT;
        return "main.js";
    }
    if (ends_with(path, ".lua"))
    {
        *runtime = RUNTIME_LUA;
        return "main.lua";
    }
    if (ends_with(path, ".wat"))
    {
        *runtime = RUNTIME_WEBASSEMBLY;
        return "main.wat";
    }
    if (ends_with(path, ".wasm"))
    {
        *runtime = RUNTIME_WEBASSEMBLY;
        return "main.wasm";
    }
    return NULL;
}

static const char *input_main_name(const char *language, RuntimeType *runtime)
{
    if (strcmp(language, "ts") == 0)
    {
        *runtime = RUNTIME_JAVASCRIPT;
        return "main.ts";
    }
    if (strcmp(language, "js") == 0)
    {
        *runtime = RUNTIME_JAVASCRIPT;
        return "main.js";
    }
    if (strcmp(language, "lua") == 0)
    {
        *runtime = RUNTIME_LUA;
        return "main.lua";
    }
    if (strcmp(language, "wat") == 0)
    {
        *runtime = RUNTIME_WEBASSEMBLY;
        return "main.wat";
    }
    if (strcmp(language, "wasm") == 0)
    {
        *runtime = RUNTIME_WEBASSEMBLY;
        return "main.wasm";
    }
    return NULL;
}

static bool write_stdin_to_file(const char *dst)
{
    FILE *out = fopen(dst, "wb");
    unsigned char buffer[8192];
    size_t n;
    bool ok = false;

    if (!out)
        return false;

    while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0)
    {
        if (fwrite(buffer, 1, n, out) != n)
            goto done;
    }

    ok = !ferror(stdin) && fflush(out) == 0;

done:
    fclose(out);
    return ok;
}

static bool refresh_single_file_app(AppConfig *config)
{
    if (!config || !config->single_file_mode)
        return true;

    if (!copy_file(config->single_file_source, config->main_script))
    {
        fprintf(stderr, "Error: Cannot stage single-file app '%s': %s\n",
                config->single_file_source, strerror(errno));
        return false;
    }
    return true;
}

static void cleanup_single_file_app(AppConfig *config)
{
    if (!config || (!config->single_file_mode && !config->from_input_mode) ||
        !config->virtual_project_dir[0])
        return;

    if (config->main_script[0])
        unlink(config->main_script);
    rmdir(config->virtual_project_dir);
}

static bool configure_single_file_app(AppConfig *config, const char *input_path)
{
    RuntimeType runtime;
    const char *main_name = single_file_main_name(input_path, &runtime);
    char tmp_template[] = "/tmp/budo-single-XXXXXX";
    char *tmp_dir;

    if (!main_name)
    {
        fprintf(stderr,
                "Error: '%s' is not a supported single-file app. Use .js, .ts, .lua, .wat, or .wasm.\n",
                input_path);
        return false;
    }

    if (!realpath(input_path, config->single_file_source))
    {
        fprintf(stderr, "Error: Cannot resolve '%s': %s\n", input_path, strerror(errno));
        return false;
    }

    tmp_dir = mkdtemp(tmp_template);
    if (!tmp_dir)
    {
        fprintf(stderr, "Error: Cannot create virtual app directory: %s\n", strerror(errno));
        return false;
    }

    snprintf(config->virtual_project_dir, sizeof(config->virtual_project_dir), "%s", tmp_dir);
    snprintf(config->main_script, sizeof(config->main_script), "%s/%s",
             config->virtual_project_dir, main_name);
    config->project_dir = config->virtual_project_dir;
    config->watch_root = config->single_file_source;
    config->runtime_type = runtime;
    config->single_file_mode = true;

    if (config->file_root == NULL)
        config->file_root = config->project_dir;

    if (!refresh_single_file_app(config))
    {
        cleanup_single_file_app(config);
        return false;
    }

    return true;
}

static bool configure_stdin_app(AppConfig *config)
{
    RuntimeType runtime;
    const char *main_name = input_main_name(config->from_input_language, &runtime);
    char tmp_template[] = "/tmp/budo-input-XXXXXX";
    char *tmp_dir;

    if (!main_name)
    {
        fprintf(stderr,
                "Error: unsupported --from-input kind '%s'. Use js, ts, lua, wat, or wasm.\n",
                config->from_input_language ? config->from_input_language : "");
        return false;
    }

    tmp_dir = mkdtemp(tmp_template);
    if (!tmp_dir)
    {
        fprintf(stderr, "Error: Cannot create virtual app directory: %s\n", strerror(errno));
        return false;
    }

    snprintf(config->virtual_project_dir, sizeof(config->virtual_project_dir), "%s", tmp_dir);
    snprintf(config->main_script, sizeof(config->main_script), "%s/%s",
             config->virtual_project_dir, main_name);
    config->project_dir = config->virtual_project_dir;
    config->watch_root = NULL;
    config->runtime_type = runtime;

    if (config->file_root == NULL)
        config->file_root = config->project_dir;

    if (!write_stdin_to_file(config->main_script))
    {
        fprintf(stderr, "Error: Cannot read stdin into virtual app: %s\n", strerror(errno));
        cleanup_single_file_app(config);
        return false;
    }

    return true;
}

static bool resolve_main_script(AppConfig *config)
{
    AppEntrypoint entrypoint;
    char error[256];
    if (!app_entrypoint_resolve(config->project_dir,
                                APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA |
                                    APP_ENTRYPOINT_WEBASSEMBLY,
                                &entrypoint, error, sizeof(error)))
    {
        if (error[0] && strstr(error, "Ambiguous"))
            fprintf(stderr, "Error: %s in '%s'\n", error, config->project_dir);
        return false;
    }
    snprintf(config->main_script, PATH_MAX, "%s/%s",
             config->project_dir, entrypoint.filename);
    config->runtime_type = entrypoint.runtime == APP_ENTRYPOINT_JAVASCRIPT
                               ? RUNTIME_JAVASCRIPT
                           : entrypoint.runtime == APP_ENTRYPOINT_LUA
                               ? RUNTIME_LUA
                               : RUNTIME_WEBASSEMBLY;
    return true;
}

static bool parse_run_options(int argc, char **argv, int start, AppConfig *config)
{
    for (int i = start; i < argc; i++)
    {
        if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            config->width = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            config->height = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--title") == 0 && i + 1 < argc)
        {
            config->title = argv[++i];
        }
        else if (strcmp(argv[i], "--fullscreen") == 0)
        {
            config->fullscreen = true;
        }
        else if (strcmp(argv[i], "--no-vsync") == 0)
        {
            config->vsync = false;
        }
        else if (strcmp(argv[i], "--watch") == 0)
        {
            config->watch = true;
        }
        else if (strcmp(argv[i], "--from-input") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --from-input expects js, ts, lua, wat, or wasm\n");
                return false;
            }
            config->from_input_mode = true;
            config->from_input_language = argv[++i];
        }
        else if (strcmp(argv[i], "--file-root") == 0 && i + 1 < argc)
        {
            config->file_root = argv[++i];
        }
        else if (argv[i][0] != '-')
        {
            if (config->project_dir == NULL)
            {
                config->project_dir = argv[i];
            }
            else
            {
                fprintf(stderr, "Error: Multiple project directories specified\n");
                return false;
            }
        }
        else
        {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

static bool parse_args(int argc, char **argv, AppConfig *config)
{
    config->command = CMD_RUN;
    config->project_dir = NULL;
    config->watch_root = NULL;
    config->virtual_project_dir[0] = '\0';
    config->single_file_source[0] = '\0';
    config->from_input_language = NULL;
    config->main_script[0] = '\0';
    config->runtime_type = RUNTIME_JAVASCRIPT;
    config->width = DEFAULT_WIDTH;
    config->height = DEFAULT_HEIGHT;
    config->title = DEFAULT_TITLE;
    config->fullscreen = false;
    config->vsync = true;
    config->watch = false;
    config->single_file_mode = false;
    config->web_listen_host = NULL;
    config->web_listen_port = 8080;
    config->from_input_mode = false;
    config->file_root = NULL;
    config->export_web_output = NULL;
    config->native_build_directory = NULL;
    config->native_sdk_directory = NULL;
    config->init_options.language = BUDO_INIT_JAVASCRIPT;
    config->init_options.template_kind = BUDO_INIT_TEMPLATE_CANVAS;
    config->native_run_after_build = false;
    config->native_configuration = NATIVE_BUILD_RELWITHDEBINFO;
    config->native_sanitizers = NATIVE_SANITIZER_NONE;
    config->native_cache_json = false;
    config->native_offline = false;
    config->android_release = false;
    config->android_no_build = false;
    config->android_clean = false;
    config->android_install = false;
    config->android_output_dir = NULL;

    if (argc < 2)
    {
        config->command = CMD_HELP;
        return true;
    }

    const char *first = argv[1];

    if (strcmp(first, "help") == 0 ||
        strcmp(first, "--help") == 0 ||
        strcmp(first, "-h") == 0)
    {
        config->command = CMD_HELP;
        return true;
    }

    if (strcmp(first, "budo.d.ts") == 0)
    {
        config->command = CMD_PRINT_DTS;
        return true;
    }
    if (strcmp(first, "budo-llm.md") == 0)
    {
        config->command = CMD_PRINT_DEVAPI;
        return true;
    }

    if (strcmp(first, "init") == 0)
    {
        config->command = CMD_INIT;
        if (argc < 3 || argv[2][0] == '-')
        {
            fprintf(stderr, "Error: `init` requires a directory argument.\n");
            fprintf(stderr, "Usage: %s init <DIRECTORY> [--language js|c] [--template canvas|gpu]\n", argv[0]);
            return false;
        }
        config->project_dir = argv[2];
        bool language_seen = false;
        bool template_seen = false;
        for (int i = 3; i < argc; i++)
        {
            if (strcmp(argv[i], "--language") == 0 && i + 1 < argc)
            {
                if (language_seen)
                {
                    fprintf(stderr, "Error: --language may only be specified once.\n");
                    return false;
                }
                language_seen = true;
                const char *language = argv[++i];
                if (strcmp(language, "c") == 0)
                    config->init_options.language = BUDO_INIT_NATIVE_C;
                else if (strcmp(language, "js") != 0 && strcmp(language, "javascript") != 0)
                {
                    fprintf(stderr, "Error: unsupported init language '%s'; use js or c.\n", language);
                    return false;
                }
            }
            else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc)
            {
                if (template_seen)
                {
                    fprintf(stderr, "Error: --template may only be specified once.\n");
                    return false;
                }
                template_seen = true;
                const char *template_name = argv[++i];
                if (strcmp(template_name, "gpu") == 0)
                    config->init_options.template_kind = BUDO_INIT_TEMPLATE_GPU;
                else if (strcmp(template_name, "canvas") != 0)
                {
                    fprintf(stderr, "Error: unsupported native template '%s'; use canvas or gpu.\n", template_name);
                    return false;
                }
            }
            else
            {
                fprintf(stderr, "Error: unknown option '%s' for init.\n", argv[i]);
                return false;
            }
        }
        if (template_seen && config->init_options.language != BUDO_INIT_NATIVE_C)
        {
            fprintf(stderr, "Error: --template is only valid with --language c.\n");
            return false;
        }
        return true;
    }

    if (strcmp(first, "compile") == 0)
    {
        config->command = CMD_COMPILE;
        if (argc < 3 || argv[2][0] == '-')
        {
            fprintf(stderr, "Error: `compile` requires a C file or project directory.\n");
            fprintf(stderr, "Usage: %s compile <DIRECTORY|FILE> [--build-dir DIR] [--sdk DIR] [--run]\n",
                    argv[0]);
            return false;
        }
        config->project_dir = argv[2];
        bool configuration_seen = false;
        bool sanitizers_seen = false;
        for (int i = 3; i < argc; i++)
        {
            if (strcmp(argv[i], "--run") == 0)
                config->native_run_after_build = true;
            else if (strcmp(argv[i], "--offline") == 0)
                config->native_offline = true;
            else if (strcmp(argv[i], "--debug") == 0 ||
                     strcmp(argv[i], "--release") == 0)
            {
                if (configuration_seen)
                {
                    fprintf(stderr, "Error: choose only one native build configuration.\n");
                    return false;
                }
                configuration_seen = true;
                config->native_configuration = strcmp(argv[i], "--debug") == 0
                                                   ? NATIVE_BUILD_DEBUG
                                                   : NATIVE_BUILD_RELEASE;
            }
            else if (strcmp(argv[i], "--sanitize") == 0)
            {
                if (sanitizers_seen || i + 1 >= argc)
                {
                    fprintf(stderr, "Error: --sanitize requires one value and may only be specified once.\n");
                    return false;
                }
                sanitizers_seen = true;
                const char *value = argv[++i];
                if (strcmp(value, "address") == 0)
                    config->native_sanitizers = NATIVE_SANITIZER_ADDRESS;
                else if (strcmp(value, "undefined") == 0)
                    config->native_sanitizers = NATIVE_SANITIZER_UNDEFINED;
                else if (strcmp(value, "address,undefined") == 0 ||
                         strcmp(value, "undefined,address") == 0)
                    config->native_sanitizers = NATIVE_SANITIZER_ADDRESS |
                                                NATIVE_SANITIZER_UNDEFINED;
                else
                {
                    fprintf(stderr, "Error: unsupported sanitizer '%s'; use address, undefined, or address,undefined.\n", value);
                    return false;
                }
            }
            else if (strcmp(argv[i], "--watch") == 0)
            {
                fprintf(stderr,
                        "Error: --watch is not supported for native C projects; compile again after changes.\n");
                return false;
            }
            else if (strcmp(argv[i], "--build-dir") == 0)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    fprintf(stderr, "Error: --build-dir requires a directory argument.\n");
                    return false;
                }
                config->native_build_directory = argv[++i];
            }
            else if (strcmp(argv[i], "--sdk") == 0)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    fprintf(stderr, "Error: --sdk requires a directory argument.\n");
                    return false;
                }
                config->native_sdk_directory = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: Unknown option '%s' for compile\n", argv[i]);
                return false;
            }
        }
        if (config->native_configuration == NATIVE_BUILD_RELEASE &&
            config->native_sanitizers != NATIVE_SANITIZER_NONE)
        {
            fprintf(stderr, "Error: sanitizers cannot be combined with --release; use --debug or the default RelWithDebInfo build.\n");
            return false;
        }
        return true;
    }

    if (strcmp(first, "cache") == 0)
    {
        if (argc >= 4 && strcmp(argv[2], "native") == 0 &&
            strcmp(argv[3], "inspect") == 0)
        {
            config->command = CMD_NATIVE_CACHE_INSPECT;
            if (argc == 5 && strcmp(argv[4], "--json") == 0)
                config->native_cache_json = true;
            else if (argc != 4)
            {
                fprintf(stderr, "Usage: %s cache native inspect [--json]\n", argv[0]);
                return false;
            }
            return true;
        }
        if (argc == 5 && strcmp(argv[2], "native") == 0 &&
            strcmp(argv[3], "clean") == 0 && strcmp(argv[4], "--all") == 0)
        {
            config->command = CMD_NATIVE_CACHE_CLEAN;
            return true;
        }
        if (argc >= 4 && strcmp(argv[2], "sdk") == 0 &&
            strcmp(argv[3], "inspect") == 0)
        {
            config->command = CMD_NATIVE_SDK_CACHE_INSPECT;
            if (argc == 5 && strcmp(argv[4], "--json") == 0)
                config->native_cache_json = true;
            else if (argc != 4)
            {
                fprintf(stderr, "Usage: %s cache sdk inspect [--json]\n", argv[0]);
                return false;
            }
            return true;
        }
        if (argc == 5 && strcmp(argv[2], "sdk") == 0 &&
            strcmp(argv[3], "clean") == 0 && strcmp(argv[4], "--all") == 0)
        {
            config->command = CMD_NATIVE_SDK_CACHE_CLEAN;
            return true;
        }
        fprintf(stderr,
                "Usage: %s cache native inspect [--json]\n"
                "       %s cache native clean --all\n"
                "       %s cache sdk inspect [--json]\n"
                "       %s cache sdk clean --all\n",
                argv[0], argv[0], argv[0], argv[0]);
        return false;
    }

    if (strcmp(first, "web-serve") == 0)
    {
        config->command = CMD_WEB_SERVE;
        if (argc < 3 || argv[2][0] == '-')
        {
            fprintf(stderr, "Error: `web-serve` requires a directory argument.\n");
            fprintf(stderr, "Usage: %s web-serve <DIRECTORY> [--listen [HOST:]PORT]\n", argv[0]);
            return false;
        }
        config->project_dir = argv[2];
        for (int i = 3; i < argc; i++)
        {
            if ((strcmp(argv[i], "--listen") == 0 || strcmp(argv[i], "-l") == 0) &&
                i + 1 < argc)
            {
                const char *spec = argv[++i];
                
                const char *colon = strrchr(spec, ':');
                const char *port_str = NULL;
                if (colon)
                {
                    size_t host_len = (size_t)(colon - spec);
                    if (host_len >= sizeof(config->web_listen_host_buf))
                    {
                        fprintf(stderr, "Error: listen host is too long: %s\n", spec);
                        return false;
                    }
                    memcpy(config->web_listen_host_buf, spec, host_len);
                    config->web_listen_host_buf[host_len] = '\0';
                    if (config->web_listen_host_buf[0] != '\0')
                        config->web_listen_host = config->web_listen_host_buf;
                    port_str = colon + 1;
                }
                else
                {
                    
                    char *endp = NULL;
                    long maybe_port = strtol(spec, &endp, 10);
                    if (*spec != '\0' && *endp == '\0')
                    {
                        (void)maybe_port;
                        port_str = spec;
                    }
                    else
                    {
                        if (strlen(spec) >= sizeof(config->web_listen_host_buf))
                        {
                            fprintf(stderr, "Error: listen host is too long: %s\n", spec);
                            return false;
                        }
                        strcpy(config->web_listen_host_buf, spec);
                        config->web_listen_host = config->web_listen_host_buf;
                    }
                }

                if (port_str && *port_str != '\0')
                {
                    char *endp = NULL;
                    long p = strtol(port_str, &endp, 10);
                    if (*endp != '\0' || p < 1 || p > 65535)
                    {
                        fprintf(stderr, "Error: invalid listen port: %s\n", port_str);
                        return false;
                    }
                    config->web_listen_port = (int)p;
                }
            }
            else
            {
                fprintf(stderr, "Error: Unknown option '%s' for web-serve\n", argv[i]);
                fprintf(stderr, "Usage: %s web-serve <DIRECTORY> [--listen [HOST:]PORT]\n", argv[0]);
                return false;
            }
        }
        return true;
    }

    if (strcmp(first, "web-export") == 0)
    {
        config->command = CMD_WEB_EXPORT;
        if (argc < 3 || argv[2][0] == '-')
        {
            fprintf(stderr, "Error: `web-export` requires a directory argument.\n");
            fprintf(stderr, "Usage: %s web-export <DIRECTORY> [-o <OUTPUT_DIR>]\n", argv[0]);
            return false;
        }
        config->project_dir = argv[2];
        for (int i = 3; i < argc; i++)
        {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            {
                config->export_web_output = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: Unknown option '%s' for web-export\n", argv[i]);
                return false;
            }
        }
        return true;
    }

    if (strcmp(first, "android-apk") == 0 || strcmp(first, "android-aab") == 0)
    {
        config->command = (strcmp(first, "android-aab") == 0)
                              ? CMD_ANDROID_AAB
                              : CMD_ANDROID_APK;
        config->android_release = (config->command == CMD_ANDROID_AAB);
        if (argc < 3 || argv[2][0] == '-')
        {
            fprintf(stderr, "Error: `%s` requires a directory argument.\n", first);
            fprintf(stderr, "Usage: %s %s <DIRECTORY> [--release|--debug] [-o DIR] "
                            "[--install] [--no-build] [--clean]\n",
                    argv[0], first);
            return false;
        }
        config->project_dir = argv[2];
        for (int i = 3; i < argc; i++)
        {
            if (strcmp(argv[i], "--release") == 0)
                config->android_release = true;
            else if (strcmp(argv[i], "--debug") == 0)
                config->android_release = false;
            else if (strcmp(argv[i], "--install") == 0)
            {
                if (config->command != CMD_ANDROID_APK)
                {
                    fprintf(stderr, "Error: --install is only supported for android-apk\n");
                    return false;
                }
                config->android_install = true;
            }
            else if (strcmp(argv[i], "--no-build") == 0)
                config->android_no_build = true;
            else if (strcmp(argv[i], "--clean") == 0)
                config->android_clean = true;
            else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
                config->android_output_dir = argv[++i];
            else
            {
                fprintf(stderr, "Error: Unknown option '%s' for %s\n", argv[i], first);
                return false;
            }
        }
        return true;
    }

    int opt_start = 1;
    if (strcmp(first, "run") == 0)
    {
        opt_start = 2;
    }

    config->command = CMD_RUN;
    if (!parse_run_options(argc, argv, opt_start, config))
        return false;

    if (config->from_input_mode)
    {
        if (config->project_dir != NULL)
        {
            fprintf(stderr, "Error: --from-input cannot be combined with a project path\n");
            return false;
        }
        if (config->watch)
        {
            fprintf(stderr, "Error: --watch cannot be used with --from-input\n");
            return false;
        }
        return configure_stdin_app(config);
    }

    if (config->project_dir == NULL)
    {
        config->project_dir = ".";
    }

    if (file_exists(config->project_dir))
    {
        return configure_single_file_app(config, config->project_dir);
    }

    config->watch_root = config->project_dir;

    if (config->file_root == NULL)
    {
        config->file_root = config->project_dir;
    }

    if (!is_directory(config->project_dir))
    {
        fprintf(stderr, "Error: '%s' is not a valid directory or supported app file\n", config->project_dir);
        return false;
    }

    if (!resolve_main_script(config))
    {
        fprintf(stderr,
                "Error: Cannot find 'main.ts', 'main.js', 'main.lua', 'main.wat', "
                "or 'main.wasm' in project directory '%s'\n",
                config->project_dir);
        return false;
    }
    return true;
}

static const char *runtime_type_name(RuntimeType runtime_type)
{
    switch (runtime_type)
    {
    case RUNTIME_JAVASCRIPT:
        return "JavaScript (QuickJS)";
    case RUNTIME_LUA:
        return "Lua";
    case RUNTIME_WEBASSEMBLY:
        return "WebAssembly (Wasmtime)";
    }
    return "Unknown";
}

static void metadata_name_from_single_file(const char *path, AppMetadata *metadata)
{
    const char *base;
    const char *dot;
    size_t len;

    if (!path || !metadata)
        return;

    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    dot = strrchr(base, '.');
    len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    if (len == 0)
        return;
    if (len >= sizeof(metadata->name))
        len = sizeof(metadata->name) - 1;
    memcpy(metadata->name, base, len);
    metadata->name[len] = '\0';
}

#define DEFINE_SUBSYSTEM_CLEANUP_ADAPTER(adapter, context_type, cleanup) \
    static void adapter(void *opaque)                                    \
    {                                                                    \
        cleanup((context_type *)opaque);                                 \
    }

DEFINE_SUBSYSTEM_CLEANUP_ADAPTER(wasm_canvas_cleanup_adapter,
                                 WasmCanvasContext, wasm_canvas_destroy)
DEFINE_SUBSYSTEM_CLEANUP_ADAPTER(js_udp_cleanup_adapter,
                                 JsUdpContext, js_udp_cleanup)
DEFINE_SUBSYSTEM_CLEANUP_ADAPTER(lua_udp_cleanup_adapter,
                                 LuaUdpContext, lua_udp_cleanup)
DEFINE_SUBSYSTEM_CLEANUP_ADAPTER(rtpmidi_cleanup_adapter,
                                 RtpMidiContext, rtpmidi_destroy)

#undef DEFINE_SUBSYSTEM_CLEANUP_ADAPTER

static void js_udp_poll_adapter(void *opaque)
{
    js_udp_poll((JsUdpContext *)opaque);
}

static void lua_udp_poll_adapter(void *opaque)
{
    lua_udp_poll((LuaUdpContext *)opaque);
}

typedef struct
{
    
    ApplicationDriver driver;
    SubsystemRegistry subsystems;

    RuntimeType runtime_type;
    bool loaded;

    const AppConfig *config;
    const AppMetadata *metadata;

    Window *window;
    InputState input;

    ManagedRuntimeCommon common_contexts;
} RuntimeInstance;

static void runtime_instance_init(RuntimeInstance *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    subsystem_registry_init(&runtime->subsystems);
    runtime->runtime_type = RUNTIME_JAVASCRIPT;
}

#define RUNTIME_SUBSYSTEM(name_, role_, initialize_, poll_, cleanup_, optional_) \
    {                                                                            \
        name_, role_, initialize_, {.poll = poll_, .cleanup = cleanup_}, optional_}

static void *runtime_init_js_core(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_ctx = js_runtime_create(runtime->config->project_dir);
    return runtime->common_contexts.js_ctx;
}

static void *runtime_init_js_canvas(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_graphic_ctx = js_graphic_init(runtime->common_contexts.js_ctx);
    return runtime->common_contexts.js_graphic_ctx;
}

static void *runtime_init_js_audio(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_audio_ctx = js_audio_init(
        runtime->common_contexts.js_ctx->context, runtime->config->project_dir);
    return runtime->common_contexts.js_audio_ctx;
}

static void *runtime_init_js_sqlite(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.sqlite_ctx = js_sqlite_init(
        runtime->common_contexts.js_ctx->context, runtime->config->project_dir);
    return runtime->common_contexts.sqlite_ctx;
}

static void *runtime_init_js_file(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_file_ctx = js_file_init(
        runtime->common_contexts.js_ctx->context, runtime->config->project_dir);
    runtime->common_contexts.file_ctx = js_file_context(runtime->common_contexts.js_file_ctx);
    if (runtime->common_contexts.file_ctx)
        file_set_write_root(runtime->common_contexts.file_ctx, runtime->config->file_root);
    return runtime->common_contexts.js_file_ctx;
}

static void *runtime_init_js_network(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_network_ctx = js_network_init(
        runtime->common_contexts.js_ctx->context, runtime->config->project_dir);
    return runtime->common_contexts.js_network_ctx;
}

#ifdef BUDO_LLAMACPP
static void *runtime_init_js_llamacpp(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_lamacpp_ctx = js_llamacpp_init(runtime->common_contexts.js_ctx->context, runtime->common_contexts.file_ctx);
    return runtime->common_contexts.js_lamacpp_ctx;
}

static void *runtime_init_lua_llamacpp(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_llamacpp_ctx = lua_llamacpp_init(runtime->common_contexts.lua_ctx->L, runtime->common_contexts.file_ctx);
    return runtime->common_contexts.lua_llamacpp_ctx;
}
#endif

static void *runtime_init_js_magneto(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.magneto_ctx = js_magneto_init(runtime->common_contexts.js_ctx->context);
    return runtime->common_contexts.magneto_ctx;
}

static void *runtime_init_js_device(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.device_ctx = managed_basics_init_js(runtime->common_contexts.js_ctx->context);
    return runtime->common_contexts.device_ctx;
}

static void *runtime_init_js_udp(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_udp_ctx = js_udp_init(runtime->common_contexts.js_ctx->context);
    runtime->common_contexts.udp_ctx = js_udp_context(runtime->common_contexts.js_udp_ctx);
    return runtime->common_contexts.js_udp_ctx;
}

static void *runtime_init_rtpmidi(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.rtpmidi_ctx = runtime->common_contexts.udp_ctx
                                               ? rtpmidi_create(runtime->common_contexts.udp_ctx)
                                               : NULL;
    return runtime->common_contexts.rtpmidi_ctx;
}

static void *runtime_init_js_midi(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.js_midi_ctx = js_midi_init(runtime->common_contexts.js_ctx->context);
    if (runtime->common_contexts.js_midi_ctx)
        js_midi_set_rtpmidi(runtime->common_contexts.js_midi_ctx, runtime->common_contexts.rtpmidi_ctx);
    return runtime->common_contexts.js_midi_ctx;
}

static void *runtime_init_lua_canvas(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_ctx = lua_canvas_create(runtime->config->project_dir);
    return runtime->common_contexts.lua_ctx;
}

static void *runtime_init_lua_audio(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_audio_ctx = lua_audio_init(
        runtime->common_contexts.lua_ctx->L, runtime->config->project_dir);
    return runtime->common_contexts.lua_audio_ctx;
}

static void *runtime_init_lua_sqlite(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.sqlite_ctx = lua_sqlite_init(
        runtime->common_contexts.lua_ctx->L, runtime->config->project_dir);
    return runtime->common_contexts.sqlite_ctx;
}

static void *runtime_init_lua_file(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.file_ctx = lua_file_init(
        runtime->common_contexts.lua_ctx->L, runtime->config->project_dir);
    if (runtime->common_contexts.file_ctx)
        file_set_write_root(runtime->common_contexts.file_ctx, runtime->config->file_root);
    return runtime->common_contexts.file_ctx;
}

static void *runtime_init_lua_network(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_network_ctx = lua_network_init(
        runtime->common_contexts.lua_ctx->L, runtime->config->project_dir);
    return runtime->common_contexts.lua_network_ctx;
}

static void *runtime_init_lua_magneto(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.magneto_ctx = lua_magneto_init(runtime->common_contexts.lua_ctx->L);
    return runtime->common_contexts.magneto_ctx;
}

static void *runtime_init_lua_device(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.device_ctx = managed_basics_init_lua(runtime->common_contexts.lua_ctx->L);
    return runtime->common_contexts.device_ctx;
}

static void *runtime_init_lua_udp(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_udp_ctx = lua_udp_init(runtime->common_contexts.lua_ctx->L);
    runtime->common_contexts.udp_ctx = lua_udp_context(runtime->common_contexts.lua_udp_ctx);
    return runtime->common_contexts.lua_udp_ctx;
}

static void *runtime_init_lua_midi(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.lua_midi_ctx = lua_midi_init(runtime->common_contexts.lua_ctx->L);
    if (runtime->common_contexts.lua_midi_ctx)
        lua_midi_set_rtpmidi(runtime->common_contexts.lua_midi_ctx, runtime->common_contexts.rtpmidi_ctx);
    return runtime->common_contexts.lua_midi_ctx;
}

static void *runtime_init_wasm_canvas(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    runtime->common_contexts.wasm_ctx = wasm_canvas_create(runtime->config->project_dir);
    if (runtime->common_contexts.wasm_ctx)
    {
        runtime->common_contexts.file_ctx = wasm_canvas_file_context(runtime->common_contexts.wasm_ctx);
        runtime->common_contexts.udp_ctx = wasm_canvas_udp_context(runtime->common_contexts.wasm_ctx);
        if (runtime->common_contexts.file_ctx)
            file_set_write_root(runtime->common_contexts.file_ctx, runtime->config->file_root);
    }
    return runtime->common_contexts.wasm_ctx;
}

#ifdef BUDO_NEURAL
static void neural_cleanup_adapter(void *opaque)
{
    neural_destroy((NeuralContext *)opaque);
}

static void *runtime_init_js_neural(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    if (!runtime->metadata->neural_enabled)
        return NULL;
    runtime->common_contexts.neural_ctx = neural_create(runtime->config->project_dir);
    if (runtime->common_contexts.neural_ctx)
        js_neural_init(runtime->common_contexts.js_ctx->context, runtime->common_contexts.neural_ctx);
    return runtime->common_contexts.neural_ctx;
}

static void *runtime_init_lua_neural(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    if (!runtime->metadata->neural_enabled)
        return NULL;
    runtime->common_contexts.neural_ctx = neural_create(runtime->config->project_dir);
    if (runtime->common_contexts.neural_ctx)
        lua_neural_init(runtime->common_contexts.lua_ctx->L, runtime->common_contexts.neural_ctx);
    return runtime->common_contexts.neural_ctx;
}
#endif

static const SubsystemDescriptor runtime_js_subsystems[] = {
    RUNTIME_SUBSYSTEM("JavaScript core", SUBSYSTEM_ROLE_CORE, runtime_init_js_core, NULL, managed_js_core_cleanup, false),
    RUNTIME_SUBSYSTEM("JavaScript canvas", SUBSYSTEM_ROLE_CANVAS, runtime_init_js_canvas, NULL, managed_js_canvas_cleanup, false),
    RUNTIME_SUBSYSTEM("JavaScript audio", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_audio, NULL, managed_js_audio_cleanup, true),
    RUNTIME_SUBSYSTEM("JavaScript SQLite", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_sqlite, NULL, managed_js_sqlite_cleanup, true),
    RUNTIME_SUBSYSTEM("JavaScript file", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_file, managed_js_file_poll, managed_js_file_cleanup, true),
#ifdef BUDO_LLAMACPP
    RUNTIME_SUBSYSTEM("JavaScript llama.cpp", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_llamacpp, managed_js_llamacpp_poll, managed_js_llamacpp_cleanup, true),
#endif
    RUNTIME_SUBSYSTEM("JavaScript network", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_network, managed_js_network_poll, managed_js_network_cleanup, true),
    RUNTIME_SUBSYSTEM("JavaScript magnetometer", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_magneto, NULL, managed_js_magneto_cleanup, true),
    RUNTIME_SUBSYSTEM("JavaScript device", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_device, NULL, managed_device_cleanup, false),
    RUNTIME_SUBSYSTEM("JavaScript UDP", SUBSYSTEM_ROLE_UDP, runtime_init_js_udp, js_udp_poll_adapter, js_udp_cleanup_adapter, true),
    RUNTIME_SUBSYSTEM("RTP-MIDI", SUBSYSTEM_ROLE_RTP_MIDI, runtime_init_rtpmidi, NULL, rtpmidi_cleanup_adapter, true),
    RUNTIME_SUBSYSTEM("JavaScript MIDI", SUBSYSTEM_ROLE_MIDI, runtime_init_js_midi, managed_js_midi_poll, managed_js_midi_cleanup, false),
#ifdef BUDO_NEURAL
    RUNTIME_SUBSYSTEM("JavaScript neural", SUBSYSTEM_ROLE_SERVICE, runtime_init_js_neural, NULL, neural_cleanup_adapter, true),
#endif
};

static const SubsystemDescriptor runtime_lua_subsystems[] = {
    RUNTIME_SUBSYSTEM("Lua canvas", SUBSYSTEM_ROLE_CANVAS, runtime_init_lua_canvas,
                      NULL, managed_lua_canvas_cleanup, false),
    RUNTIME_SUBSYSTEM("Lua audio", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_audio,
                      NULL, managed_lua_audio_cleanup, true),
    RUNTIME_SUBSYSTEM("Lua SQLite", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_sqlite,
                      NULL, managed_lua_sqlite_cleanup, true),
    RUNTIME_SUBSYSTEM("Lua file", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_file,
                      NULL, managed_lua_file_cleanup, true),
#ifdef BUDO_LLAMACPP
    RUNTIME_SUBSYSTEM("Lua llama.cpp", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_llamacpp,
                      managed_lua_llamacpp_poll, managed_lua_llamacpp_cleanup, true),
#endif
    RUNTIME_SUBSYSTEM("Lua network", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_network,
                      managed_lua_network_poll, managed_lua_network_cleanup, true),
    RUNTIME_SUBSYSTEM("Lua magnetometer", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_magneto,
                      NULL, managed_lua_magneto_cleanup, true),
    RUNTIME_SUBSYSTEM("Lua device", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_device,
                      NULL, managed_device_cleanup, false),
    RUNTIME_SUBSYSTEM("Lua UDP", SUBSYSTEM_ROLE_UDP, runtime_init_lua_udp,
                      lua_udp_poll_adapter, lua_udp_cleanup_adapter, true),
    RUNTIME_SUBSYSTEM("RTP-MIDI", SUBSYSTEM_ROLE_RTP_MIDI, runtime_init_rtpmidi,
                      NULL, rtpmidi_cleanup_adapter, true),
    RUNTIME_SUBSYSTEM("Lua MIDI", SUBSYSTEM_ROLE_MIDI, runtime_init_lua_midi,
                      managed_lua_midi_poll, managed_lua_midi_cleanup, false),
#ifdef BUDO_NEURAL
    RUNTIME_SUBSYSTEM("Lua neural", SUBSYSTEM_ROLE_SERVICE, runtime_init_lua_neural,
                      NULL, neural_cleanup_adapter, true),
#endif
};

static const SubsystemDescriptor runtime_wasm_subsystems[] = {
    RUNTIME_SUBSYSTEM("WebAssembly canvas", SUBSYSTEM_ROLE_CANVAS, runtime_init_wasm_canvas,
                      NULL, wasm_canvas_cleanup_adapter, false),
};

#undef RUNTIME_SUBSYSTEM

static bool runtime_compose_subsystems(
    RuntimeInstance *runtime, const SubsystemDescriptor *descriptors,
    size_t descriptor_count)
{
    const char *failed_subsystem = NULL;

    if (subsystem_compose(&runtime->subsystems, runtime, descriptors,
                          descriptor_count, &failed_subsystem))
        return true;

    fprintf(stderr, "Error: Failed to initialize %s subsystem\n",
            failed_subsystem ? failed_subsystem : "runtime");
    return false;
}

static void runtime_adapter_pause(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    if (runtime)
        subsystem_registry_pause(&runtime->subsystems);
}

static void runtime_adapter_resume(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    if (runtime)
        subsystem_registry_resume(&runtime->subsystems);
}

static void runtime_adapter_context_lost(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    if (runtime)
        subsystem_registry_context_lost(&runtime->subsystems);
}

static void runtime_adapter_shutdown(void *opaque)
{
    RuntimeInstance *runtime = opaque;

    if (!runtime)
        return;

    managed_runtime_shutdown(&runtime->subsystems);
    window_reset_app_resources(runtime->window);
    runtime->loaded = false;
}

static bool runtime_adapter_initialize(void *opaque)
{
    RuntimeInstance *runtime = opaque;
    const AppConfig *config = runtime->config;
    const AppMetadata *metadata = runtime->metadata;
    Window *window = runtime->window;
    InputState *input = &runtime->input;
    int width, height;
    SkiaCanvas *canvas = window_get_canvas(window);

    window_get_size(window, &width, &height);

    if (config->runtime_type == RUNTIME_JAVASCRIPT)
    {
        if (!runtime_compose_subsystems(
                runtime, runtime_js_subsystems,
                sizeof(runtime_js_subsystems) / sizeof(runtime_js_subsystems[0])))
            return false;

        js_graphic_set_frame_context(runtime->common_contexts.js_graphic_ctx, canvas, input, window, width, height,
                                     window_get_dpi_scale(window));

        if (!js_runtime_load_file(runtime->common_contexts.js_ctx, config->main_script))
        {
            fprintf(stderr, "Error: Failed to load script '%s'\n", config->main_script);
            return false;
        }
        printf("JavaScript loaded successfully\n");
    }
    else if (config->runtime_type == RUNTIME_LUA)
    {
        if (!runtime_compose_subsystems(
                runtime, runtime_lua_subsystems,
                sizeof(runtime_lua_subsystems) / sizeof(runtime_lua_subsystems[0])))
            return false;

        lua_canvas_set_context(runtime->common_contexts.lua_ctx, canvas, input, window, width, height,
                               window_get_dpi_scale(window));

        if (!lua_canvas_load_file(runtime->common_contexts.lua_ctx, config->main_script))
        {
            fprintf(stderr, "Error: Failed to load script '%s'\n", config->main_script);
            return false;
        }
        printf("Lua loaded successfully\n");
    }
    else
    {
        bool loaded = false;

        if (!runtime_compose_subsystems(
                runtime, runtime_wasm_subsystems,
                sizeof(runtime_wasm_subsystems) / sizeof(runtime_wasm_subsystems[0])))
            return false;
#ifdef BUDO_NEURAL
        if (metadata->neural_enabled)
        {
            if (!wasm_canvas_enable_neural(runtime->common_contexts.wasm_ctx))
                fprintf(stderr, "neural: failed to create context for WASM runtime\n");
        }
#endif

        wasm_canvas_set_context(runtime->common_contexts.wasm_ctx, canvas, input, window, width, height,
                                window_get_dpi_scale(window));

        if (ends_with(config->main_script, ".wat"))
            loaded = wasm_canvas_load_wat_file(runtime->common_contexts.wasm_ctx, config->main_script);
        else
            loaded = wasm_canvas_load_wasm_file(runtime->common_contexts.wasm_ctx, config->main_script);

        if (!loaded)
        {
            fprintf(stderr, "Error: Failed to load module '%s': %s\n",
                    config->main_script, wasm_canvas_get_error(runtime->common_contexts.wasm_ctx));
            return false;
        }
        printf("WebAssembly module loaded successfully\n");
    }

    runtime->loaded = true;
    return true;
}

static void js_runtime_frame(void *opaque, double timestamp, double delta_seconds)
{
    RuntimeInstance *runtime = opaque;
    Window *window = runtime->window;
    int width, height;
    (void)delta_seconds;

    window_get_size(window, &width, &height);
    managed_runtime_frame(MANAGED_RUNTIME_JAVASCRIPT, &runtime->subsystems,
                          runtime->common_contexts.js_ctx, runtime->common_contexts.js_graphic_ctx, NULL, NULL,
                          window_get_canvas(window), width, height,
                          window, &runtime->input,
                          window_get_dpi_scale(window), timestamp);
}

static void lua_runtime_frame(void *opaque, double timestamp, double delta_seconds)
{
    RuntimeInstance *runtime = opaque;
    Window *window = runtime->window;
    int width, height;
    (void)delta_seconds;

    window_get_size(window, &width, &height);
    managed_runtime_frame(MANAGED_RUNTIME_LUA, &runtime->subsystems,
                          NULL, NULL, runtime->common_contexts.lua_ctx, NULL,
                          window_get_canvas(window), width, height,
                          window, &runtime->input,
                          window_get_dpi_scale(window), timestamp);
}

static void wasm_runtime_frame(void *opaque, double timestamp, double delta_seconds)
{
    RuntimeInstance *runtime = opaque;
    Window *window = runtime->window;
    int width, height;
    (void)delta_seconds;

    window_get_size(window, &width, &height);
    managed_runtime_frame(MANAGED_RUNTIME_WEBASSEMBLY, &runtime->subsystems,
                          NULL, NULL, NULL, runtime->common_contexts.wasm_ctx,
                          window_get_canvas(window), width, height,
                          window, &runtime->input,
                          window_get_dpi_scale(window), timestamp);
}

static const ApplicationDriverOps js_runtime_driver_ops = {
    .initialize = runtime_adapter_initialize,
    .frame = js_runtime_frame,
    .pause = runtime_adapter_pause,
    .resume = runtime_adapter_resume,
    .context_lost = runtime_adapter_context_lost,
    .shutdown = runtime_adapter_shutdown,
};

static const ApplicationDriverOps lua_runtime_driver_ops = {
    .initialize = runtime_adapter_initialize,
    .frame = lua_runtime_frame,
    .pause = runtime_adapter_pause,
    .resume = runtime_adapter_resume,
    .context_lost = runtime_adapter_context_lost,
    .shutdown = runtime_adapter_shutdown,
};

static const ApplicationDriverOps wasm_runtime_driver_ops = {
    .initialize = runtime_adapter_initialize,
    .frame = wasm_runtime_frame,
    .pause = runtime_adapter_pause,
    .resume = runtime_adapter_resume,
    .context_lost = runtime_adapter_context_lost,
    .shutdown = runtime_adapter_shutdown,
};

static bool runtime_instance_load(RuntimeInstance *runtime,
                                  const AppConfig *config,
                                  const AppMetadata *metadata,
                                  Window *window,
                                  InputState *input)
{
    const ApplicationDriverOps *ops = &js_runtime_driver_ops;
    int width, height;

    runtime_instance_init(runtime);
    runtime->runtime_type = config->runtime_type;
    runtime->config = config;
    runtime->metadata = metadata;
    runtime->window = window;
    if (config->runtime_type == RUNTIME_LUA)
        ops = &lua_runtime_driver_ops;
    else if (config->runtime_type == RUNTIME_WEBASSEMBLY)
        ops = &wasm_runtime_driver_ops;

    application_driver_init(&runtime->driver, ops, runtime);
    if (!application_driver_initialize(&runtime->driver))
        return false;
    application_driver_surface_created(&runtime->driver);
    window_get_size(window, &width, &height);
    application_driver_resize(&runtime->driver, width, height,
                              window_get_dpi_scale(window));
    return true;
}

static void runtime_instance_destroy(RuntimeInstance *runtime)
{
    if (!runtime)
        return;
    application_driver_destroy(&runtime->driver);
    runtime_instance_init(runtime);
}

typedef struct ManagedDesktopApplication
{
    RuntimeInstance *runtime;
    AppConfig *config;
    AppMetadata *metadata;
    FileWatcher *watcher;
} ManagedDesktopApplication;

static ApplicationDriver *managed_desktop_get_driver(void *opaque)
{
    ManagedDesktopApplication *application = opaque;
    return application->runtime->loaded ? &application->runtime->driver : NULL;
}

static void managed_desktop_before_frame(void *opaque, Window *window,
                                         InputState *input, double now)
{
    ManagedDesktopApplication *application = opaque;
    RuntimeInstance *runtime = application->runtime;
    AppConfig *config = application->config;
    AppMetadata *metadata = application->metadata;
    bool metadata_loaded;

    if (!application->watcher ||
        !file_watcher_poll(application->watcher, now))
        return;

    printf("\nChange detected. Reloading project...\n");
    runtime_instance_destroy(runtime);
    if (!refresh_single_file_app(config))
    {
        fprintf(stderr, "Watch mode: reload failed; waiting for the next change.\n");
        return;
    }
    if (!resolve_main_script(config))
    {
        fprintf(stderr,
                "Error: Cannot find 'main.ts', 'main.js', 'main.lua', 'main.wat', "
                "or 'main.wasm' in project directory '%s'\n",
                config->project_dir);
        return;
    }

    metadata_loaded = app_metadata_load(config->project_dir, metadata);
    if (config->single_file_mode && !metadata_loaded)
        metadata_name_from_single_file(config->single_file_source, metadata);
    printf("Runtime: %s\n", runtime_type_name(config->runtime_type));
    printf("Main script: %s\n", config->main_script);
    if (!runtime_instance_load(runtime, config, metadata, window, input))
    {
        runtime_instance_destroy(runtime);
        fprintf(stderr, "Watch mode: reload failed; waiting for the next change.\n");
        return;
    }
    printf("App: %s v%s by %s (%s)\n",
           metadata->name, metadata->version, metadata->author, metadata->date);
}

int budo_cli_main(int argc, char **argv)
{
    
    AppConfig app_config;
    if (!parse_args(argc, argv, &app_config))
    {
        return 1;
    }

    switch (app_config.command)
    {
    case CMD_HELP:
        print_usage(argv[0]);
        return 0;

    case CMD_PRINT_DTS:
        return embedded_resource_write_stdout(embedded_types_data, embedded_types_len,
                                              embedded_types_uncompressed_len,
                                              embedded_types_is_gzip) == 0
                   ? 0
                   : 1;

    case CMD_PRINT_DEVAPI:
        return embedded_resource_write_stdout(embedded_devapi_data, embedded_devapi_len,
                                              embedded_devapi_uncompressed_len,
                                              embedded_devapi_is_gzip) == 0
                   ? 0
                   : 1;

    case CMD_INIT:
        return budo_run_init(app_config.project_dir, &app_config.init_options);

    case CMD_COMPILE:
    {
        NativeCompileOptions options = {
            .build_directory = app_config.native_build_directory,
            .sdk_directory = app_config.native_sdk_directory,
            .run_after_build = app_config.native_run_after_build,
            .offline = app_config.native_offline,
            .configuration = app_config.native_configuration,
            .sanitizers = app_config.native_sanitizers,
        };
        return native_compile_project(app_config.project_dir, &options);
    }

    case CMD_NATIVE_CACHE_INSPECT:
        return native_compile_cache_inspect(app_config.native_cache_json);

    case CMD_NATIVE_CACHE_CLEAN:
        return native_compile_cache_clean_all();

    case CMD_NATIVE_SDK_CACHE_INSPECT:
        return native_compile_sdk_cache_inspect(app_config.native_cache_json);

    case CMD_NATIVE_SDK_CACHE_CLEAN:
        return native_compile_sdk_cache_clean_all();

    case CMD_WEB_EXPORT:
        return web_export(app_config.project_dir, app_config.export_web_output);

    case CMD_ANDROID_APK:
    case CMD_ANDROID_AAB:
    {
        AndroidPackageOptions opts = {
            .aab = (app_config.command == CMD_ANDROID_AAB),
            .release = app_config.android_release,
            .no_build = app_config.android_no_build,
            .clean = app_config.android_clean,
            .install = app_config.android_install,
            .output_dir = app_config.android_output_dir,
        };
        return android_package(app_config.project_dir, &opts);
    }

    case CMD_WEB_SERVE:
    {
        
        char tmpl[] = "/tmp/budo-serve-XXXXXX";
        const char *out_dir = mkdtemp(tmpl);
        if (!out_dir)
        {
            fprintf(stderr, "Error: cannot create temp directory for web-serve: %s\n",
                    strerror(errno));
            return 1;
        }
        int rc = web_export(app_config.project_dir, out_dir);
        if (rc != 0)
            return rc;
        return web_server_serve_directory(out_dir, app_config.web_listen_host,
                                          app_config.web_listen_port);
    }

    case CMD_RUN:
        
        break;
    }

    printf("Budo Runtime\n");
    printf("Runtime: %s\n", runtime_type_name(app_config.runtime_type));
    printf("Loading project: %s\n", app_config.project_dir);
    if (app_config.single_file_mode)
        printf("Single-file app: %s\n", app_config.single_file_source);
    if (app_config.from_input_mode)
        printf("Input app: stdin (%s)\n", app_config.from_input_language);
    printf("Main script: %s\n", app_config.main_script);
    if (app_config.watch)
        printf("Watch mode: enabled\n");

    AppMetadata metadata;
    bool metadata_loaded = app_metadata_load(app_config.project_dir, &metadata);
    if (!metadata.valid)
    {
        cleanup_single_file_app(&app_config);
        return 1;
    }
    if (app_config.single_file_mode && !metadata_loaded)
        metadata_name_from_single_file(app_config.single_file_source, &metadata);
    if (app_config.from_input_mode && !metadata_loaded)
        snprintf(metadata.name, sizeof(metadata.name), "stdin");

    WindowConfig window_config = {
        .title = app_config.title,
        .project_dir = app_config.project_dir,
        .width = app_config.width,
        .height = app_config.height,
        .resizable = true,
        .fullscreen = app_config.fullscreen,
        .vsync = app_config.vsync};

    Window *window = window_create(&window_config);
    if (!window)
    {
        fprintf(stderr, "Error: Failed to create window\n");
        cleanup_single_file_app(&app_config);
        return 1;
    }

    RuntimeInstance runtime;
    input_init(&runtime.input);
    runtime_instance_init(&runtime);
    if (!runtime_instance_load(&runtime, &app_config, &metadata, window, &runtime.input))
    {
        runtime_instance_destroy(&runtime);
        window_destroy(window);
        cleanup_single_file_app(&app_config);
        return 1;
    }

    printf("App: %s v%s by %s (%s)\n",
           metadata.name, metadata.version, metadata.author, metadata.date);

    FileWatcher *watcher = NULL;
    if (app_config.watch)
    {
        watcher = file_watcher_create(app_config.watch_root, window_get_time(window));
        if (!watcher)
        {
            fprintf(stderr, "Warning: failed to start watch mode for '%s'\n",
                    app_config.watch_root);
        }
        else if (file_watcher_get_error(watcher))
        {
            fprintf(stderr, "Warning: %s\n", file_watcher_get_error(watcher));
        }
    }

    ManagedDesktopApplication managed_application = {
        .runtime = &runtime,
        .config = &app_config,
        .metadata = &metadata,
        .watcher = watcher,
    };
    DesktopHostApplication desktop_application = {
        .context = &managed_application,
        .get_driver = managed_desktop_get_driver,
        .before_frame = managed_desktop_before_frame,
    };
    desktop_host_run(window, &runtime.input, &desktop_application);

    printf("Shutting down...\n");

    file_watcher_destroy(watcher);
    runtime_instance_destroy(&runtime);
    window_destroy(window);
    cleanup_single_file_app(&app_config);

    return 0;
}