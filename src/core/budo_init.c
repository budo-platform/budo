#include "budo_init.h"
#include "core/embedded_resource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "embedded_devapi.h"
#include "embedded_types.h"
#include "embedded_native_canvas.h"
#include "embedded_native_gpu.h"
#include "core/version.h"

static int file_exists_local(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int write_embedded_file(const char *filename,
                               const unsigned char *data,
                               size_t len,
                               size_t uncompressed_len,
                               int is_gzip)
{
    if (file_exists_local(filename))
    {
        printf("  %-20s already exists — skipping.\n", filename);
        return 0;
    }

    size_t written_len = 0;
    if (embedded_resource_write_file(filename, data, len, uncompressed_len, is_gzip, &written_len) != 0)
    {
        fprintf(stderr, "Error: cannot create '%s'\n", filename);
        return -1;
    }
    printf("  %-20s created  (%zu bytes)\n", filename, written_len);
    return 0;
}

static void prompt_read(const char *prompt, char *buf, size_t bufsz)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)bufsz, stdin))
        buf[0] = '\0';

    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
}

static void prompt_default(const char *label, const char *def,
                           char *buf, size_t bufsz)
{
    char prompt[512];
    snprintf(prompt, sizeof(prompt), "  %s (%s): ", label, def);
    prompt_read(prompt, buf, bufsz);
    if (buf[0] == '\0')
    {
        strncpy(buf, def, bufsz - 1);
        buf[bufsz - 1] = '\0';
    }
}

static void prompt_optional(const char *label, char *buf, size_t bufsz)
{
    char prompt[512];
    snprintf(prompt, sizeof(prompt), "  %s (optional, press Enter to skip): ", label);
    prompt_read(prompt, buf, bufsz);
}

static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_sz; i++)
    {
        unsigned char c = (unsigned char)in[i];
        if (c == '"')
        {
            if (j + 2 < out_sz)
            {
                out[j++] = '\\';
                out[j++] = '"';
            }
        }
        else if (c == '\\')
        {
            if (j + 2 < out_sz)
            {
                out[j++] = '\\';
                out[j++] = '\\';
            }
        }
        else if (c < 0x20)
        {
            
        }
        else
        {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

static int ensure_directory(const char *path)
{
    if (!path || !*path)
        return 0;

    struct stat st;
    if (stat(path, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return 0;
        fprintf(stderr, "Error: '%s' exists but is not a directory\n", path);
        return -1;
    }

    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    char *slash = strrchr(tmp, '/');
    if (slash && slash != tmp)
    {
        *slash = '\0';
        if (ensure_directory(tmp) != 0)
            return -1;
    }

    if (mkdir(path, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Error: cannot create directory '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    return 0;
}

static const char *DEFAULT_MAIN_JS =
    "/// <reference path=\"./budo.d.ts\" />\n"
    "\n"
    "function frame(timestamp) {\n"
    "    sys.canvas.clear(\"#222831\");\n"
    "    sys.canvas.setFillColor(\"#eeeeee\");\n"
    "    sys.canvas.drawText(\"Hello, Budo!\", 24, 48, 28);\n"
    "    sys.animation.requestFrame(frame);\n"
    "}\n"
    "\n"
    "sys.animation.requestFrame(frame);\n";

static const char *DEFAULT_JSCONFIG_JSON =
    "{\n"
    "  \"compilerOptions\": {\n"
    "    \"target\": \"ES2020\",\n"
    "    \"module\": \"ES2020\",\n"
    "    \"checkJs\": true,\n"
    "    \"noEmit\": true,\n"
    "    \"noImplicitAny\": false,\n"
    "    \"strictNullChecks\": false,\n"
    "    \"lib\": [\n"
    "      \"ES2020\"\n"
    "    ],\n"
    "    \"types\": []\n"
    "  },\n"
    "  \"include\": [\n"
    "    \"**/*.js\",\n"
    "    \"budo.d.ts\"\n"
    "  ]\n"
    "}\n";

static int run_native_init(const char *target_dir, BudoInitTemplate template_kind)
{
    static const char *managed_entries[] = {
        "main.ts", "main.js", "main.lua", "main.wat", "main.wasm"};
    const unsigned char *source = template_kind == BUDO_INIT_TEMPLATE_GPU
                                      ? embedded_native_gpu_data
                                      : embedded_native_canvas_data;
    size_t source_len = template_kind == BUDO_INIT_TEMPLATE_GPU
                            ? embedded_native_gpu_len
                            : embedded_native_canvas_len;
    size_t source_uncompressed_len =
        template_kind == BUDO_INIT_TEMPLATE_GPU
            ? embedded_native_gpu_uncompressed_len
            : embedded_native_canvas_uncompressed_len;
    int source_is_gzip = template_kind == BUDO_INIT_TEMPLATE_GPU
                             ? embedded_native_gpu_is_gzip
                             : embedded_native_canvas_is_gzip;
    char manifest[1024];
    char readme[2048];
    static const char *gitignore = ".budo/\n";
    static const char *vscode_settings =
        "{\n"
        "  \"C_Cpp.default.compileCommands\": \"${workspaceFolder}/.budo/native/build/compile_commands.json\"\n"
        "}\n";

    for (size_t i = 0; i < sizeof(managed_entries) / sizeof(managed_entries[0]); i++)
    {
        if (file_exists_local(managed_entries[i]))
        {
            fprintf(stderr,
                    "Error: cannot initialize a native C project over managed entrypoint '%s'.\n",
                    managed_entries[i]);
            return 1;
        }
    }

    fprintf(stderr,
            "Warning: native Budo projects are trusted, unsandboxed code.\n"
            "Only compile and run source you trust.\n");
    printf("Writing native C project files:\n");
    if (ensure_directory(".vscode") != 0)
        return 1;
    snprintf(manifest, sizeof(manifest),
             "{\n"
             "  \"schema_version\": 1,\n"
             "  \"name\": \"Budo Native App\",\n"
             "  \"sources\": [\"main.c\"],\n"
             "  \"modules\": [\"core\"],\n"
             "  \"c_standard\": 11,\n"
             "  \"sdk_version\": \"%s\"\n"
             "}\n",
             BUDO_VERSION_STRING);
    snprintf(readme, sizeof(readme),
             "# Budo Native C App\n\n"
             "This is a trusted, unsandboxed native C application. It implements "
             "`budo_get_application()`; Budo owns `main()` and presentation.\n\n"
             "Build with debug symbols:\n\n"
             "    budo compile . --debug --run\n\n"
             "Instrument application code with AddressSanitizer and UBSan (Clang/GCC):\n\n"
             "    budo compile . --debug --sanitize address,undefined\n\n"
             "Package for Android with Budo Pro:\n\n"
             "    budo android-apk .\n\n"
             "The compilation database is generated at "
             "`.budo/native/build/compile_commands.json`.\n");
    if (write_embedded_file("main.c", source, source_len,
                            source_uncompressed_len, source_is_gzip) != 0 ||
        write_embedded_file("budo-native.json", (const unsigned char *)manifest,
                            strlen(manifest), strlen(manifest), 0) != 0 ||
        write_embedded_file(".gitignore", (const unsigned char *)gitignore,
                            strlen(gitignore), strlen(gitignore), 0) != 0 ||
        write_embedded_file(".vscode/settings.json",
                            (const unsigned char *)vscode_settings,
                            strlen(vscode_settings), strlen(vscode_settings), 0) != 0 ||
        write_embedded_file("README.md", (const unsigned char *)readme,
                            strlen(readme), strlen(readme), 0) != 0)
        return 1;

    printf("\nDone! Compile the native application with:\n");
    printf("  budo compile %s --debug --run\n", target_dir);
    printf("Package it for Android with Budo Pro:\n");
    printf("  budo android-apk %s\n\n", target_dir);
    return 0;
}

int budo_run_init(const char *target_dir, const BudoInitOptions *options)
{
    if (!target_dir || !*target_dir)
    {
        fprintf(stderr, "Error: init requires a directory argument\n");
        return 1;
    }

    printf("\nBudo Init\n");
    printf("=============\n\n");

    if (ensure_directory(target_dir) != 0)
        return 1;
    if (chdir(target_dir) != 0)
    {
        fprintf(stderr, "Error: cannot enter directory '%s': %s\n",
                target_dir, strerror(errno));
        return 1;
    }

    printf("Target directory: %s\n\n", target_dir);

    if (options && options->language == BUDO_INIT_NATIVE_C)
        return run_native_init(target_dir, options->template_kind);

    printf("Writing project files:\n");
    if (write_embedded_file("budo-llm.md", embedded_devapi_data, embedded_devapi_len,
                            embedded_devapi_uncompressed_len, embedded_devapi_is_gzip) != 0)
        return 1;
    if (write_embedded_file("budo.d.ts", embedded_types_data, embedded_types_len,
                            embedded_types_uncompressed_len, embedded_types_is_gzip) != 0)
        return 1;
    if (write_embedded_file("jsconfig.json",
                            (const unsigned char *)DEFAULT_JSCONFIG_JSON,
                            strlen(DEFAULT_JSCONFIG_JSON),
                            strlen(DEFAULT_JSCONFIG_JSON),
                            0) != 0)
        return 1;
    if (write_embedded_file("main.js",
                            (const unsigned char *)DEFAULT_MAIN_JS,
                            strlen(DEFAULT_MAIN_JS),
                            strlen(DEFAULT_MAIN_JS),
                            0) != 0)
        return 1;

    if (file_exists_local("app.json"))
    {
        printf("  %-20s already exists — skipping.\n", "app.json");
        printf("\nDone.\n\n");
        return 0;
    }

    char cwd[PATH_MAX] = ".";
    if (!getcwd(cwd, sizeof(cwd)))
        snprintf(cwd, sizeof(cwd), ".");
    const char *default_name = strrchr(cwd, '/');
    default_name = default_name ? default_name + 1 : cwd;

    printf("\nCreating app.json\n");
    printf("(Press Enter to accept the default shown in parentheses)\n\n");

    char name[256], author[256], version[64];
    char description[512], orientation[64], network[256];

    prompt_default("App name", default_name, name, sizeof(name));
    prompt_default("Author", "Unknown", author, sizeof(author));
    prompt_default("Version", "1.0.0", version, sizeof(version));
    prompt_optional("Description", description, sizeof(description));
    prompt_default("Orientation (portrait / landscape / unspecified)",
                   "unspecified", orientation, sizeof(orientation));
    prompt_optional("Network policy (* for any, or comma-separated domains)",
                    network, sizeof(network));

    char e_name[512], e_author[512], e_version[128];
    char e_desc[1024], e_orient[128], e_net[512];
    json_escape(name, e_name, sizeof(e_name));
    json_escape(author, e_author, sizeof(e_author));
    json_escape(version, e_version, sizeof(e_version));
    json_escape(description, e_desc, sizeof(e_desc));
    json_escape(orientation, e_orient, sizeof(e_orient));
    json_escape(network, e_net, sizeof(e_net));

    FILE *f = fopen("app.json", "w");
    if (!f)
    {
        fprintf(stderr, "Error: cannot create 'app.json'\n");
        return 1;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"%s\",\n", e_name);
    fprintf(f, "  \"author\": \"%s\",\n", e_author);
    fprintf(f, "  \"version\": \"%s\"", e_version);
    if (e_desc[0])
        fprintf(f, ",\n  \"description\": \"%s\"", e_desc);
    if (e_orient[0] && strcmp(e_orient, "unspecified") != 0)
        fprintf(f, ",\n  \"orientation\": \"%s\"", e_orient);
    if (e_net[0])
        fprintf(f, ",\n  \"network\": \"%s\"", e_net);
    fprintf(f, "\n}\n");
    fclose(f);

    printf("\n  %-20s created.\n", "app.json");
    printf("\nDone!  Run your app with:\n");
    printf("  budo run %s\n\n", target_dir);

    return 0;
}