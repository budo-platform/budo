#include "cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/version.h"
#include "build_info.h"

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600
#define DEFAULT_TITLE "Budo"

void budo_cli_print_version(void)
{
    printf("budo " BUDO_VERSION_STRING "\n"
           "commit:     " BUDO_BUILD_COMMIT "\n"
           "platform:   " BUDO_BUILD_TARGET "\n"
           "javascript: " BUDO_BUILD_QUICKJS "\n"
           "native api: %u.%u\n",
           (unsigned)BUDO_NATIVE_API_VERSION_MAJOR, (unsigned)BUDO_NATIVE_API_VERSION_MINOR);
}

void budo_cli_print_usage(const char *program_name)
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
            "  version                         Print the version and build details\n"
            "                                  (also --version, -v).\n"
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

static const char *take_option_value(int argc, char **argv, int *index)
{
    if (*index + 1 >= argc)
    {
        fprintf(stderr, "Error: %s requires a value\n", argv[*index]);
        return NULL;
    }
    return argv[++*index];
}

static bool parse_window_dimension(const char *option, const char *value, int *out)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1 || parsed > 16384)
    {
        fprintf(stderr, "Error: %s expects a whole number of pixels between 1 and 16384, got '%s'\n",
                option, value);
        return false;
    }
    *out = (int)parsed;
    return true;
}

static bool parse_run_options(int argc, char **argv, int start, BudoRunOptions *run)
{
    for (int i = start; i < argc; i++)
    {
        const char *value;

        if (strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "--height") == 0)
        {
            const char *option = argv[i];
            if (!(value = take_option_value(argc, argv, &i)) ||
                !parse_window_dimension(option, value,
                                        strcmp(option, "--width") == 0 ? &run->width
                                                                       : &run->height))
                return false;
        }
        else if (strcmp(argv[i], "--title") == 0)
        {
            if (!(value = take_option_value(argc, argv, &i)))
                return false;
            run->title = value;
        }
        else if (strcmp(argv[i], "--fullscreen") == 0)
        {
            run->fullscreen = true;
        }
        else if (strcmp(argv[i], "--no-vsync") == 0)
        {
            run->vsync = false;
        }
        else if (strcmp(argv[i], "--watch") == 0)
        {
            run->watch = true;
        }
        else if (strcmp(argv[i], "--from-input") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: --from-input expects js, ts, lua, wat, or wasm\n");
                return false;
            }
            run->from_input_language = argv[++i];
        }
        else if (strcmp(argv[i], "--file-root") == 0)
        {
            if (!(value = take_option_value(argc, argv, &i)))
                return false;
            run->file_root = value;
        }
        else if (argv[i][0] != '-')
        {
            if (run->target == NULL)
            {
                run->target = argv[i];
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

bool budo_cli_parse(int argc, char **argv, BudoCliOptions *config)
{
    memset(config, 0, sizeof(*config));
    config->command = BUDO_CMD_RUN;
    config->run.width = DEFAULT_WIDTH;
    config->run.height = DEFAULT_HEIGHT;
    config->run.title = DEFAULT_TITLE;
    config->run.vsync = true;
    config->web_listen_port = 8080;
    config->init.language = BUDO_INIT_JAVASCRIPT;
    config->init.template_kind = BUDO_INIT_TEMPLATE_CANVAS;
    config->compile.configuration = NATIVE_BUILD_RELWITHDEBINFO;
    config->compile.sanitizers = NATIVE_SANITIZER_NONE;

    if (argc < 2)
    {
        config->command = BUDO_CMD_HELP;
        return true;
    }

    const char *first = argv[1];

    if (strcmp(first, "help") == 0 ||
        strcmp(first, "--help") == 0 ||
        strcmp(first, "-h") == 0)
    {
        config->command = BUDO_CMD_HELP;
        return true;
    }

    if (strcmp(first, "version") == 0 ||
        strcmp(first, "--version") == 0 ||
        strcmp(first, "-v") == 0)
    {
        config->command = BUDO_CMD_VERSION;
        return true;
    }

    if (strcmp(first, "budo.d.ts") == 0)
    {
        config->command = BUDO_CMD_PRINT_DTS;
        return true;
    }
    if (strcmp(first, "budo-llm.md") == 0)
    {
        config->command = BUDO_CMD_PRINT_DEVAPI;
        return true;
    }

    if (strcmp(first, "init") == 0)
    {
        config->command = BUDO_CMD_INIT;
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
                    config->init.language = BUDO_INIT_NATIVE_C;
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
                    config->init.template_kind = BUDO_INIT_TEMPLATE_GPU;
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
        if (template_seen && config->init.language != BUDO_INIT_NATIVE_C)
        {
            fprintf(stderr, "Error: --template is only valid with --language c.\n");
            return false;
        }
        return true;
    }

    if (strcmp(first, "compile") == 0)
    {
        config->command = BUDO_CMD_COMPILE;
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
                config->compile.run_after_build = true;
            else if (strcmp(argv[i], "--offline") == 0)
                config->compile.offline = true;
            else if (strcmp(argv[i], "--debug") == 0 ||
                     strcmp(argv[i], "--release") == 0)
            {
                if (configuration_seen)
                {
                    fprintf(stderr, "Error: choose only one native build configuration.\n");
                    return false;
                }
                configuration_seen = true;
                config->compile.configuration = strcmp(argv[i], "--debug") == 0
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
                    config->compile.sanitizers = NATIVE_SANITIZER_ADDRESS;
                else if (strcmp(value, "undefined") == 0)
                    config->compile.sanitizers = NATIVE_SANITIZER_UNDEFINED;
                else if (strcmp(value, "address,undefined") == 0 ||
                         strcmp(value, "undefined,address") == 0)
                    config->compile.sanitizers = NATIVE_SANITIZER_ADDRESS |
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
                config->compile.build_directory = argv[++i];
            }
            else if (strcmp(argv[i], "--sdk") == 0)
            {
                if (i + 1 >= argc || argv[i + 1][0] == '-')
                {
                    fprintf(stderr, "Error: --sdk requires a directory argument.\n");
                    return false;
                }
                config->compile.sdk_directory = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: Unknown option '%s' for compile\n", argv[i]);
                return false;
            }
        }
        if (config->compile.configuration == NATIVE_BUILD_RELEASE &&
            config->compile.sanitizers != NATIVE_SANITIZER_NONE)
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
            config->command = BUDO_CMD_NATIVE_CACHE_INSPECT;
            if (argc == 5 && strcmp(argv[4], "--json") == 0)
                config->cache_json = true;
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
            config->command = BUDO_CMD_NATIVE_CACHE_CLEAN;
            return true;
        }
        if (argc >= 4 && strcmp(argv[2], "sdk") == 0 &&
            strcmp(argv[3], "inspect") == 0)
        {
            config->command = BUDO_CMD_NATIVE_SDK_CACHE_INSPECT;
            if (argc == 5 && strcmp(argv[4], "--json") == 0)
                config->cache_json = true;
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
            config->command = BUDO_CMD_NATIVE_SDK_CACHE_CLEAN;
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
        config->command = BUDO_CMD_WEB_SERVE;
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
        config->command = BUDO_CMD_WEB_EXPORT;
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
                config->web_export_output = argv[++i];
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
                              ? BUDO_CMD_ANDROID_AAB
                              : BUDO_CMD_ANDROID_APK;
        config->android.aab = (config->command == BUDO_CMD_ANDROID_AAB);
        config->android.release = config->android.aab;
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
                config->android.release = true;
            else if (strcmp(argv[i], "--debug") == 0)
                config->android.release = false;
            else if (strcmp(argv[i], "--install") == 0)
            {
                if (config->command != BUDO_CMD_ANDROID_APK)
                {
                    fprintf(stderr, "Error: --install is only supported for android-apk\n");
                    return false;
                }
                config->android.install = true;
            }
            else if (strcmp(argv[i], "--no-build") == 0)
                config->android.no_build = true;
            else if (strcmp(argv[i], "--clean") == 0)
                config->android.clean = true;
            else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
                config->android.output_dir = argv[++i];
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

    config->command = BUDO_CMD_RUN;
    if (!parse_run_options(argc, argv, opt_start, &config->run))
        return false;

    if (config->run.from_input_language)
    {
        if (config->run.target != NULL)
        {
            fprintf(stderr, "Error: --from-input cannot be combined with a project path\n");
            return false;
        }
        if (config->run.watch)
        {
            fprintf(stderr, "Error: --watch cannot be used with --from-input\n");
            return false;
        }
    }
    return true;
}