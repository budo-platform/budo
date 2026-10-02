#include "managed_desktop.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "core/app_entrypoint.h"
#include "core/app_metadata.h"
#include "core/application_driver.h"
#include "core/file_watcher.h"
#include "core/fs_util.h"
#include "core/input.h"
#include "core/managed_runtime.h"
#include "core/managed_subsystems.h"
#include "core/subsystem_composition.h"
#include "core/subsystem_registry.h"
#include "core/window.h"
#include "desktop/desktop_host.h"
#include "file/file_wrapper.h"
#include "graphics/js_canvas_bindings.h"
#include "graphics/lua_canvas_bindings.h"
#include "graphics/wasm_canvas_bindings.h"

static bool ends_with(const char *str, const char *suffix)
{
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len)
        return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

typedef struct ManagedApp
{
    const BudoRunOptions *options;
    const char *project_dir;
    const char *watch_root;
    char virtual_project_dir[PATH_MAX];
    char single_file_source[PATH_MAX];
    char main_script[PATH_MAX];
    ManagedRuntimeKind runtime_kind;
    const char *file_root;
    bool single_file_mode;
    bool from_input_mode;
} ManagedApp;

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

static const struct
{
    const char *language; 
    const char *main_name;
    ManagedRuntimeKind runtime;
} virtual_app_sources[] = {
    {"ts", "main.ts", MANAGED_RUNTIME_JAVASCRIPT},
    {"js", "main.js", MANAGED_RUNTIME_JAVASCRIPT},
    {"lua", "main.lua", MANAGED_RUNTIME_LUA},
    {"wat", "main.wat", MANAGED_RUNTIME_WEBASSEMBLY},
    {"wasm", "main.wasm", MANAGED_RUNTIME_WEBASSEMBLY},
};

static const char *single_file_main_name(const char *path, ManagedRuntimeKind *runtime)
{
    const char *dot = strrchr(path, '.');

    if (!dot)
        return NULL;
    for (size_t i = 0; i < sizeof(virtual_app_sources) / sizeof(virtual_app_sources[0]); i++)
    {
        if (strcmp(dot + 1, virtual_app_sources[i].language) == 0)
        {
            *runtime = virtual_app_sources[i].runtime;
            return virtual_app_sources[i].main_name;
        }
    }
    return NULL;
}

static const char *input_main_name(const char *language, ManagedRuntimeKind *runtime)
{
    for (size_t i = 0; i < sizeof(virtual_app_sources) / sizeof(virtual_app_sources[0]); i++)
    {
        if (strcmp(language, virtual_app_sources[i].language) == 0)
        {
            *runtime = virtual_app_sources[i].runtime;
            return virtual_app_sources[i].main_name;
        }
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

static bool refresh_single_file_app(ManagedApp *config)
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

static void cleanup_single_file_app(ManagedApp *config)
{
    if (!config || (!config->single_file_mode && !config->from_input_mode) ||
        !config->virtual_project_dir[0])
        return;

    if (config->main_script[0])
        unlink(config->main_script);
    rmdir(config->virtual_project_dir);
}

static bool configure_single_file_app(ManagedApp *config, const char *input_path)
{
    ManagedRuntimeKind runtime;
    const char *main_name = single_file_main_name(input_path, &runtime);

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

    if (!fs_make_temp_dir("budo-single", config->virtual_project_dir,
                          sizeof(config->virtual_project_dir)))
    {
        fprintf(stderr, "Error: Cannot create virtual app directory: %s\n", strerror(errno));
        return false;
    }

    snprintf(config->main_script, sizeof(config->main_script), "%s/%s",
             config->virtual_project_dir, main_name);
    config->project_dir = config->virtual_project_dir;
    config->watch_root = config->single_file_source;
    config->runtime_kind = runtime;
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

static bool configure_stdin_app(ManagedApp *config)
{
    ManagedRuntimeKind runtime;
    const char *main_name = input_main_name(config->options->from_input_language, &runtime);

    if (!main_name)
    {
        fprintf(stderr,
                "Error: unsupported --from-input kind '%s'. Use js, ts, lua, wat, or wasm.\n",
                config->options->from_input_language);
        return false;
    }

    if (!fs_make_temp_dir("budo-input", config->virtual_project_dir,
                          sizeof(config->virtual_project_dir)))
    {
        fprintf(stderr, "Error: Cannot create virtual app directory: %s\n", strerror(errno));
        return false;
    }

    snprintf(config->main_script, sizeof(config->main_script), "%s/%s",
             config->virtual_project_dir, main_name);
    config->project_dir = config->virtual_project_dir;
    config->watch_root = NULL;
    config->runtime_kind = runtime;

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

static bool resolve_main_script(ManagedApp *config)
{
    AppEntrypoint entrypoint;
    char error[256];
    if (!app_entrypoint_resolve(config->project_dir,
                                APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA |
                                    APP_ENTRYPOINT_WEBASSEMBLY,
                                &entrypoint, error, sizeof(error)))
    {
        if (strstr(error, "Ambiguous"))
            fprintf(stderr, "Error: %s in '%s'\n", error, config->project_dir);
        else
            fprintf(stderr,
                    "Error: Cannot find 'main.ts', 'main.js', 'main.lua', 'main.wat', "
                    "or 'main.wasm' in project directory '%s'\n",
                    config->project_dir);
        return false;
    }
    snprintf(config->main_script, PATH_MAX, "%s/%s",
             config->project_dir, entrypoint.filename);
    return managed_runtime_kind_from_entrypoint(entrypoint.runtime,
                                                &config->runtime_kind);
}

static bool managed_app_prepare(ManagedApp *config, const BudoRunOptions *options)
{
    memset(config, 0, sizeof(*config));
    config->options = options;
    config->file_root = options->file_root;
    config->runtime_kind = MANAGED_RUNTIME_JAVASCRIPT;

    if (options->from_input_language)
    {
        config->from_input_mode = true;
        return configure_stdin_app(config);
    }

    config->project_dir = options->target ? options->target : ".";
    if (file_exists(config->project_dir))
        return configure_single_file_app(config, config->project_dir);

    config->watch_root = config->project_dir;

    if (config->file_root == NULL)
        config->file_root = config->project_dir;

    if (!is_directory(config->project_dir))
    {
        fprintf(stderr, "Error: '%s' is not a valid directory or supported app file\n", config->project_dir);
        return false;
    }

    return resolve_main_script(config);
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

static void wasm_canvas_cleanup_adapter(void *opaque)
{
    wasm_canvas_destroy((WasmCanvasContext *)opaque);
}

static void wasm_canvas_poll_adapter(void *opaque)
{
    wasm_canvas_midi_poll((WasmCanvasContext *)opaque);
    wasm_canvas_network_poll((WasmCanvasContext *)opaque);
}

typedef struct
{
    ApplicationDriver driver;
    SubsystemRegistry subsystems;

    const ManagedApp *config;
    const AppMetadata *metadata;

    Window *window;    
    InputState *input; 

    BudoGraphicsActivateFn activate_graphics;
    void *activate_graphics_opaque;

    ManagedRuntimeCommon common_contexts;
} RuntimeInstance;

static void runtime_instance_init(RuntimeInstance *runtime)
{
    memset(runtime, 0, sizeof(*runtime));
    subsystem_registry_init(&runtime->subsystems);
}

#define RUNTIME_SUBSYSTEM(name_, role_, initialize_, poll_, cleanup_, optional_) \
    {                                                                            \
        name_, role_, initialize_, {.poll = poll_, .cleanup = cleanup_}, optional_}

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

static const SubsystemDescriptor runtime_wasm_subsystems[] = {
    RUNTIME_SUBSYSTEM("WebAssembly canvas", SUBSYSTEM_ROLE_CANVAS, runtime_init_wasm_canvas,
                      wasm_canvas_poll_adapter, wasm_canvas_cleanup_adapter, false),
};

#undef RUNTIME_SUBSYSTEM

static bool runtime_compose_subsystems(RuntimeInstance *runtime)
{
    const ManagedApp *config = runtime->config;
    const char *failed_subsystem = NULL;
    bool composed;

    if (config->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY)
    {
        composed = subsystem_compose(
            &runtime->subsystems, runtime, runtime_wasm_subsystems,
            sizeof(runtime_wasm_subsystems) / sizeof(runtime_wasm_subsystems[0]),
            &failed_subsystem);
    }
    else
    {
        const ManagedHostConfig host = {
            .project_dir = config->project_dir,
            .files_root = config->file_root,
            .metadata = runtime->metadata,
        };
        composed = managed_subsystems_compose(
            config->runtime_kind, &runtime->subsystems,
            &runtime->common_contexts, &host, &failed_subsystem);
    }
    if (composed)
        return true;

    fprintf(stderr, "Error: Failed to initialize %s subsystem\n",
            failed_subsystem ? failed_subsystem : "runtime");
    return false;
}

static double app_time_ms(void)
{
    static double origin = -1.0;
    double now;
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    now = (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
    if (origin < 0.0)
        origin = now;
    return now - origin;
}

static void app_sleep_ms(int milliseconds)
{
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec ts = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

static ManagedFrameContext runtime_frame_context(const RuntimeInstance *runtime)
{
    ManagedFrameContext frame = {
        .canvas = window_get_canvas(runtime->window),
        .window = runtime->window,
        .input = runtime->input,
        .display_density = window_get_dpi_scale(runtime->window),
    };
    window_get_size(runtime->window, &frame.width, &frame.height);
    return frame;
}

static bool runtime_load_entrypoint(RuntimeInstance *runtime)
{
    const ManagedApp *config = runtime->config;
    ManagedRuntimeCommon *contexts = &runtime->common_contexts;
    bool loaded = false;

    switch (config->runtime_kind)
    {
    case MANAGED_RUNTIME_JAVASCRIPT:
        loaded = js_runtime_load_file(contexts->js_ctx, config->main_script);
        break;
    case MANAGED_RUNTIME_LUA:
        loaded = lua_canvas_load_file(contexts->lua_ctx, config->main_script);
        break;
    case MANAGED_RUNTIME_WEBASSEMBLY:
        loaded = ends_with(config->main_script, ".wat")
                     ? wasm_canvas_load_wat_file(contexts->wasm_ctx, config->main_script)
                     : wasm_canvas_load_wasm_file(contexts->wasm_ctx, config->main_script);
        if (!loaded)
        {
            fprintf(stderr, "Error: Failed to load module '%s': %s\n",
                    config->main_script, wasm_canvas_get_error(contexts->wasm_ctx));
            return false;
        }
        printf("WebAssembly module loaded successfully\n");
        return true;
    }

    if (!loaded)
        fprintf(stderr, "Error: Failed to load script '%s'\n", config->main_script);
    return loaded;
}

static bool runtime_adapter_initialize(void *opaque)
{
    RuntimeInstance *runtime = opaque;

    if (!runtime_compose_subsystems(runtime))
        return false;

#ifdef BUDO_NEURAL
    if (runtime->config->runtime_kind == MANAGED_RUNTIME_WEBASSEMBLY &&
        runtime->metadata->neural_enabled &&
        !wasm_canvas_enable_neural(runtime->common_contexts.wasm_ctx))
        fprintf(stderr, "neural: failed to create context for WASM runtime\n");
#endif

    managed_runtime_set_graphics_activation(runtime->config->runtime_kind,
                                            &runtime->common_contexts,
                                            runtime->activate_graphics,
                                            runtime->activate_graphics_opaque);
    if (runtime->window)
    {
        ManagedFrameContext frame = runtime_frame_context(runtime);
        managed_runtime_set_frame_context(runtime->config->runtime_kind,
                                          &runtime->common_contexts, &frame);
    }
    return runtime_load_entrypoint(runtime);
}

static void runtime_adapter_frame(void *opaque, double timestamp_ms, double delta_seconds)
{
    RuntimeInstance *runtime = opaque;
    ManagedFrameContext frame = runtime_frame_context(runtime);
    (void)timestamp_ms;
    (void)delta_seconds;

    managed_runtime_frame(runtime->config->runtime_kind, &runtime->subsystems,
                          &runtime->common_contexts, &frame, app_time_ms());
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
    if (runtime->window)
        window_reset_app_resources(runtime->window);
}

static const ApplicationDriverOps runtime_driver_ops = {
    .initialize = runtime_adapter_initialize,
    .frame = runtime_adapter_frame,
    .pause = runtime_adapter_pause,
    .resume = runtime_adapter_resume,
    .context_lost = runtime_adapter_context_lost,
    .shutdown = runtime_adapter_shutdown,
};

static void runtime_attach_window(RuntimeInstance *runtime)
{
    int width, height;

    if (!runtime->window || runtime->driver.surface_ready || !runtime->driver.initialized)
        return;
    application_driver_surface_created(&runtime->driver);
    window_get_size(runtime->window, &width, &height);
    application_driver_resize(&runtime->driver, width, height,
                              window_get_dpi_scale(runtime->window));
}

static bool runtime_instance_load(RuntimeInstance *runtime,
                                  const ManagedApp *config,
                                  const AppMetadata *metadata,
                                  Window *window,
                                  InputState *input,
                                  BudoGraphicsActivateFn activate_graphics,
                                  void *activate_graphics_opaque)
{
    runtime_instance_init(runtime);
    runtime->config = config;
    runtime->metadata = metadata;
    runtime->window = window;
    runtime->input = input;
    runtime->activate_graphics = activate_graphics;
    runtime->activate_graphics_opaque = activate_graphics_opaque;

    application_driver_init(&runtime->driver, &runtime_driver_ops, runtime);
    if (!application_driver_initialize(&runtime->driver))
        return false;
    runtime_attach_window(runtime);
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
    ManagedApp *config;
    AppMetadata *metadata;
    FileWatcher *watcher;
    InputState *input;
    Window *window;      
    bool window_failed;
} ManagedDesktopApplication;

static void managed_desktop_activate_graphics(void *opaque)
{
    ManagedDesktopApplication *application = opaque;
    RuntimeInstance *runtime = application->runtime;
    const BudoRunOptions *options = application->config->options;

    if (!application->window && !application->window_failed)
    {
        WindowConfig window_config = {
            .title = options->title,
            .project_dir = application->config->project_dir,
            .width = options->width,
            .height = options->height,
            .resizable = true,
            .fullscreen = options->fullscreen,
            .vsync = options->vsync};
        application->window = window_create(&window_config);
        if (!application->window)
        {
            fprintf(stderr, "Error: Failed to create window\n");
            application->window_failed = true;
            return;
        }
    }
    if (!application->window)
        return;

    runtime->window = application->window;
    ManagedFrameContext frame = runtime_frame_context(runtime);
    managed_runtime_set_frame_context(application->config->runtime_kind,
                                      &runtime->common_contexts, &frame);
}

static bool managed_desktop_load(ManagedDesktopApplication *application)
{
    return runtime_instance_load(application->runtime, application->config,
                                 application->metadata, application->window,
                                 application->input, managed_desktop_activate_graphics,
                                 application);
}

static ApplicationDriver *managed_desktop_get_driver(void *opaque)
{
    ManagedDesktopApplication *application = opaque;
    ApplicationDriver *driver = &application->runtime->driver;
    
    return driver->initialized ? driver : NULL;
}

static bool managed_desktop_exit_requested(const ManagedDesktopApplication *application,
                                           int *code)
{
    const RuntimeInstance *runtime = application->runtime;
    return runtime->driver.initialized &&
           managed_runtime_exit_requested(application->config->runtime_kind,
                                          &runtime->common_contexts, code);
}

static bool managed_desktop_keep_running(void *opaque)
{
    return !managed_desktop_exit_requested(opaque, NULL);
}

static void managed_desktop_reload_if_changed(ManagedDesktopApplication *application)
{
    RuntimeInstance *runtime = application->runtime;
    ManagedApp *config = application->config;
    AppMetadata *metadata = application->metadata;
    bool metadata_loaded;

    if (!application->watcher ||
        !file_watcher_poll(application->watcher, app_time_ms() / 1000.0))
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
        fprintf(stderr, "Watch mode: reload failed; waiting for the next change.\n");
        return;
    }

    metadata_loaded = app_metadata_load(config->project_dir, metadata);
    if (!metadata->valid)
    {
        fprintf(stderr, "Watch mode: invalid app.json; waiting for the next change.\n");
        return;
    }
    if (config->single_file_mode && !metadata_loaded)
        metadata_name_from_single_file(config->single_file_source, metadata);

    printf("App: %s v%s by %s (%s)\n", metadata->name, metadata->version, metadata->author, metadata->date);
    printf("Runtime: %s\n", managed_runtime_kind_name(config->runtime_kind));
    printf("Main script: %s\n", config->main_script);

    if (!managed_desktop_load(application))
    {
        runtime_instance_destroy(runtime);
        fprintf(stderr, "Watch mode: reload failed; waiting for the next change.\n");
        return;
    }
}

static void managed_desktop_before_frame(void *opaque, Window *window,
                                         InputState *input, double now)
{
    (void)window;
    (void)input;
    (void)now;
    managed_desktop_reload_if_changed(opaque);
}

static int managed_desktop_loop(ManagedDesktopApplication *application)
{
    RuntimeInstance *runtime = application->runtime;
    ManagedRuntimeKind kind = application->config->runtime_kind;
    int exit_code = 0;

    for (;;)
    {
        if (managed_desktop_exit_requested(application, &exit_code))
            return exit_code;
        if (application->window_failed)
            return 1;

        if (!application->window && runtime->driver.initialized &&
            managed_runtime_graphics_requested(kind, &runtime->common_contexts))
            managed_desktop_activate_graphics(application);

        if (application->window)
        {
            DesktopHostApplication desktop_application = {
                .context = application,
                .get_driver = managed_desktop_get_driver,
                .before_frame = managed_desktop_before_frame,
                .keep_running = managed_desktop_keep_running,
            };
            runtime_attach_window(runtime);
            desktop_host_run(application->window, application->input, &desktop_application);
            managed_desktop_exit_requested(application, &exit_code);
            return exit_code;
        }

        managed_desktop_reload_if_changed(application);
        if (runtime->driver.initialized)
        {
            managed_runtime_tick(kind, &runtime->subsystems, &runtime->common_contexts,
                                 app_time_ms());
            if (!application->watcher && !application->window &&
                !managed_runtime_exit_requested(kind, &runtime->common_contexts, NULL) &&
                !managed_runtime_has_pending_work(kind, &runtime->subsystems,
                                                  &runtime->common_contexts))
                return 0;
        }
        app_sleep_ms(1);
    }
}

int managed_desktop_run(const BudoRunOptions *options)
{
    ManagedApp config;

    printf("Budo start\n");
    if (!managed_app_prepare(&config, options))
    {
        cleanup_single_file_app(&config);
        return 1;
    }
    printf("Runtime: %s\n", managed_runtime_kind_name(config.runtime_kind));
    printf("Loading project: %s\n", config.project_dir);
    if (config.single_file_mode)
        printf("Single-file app: %s\n", config.single_file_source);
    if (config.from_input_mode)
        printf("Input app: stdin (%s)\n", options->from_input_language);
    printf("Main script: %s\n", config.main_script);
    if (options->watch)
        printf("Watch mode: enabled\n");

    AppMetadata metadata;
    bool metadata_loaded = app_metadata_load(config.project_dir, &metadata);
    if (!metadata.valid)
    {
        cleanup_single_file_app(&config);
        return 1;
    }
    if (config.single_file_mode && !metadata_loaded)
        metadata_name_from_single_file(config.single_file_source, &metadata);
    if (config.from_input_mode && !metadata_loaded)
        snprintf(metadata.name, sizeof(metadata.name), "stdin");

    printf("App: %s v%s by %s (%s)\n", metadata.name, metadata.version, metadata.author, metadata.date);

    InputState input;
    input_init(&input);
    app_time_ms(); 

    RuntimeInstance runtime;
    runtime_instance_init(&runtime);
    ManagedDesktopApplication application = {
        .runtime = &runtime,
        .config = &config,
        .metadata = &metadata,
        .input = &input,
    };

    int exit_code = 1;
    if (managed_desktop_load(&application))
    {
        if (options->watch)
        {
            application.watcher = file_watcher_create(config.watch_root, app_time_ms() / 1000.0);
            if (!application.watcher)
                fprintf(stderr, "Warning: failed to start watch mode for '%s'\n",
                        config.watch_root);
            else if (file_watcher_get_error(application.watcher))
                fprintf(stderr, "Warning: %s\n", file_watcher_get_error(application.watcher));
        }
        exit_code = managed_desktop_loop(&application);
        printf("Shutting down Budo\n");
    }

    file_watcher_destroy(application.watcher);
    runtime_instance_destroy(&runtime);
    if (application.window)
        window_destroy(application.window);
    cleanup_single_file_app(&config);
    return exit_code;
}