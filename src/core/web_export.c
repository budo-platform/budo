#include "core/web_export.h"
#include "core/app_metadata.h"
#include "core/app_entrypoint.h"
#include "core/embedded_resource.h"
#include "core/json_util.h"
#include "core/ts_strip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>

#include "embedded_web_js.h"
#include "embedded_web_wasm.h"
#include "embedded_web_template.h"

static int mkdirs(const char *path)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

static int write_binary_file(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "Error: cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len)
    {
        fprintf(stderr, "Error: short write to %s\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int write_text_file(const char *path, const char *text)
{
    return write_binary_file(path, (const unsigned char *)text, strlen(text));
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
    {
        fprintf(stderr, "Error: cannot read %s: %s\n", src, strerror(errno));
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out)
    {
        fprintf(stderr, "Error: cannot write %s: %s\n", dst, strerror(errno));
        fclose(in);
        return -1;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            fprintf(stderr, "Error: short write to %s\n", dst);
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}

typedef struct
{
    char **paths; 
    size_t count;
    size_t capacity;
} FileList;

static void file_list_init(FileList *fl)
{
    fl->paths = NULL;
    fl->count = 0;
    fl->capacity = 0;
}

static void file_list_free(FileList *fl)
{
    for (size_t i = 0; i < fl->count; i++)
        free(fl->paths[i]);
    free(fl->paths);
    fl->paths = NULL;
    fl->count = 0;
    fl->capacity = 0;
}

static int file_list_add(FileList *fl, const char *rel_path)
{
    if (fl->count >= fl->capacity)
    {
        size_t new_cap = fl->capacity ? fl->capacity * 2 : 64;
        char **tmp = realloc(fl->paths, new_cap * sizeof(char *));
        if (!tmp)
            return -1;
        fl->paths = tmp;
        fl->capacity = new_cap;
    }
    fl->paths[fl->count] = strdup(rel_path);
    if (!fl->paths[fl->count])
        return -1;
    fl->count++;
    return 0;
}

static int collect_files(const char *base_dir, const char *rel_prefix, FileList *fl)
{
    char full_path[PATH_MAX];
    if (rel_prefix[0])
        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, rel_prefix);
    else
        snprintf(full_path, sizeof(full_path), "%s", base_dir);

    DIR *d = opendir(full_path);
    if (!d)
    {
        fprintf(stderr, "Error: cannot open directory %s: %s\n", full_path, strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL)
    {
        
        if (ent->d_name[0] == '.')
            continue;

        char rel[PATH_MAX];
        if (rel_prefix[0])
            snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, ent->d_name);
        else
            snprintf(rel, sizeof(rel), "%s", ent->d_name);

        char abs[PATH_MAX];
        snprintf(abs, sizeof(abs), "%s/%s", base_dir, rel);

        struct stat st;
        if (stat(abs, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            if (collect_files(base_dir, rel, fl) != 0)
            {
                closedir(d);
                return -1;
            }
        }
        else if (S_ISREG(st.st_mode))
        {
            if (file_list_add(fl, rel) != 0)
            {
                closedir(d);
                return -1;
            }
        }
    }

    closedir(d);
    return 0;
}

typedef struct
{
    const char *rel_path; 
    size_t start;
    size_t end;
} PackEntry;

static int generate_data_package(const char *project_dir, const FileList *fl,
                                 const char *output_dir,
                                 int synthesize_main_js_from_ts)
{
    
    size_t max_entries = fl->count + (synthesize_main_js_from_ts ? 1 : 0);
    PackEntry *entries = calloc(max_entries, sizeof(PackEntry));
    if (!entries)
        return -1;

    char data_path[PATH_MAX];
    snprintf(data_path, sizeof(data_path), "%s/budo.data", output_dir);

    FILE *data_fp = fopen(data_path, "wb");
    if (!data_fp)
    {
        fprintf(stderr, "Error: cannot create %s: %s\n", data_path, strerror(errno));
        free(entries);
        return -1;
    }

    size_t offset = 0;
    size_t entry_count = 0;
    for (size_t i = 0; i < fl->count; i++)
    {
        char abs_path[PATH_MAX];
        snprintf(abs_path, sizeof(abs_path), "%s/%s", project_dir, fl->paths[i]);

        FILE *in = fopen(abs_path, "rb");
        if (!in)
        {
            fprintf(stderr, "Error: cannot read %s: %s\n", abs_path, strerror(errno));
            fclose(data_fp);
            free(entries);
            return -1;
        }

        entries[entry_count].rel_path = fl->paths[i];
        entries[entry_count].start = offset;

        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        {
            if (fwrite(buf, 1, n, data_fp) != n)
            {
                fprintf(stderr, "Error: write failed for %s\n", data_path);
                fclose(in);
                fclose(data_fp);
                free(entries);
                return -1;
            }
            offset += n;
        }

        entries[entry_count].end = offset;
        entry_count++;
        fclose(in);
    }

    if (synthesize_main_js_from_ts)
    {
        char ts_path[PATH_MAX];
        snprintf(ts_path, sizeof(ts_path), "%s/main.ts", project_dir);

        FILE *in = fopen(ts_path, "rb");
        if (!in)
        {
            fprintf(stderr, "Error: cannot read %s: %s\n", ts_path, strerror(errno));
            fclose(data_fp);
            free(entries);
            return -1;
        }

        fseek(in, 0, SEEK_END);
        long ts_size = ftell(in);
        fseek(in, 0, SEEK_SET);

        char *ts_code = malloc((size_t)ts_size + 1);
        if (!ts_code)
        {
            fprintf(stderr, "Error: out of memory reading %s\n", ts_path);
            fclose(in);
            fclose(data_fp);
            free(entries);
            return -1;
        }

        if (ts_size > 0 && fread(ts_code, 1, (size_t)ts_size, in) != (size_t)ts_size)
        {
            fprintf(stderr, "Error: short read from %s\n", ts_path);
            free(ts_code);
            fclose(in);
            fclose(data_fp);
            free(entries);
            return -1;
        }
        ts_code[ts_size] = '\0';
        fclose(in);

        char *js_code = NULL;
        size_t js_len = 0;
        if (!ts_strip_types(ts_code, (size_t)ts_size, &js_code, &js_len))
        {
            fprintf(stderr, "Error: TypeScript transpilation failed for %s\n", ts_path);
            free(ts_code);
            fclose(data_fp);
            free(entries);
            return -1;
        }
        free(ts_code);

        entries[entry_count].rel_path = "main.js";
        entries[entry_count].start = offset;
        if (js_len > 0 && fwrite(js_code, 1, js_len, data_fp) != js_len)
        {
            fprintf(stderr, "Error: write failed for generated main.js\n");
            free(js_code);
            fclose(data_fp);
            free(entries);
            return -1;
        }
        offset += js_len;
        entries[entry_count].end = offset;
        entry_count++;
        free(js_code);
    }

    fclose(data_fp);

    char pkg_path[PATH_MAX];
    snprintf(pkg_path, sizeof(pkg_path), "%s/budo-package.js", output_dir);

    FILE *pkg_fp = fopen(pkg_path, "w");
    if (!pkg_fp)
    {
        fprintf(stderr, "Error: cannot create %s: %s\n", pkg_path, strerror(errno));
        free(entries);
        return -1;
    }

    fprintf(pkg_fp,
            "// Auto-generated by budo --export-web.  Do not edit.\n"
            "var Module = typeof Module !== 'undefined' ? Module : {};\n"
            "(function() {\n"
            "  var metadata = {\n"
            "    files: [\n");

    for (size_t i = 0; i < entry_count; i++)
    {
        fprintf(pkg_fp, "      {filename: \"/%s\", start: %zu, end: %zu}%s\n",
                entries[i].rel_path,
                entries[i].start,
                entries[i].end,
                (i + 1 < entry_count) ? "," : "");
    }

    fprintf(pkg_fp,
            "    ],\n"
            "    remote_package_size: %zu\n"
            "  };\n"
            "\n"
            "  function runWithFS() {\n"
            "    var byteArray;\n"
            "    for (var i = 0; i < metadata.files.length; i++) {\n"
            "      Module['addRunDependency']('fp ' + metadata.files[i].filename);\n"
            "    }\n"
            "    Module['addRunDependency']('datafile_budo.data');\n"
            "\n"
            "    fetch('budo.data').then(function(r) {\n"
            "      return r.arrayBuffer();\n"
            "    }).then(function(buf) {\n"
            "      byteArray = new Uint8Array(buf);\n"
            "      for (var i = 0; i < metadata.files.length; i++) {\n"
            "        var file = metadata.files[i];\n"
            "        var dir = file.filename.substring(0, file.filename.lastIndexOf('/'));\n"
            "        if (dir && dir !== '/') {\n"
            "          try { Module['FS_createPath']('/', dir.substring(1), true, true); } catch(e) {}\n"
            "        }\n"
            "        var data = byteArray.subarray(file.start, file.end);\n"
            "        Module['FS_createDataFile'](file.filename, null, data, true, true, true);\n"
            "        Module['removeRunDependency']('fp ' + file.filename);\n"
            "      }\n"
            "      Module['removeRunDependency']('datafile_budo.data');\n"
            "    });\n"
            "  }\n"
            "\n"
            "  if (Module['calledRun']) {\n"
            "    runWithFS();\n"
            "  } else {\n"
            "    if (!Module['preRun']) Module['preRun'] = [];\n"
            "    Module['preRun'].push(runWithFS);\n"
            "  }\n"
            "})();\n",
            offset);

    fclose(pkg_fp);
    free(entries);
    return 0;
}

static char *render_template(const char *tmpl, const char *app_name,
                             const char *description, const char *icon_href)
{
    
    char favicon_tag[512] = "";
    char meta_desc_tag[1024] = "";
    char og_title_tag[512] = "";
    char og_desc_tag[1024] = "";
    char og_image_tag[512] = "";

    if (icon_href && icon_href[0])
        snprintf(favicon_tag, sizeof(favicon_tag),
                 "<link rel=\"icon\" href=\"%s\">", icon_href);

    if (description && description[0])
    {
        snprintf(meta_desc_tag, sizeof(meta_desc_tag),
                 "<meta name=\"description\" content=\"%s\">", description);
        snprintf(og_desc_tag, sizeof(og_desc_tag),
                 "<meta property=\"og:description\" content=\"%s\">", description);
    }

    snprintf(og_title_tag, sizeof(og_title_tag),
             "<meta property=\"og:title\" content=\"%s\">", app_name);

    if (icon_href && icon_href[0])
        snprintf(og_image_tag, sizeof(og_image_tag),
                 "<meta property=\"og:image\" content=\"%s\">", icon_href);

    size_t tmpl_len = strlen(tmpl);
    size_t out_cap = tmpl_len + 8192;
    char *out = malloc(out_cap);
    if (!out)
        return NULL;

    size_t out_pos = 0;
    const char *p = tmpl;

    while (*p)
    {
        if (p[0] == '{' && p[1] == '{')
        {
            const char *end = strstr(p + 2, "}}");
            if (end)
            {
                
                size_t key_len = (size_t)(end - p - 2);
                char key[128];
                if (key_len < sizeof(key))
                {
                    memcpy(key, p + 2, key_len);
                    key[key_len] = '\0';

                    const char *replacement = "";
                    if (strcmp(key, "APP_NAME") == 0)
                        replacement = app_name;
                    else if (strcmp(key, "FAVICON_TAG") == 0)
                        replacement = favicon_tag;
                    else if (strcmp(key, "META_DESC_TAG") == 0)
                        replacement = meta_desc_tag;
                    else if (strcmp(key, "OG_TITLE_TAG") == 0)
                        replacement = og_title_tag;
                    else if (strcmp(key, "OG_DESC_TAG") == 0)
                        replacement = og_desc_tag;
                    else if (strcmp(key, "OG_IMAGE_TAG") == 0)
                        replacement = og_image_tag;

                    size_t repl_len = strlen(replacement);
                    
                    while (out_pos + repl_len + 1 >= out_cap)
                    {
                        out_cap *= 2;
                        char *tmp = realloc(out, out_cap);
                        if (!tmp)
                        {
                            free(out);
                            return NULL;
                        }
                        out = tmp;
                    }
                    memcpy(out + out_pos, replacement, repl_len);
                    out_pos += repl_len;
                    p = end + 2;
                    continue;
                }
            }
        }

        if (out_pos + 1 >= out_cap)
        {
            out_cap *= 2;
            char *tmp = realloc(out, out_cap);
            if (!tmp)
            {
                free(out);
                return NULL;
            }
            out = tmp;
        }
        out[out_pos++] = *p++;
    }

    out[out_pos] = '\0';
    return out;
}

int web_export(const char *project_dir, const char *output_dir)
{
    
    char default_output[PATH_MAX];
    if (!output_dir || !output_dir[0])
    {
        snprintf(default_output, sizeof(default_output), "dist/web");
        output_dir = default_output;
    }

    if (embedded_web_js_len == 0 || embedded_web_wasm_len == 0 || embedded_web_template_len == 0)
    {
        fprintf(stderr,
                "Error: Web runtime not embedded in this build.\n"
                "\n"
                "The --export-web feature requires pre-built Emscripten runtime files\n"
                "to be embedded in the budo binary.  To set this up:\n"
                "\n"
                "  1.  make web-build-runtime   # requires Emscripten (one-time)\n"
                "  2.  make rebuild             # rebuilds the native binary with\n"
                "                               # the web runtime embedded\n"
                "\n"
                "After this, --export-web will work without Emscripten.\n");
        return 1;
    }

    struct stat st;
    if (stat(project_dir, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "Error: '%s' is not a valid directory\n", project_dir);
        return 1;
    }

    char check_path[PATH_MAX];
    int has_main_js = 0, has_main_ts = 0, has_lua = 0;

    snprintf(check_path, sizeof(check_path), "%s/main.js", project_dir);
    if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode))
        has_main_js = 1;

    snprintf(check_path, sizeof(check_path), "%s/main.ts", project_dir);
    if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode))
        has_main_ts = 1;

    snprintf(check_path, sizeof(check_path), "%s/main.lua", project_dir);
    if (stat(check_path, &st) == 0 && S_ISREG(st.st_mode))
        has_lua = 1;

    if (!has_main_js && !has_main_ts && !has_lua)
    {
        fprintf(stderr, "Error: No main.js, main.ts, or main.lua in '%s'\n", project_dir);
        return 1;
    }
    AppEntrypoint entrypoint;
    char entrypoint_error[256];
    if (!app_entrypoint_resolve(project_dir,
                                APP_ENTRYPOINT_JAVASCRIPT | APP_ENTRYPOINT_LUA,
                                &entrypoint, entrypoint_error,
                                sizeof(entrypoint_error)))
    {
        fprintf(stderr, "Error: %s in '%s'\n", entrypoint_error, project_dir);
        return 1;
    }
    (void)entrypoint;

    AppMetadata metadata;
    app_metadata_load(project_dir, &metadata);
    if (!metadata.valid)
        return 1;

    char description[512] = "";
    {
        char app_json_path[PATH_MAX];
        snprintf(app_json_path, sizeof(app_json_path), "%s/app.json", project_dir);
        char *json = json_util_read_file(app_json_path, 65536);
        if (json)
        {
            json_get_string(json, "short_description", description, sizeof(description), "");
            free(json);
        }
    }

    printf("Budo Web Export\n");
    printf("  Project: %s\n", project_dir);
    printf("  Name:    %s\n", metadata.name);
    if (metadata.author[0])
        printf("  Author:  %s\n", metadata.author);
    if (metadata.version[0])
        printf("  Version: %s\n", metadata.version);
    printf("  Output:  %s\n", output_dir);
    printf("\n");

    mkdirs(output_dir);

    printf("  Collecting project files...\n");
    FileList fl;
    file_list_init(&fl);
    if (collect_files(project_dir, "", &fl) != 0)
    {
        fprintf(stderr, "Error: failed to collect project files\n");
        file_list_free(&fl);
        return 1;
    }
    printf("  Found %zu files\n", fl.count);

    printf("  Generating data package...\n");
    if (generate_data_package(project_dir, &fl, output_dir, has_main_ts && !has_main_js) != 0)
    {
        fprintf(stderr, "Error: failed to generate data package\n");
        file_list_free(&fl);
        return 1;
    }
    file_list_free(&fl);

    unsigned char *web_js = NULL;
    unsigned char *web_wasm = NULL;
    unsigned char *web_template = NULL;
    size_t web_js_len = 0;
    size_t web_wasm_len = 0;
    size_t web_template_len = 0;

    if (embedded_resource_unpack(embedded_web_js_data, embedded_web_js_len,
                                 embedded_web_js_uncompressed_len, embedded_web_js_is_gzip,
                                 &web_js, &web_js_len) != 0 ||
        embedded_resource_unpack(embedded_web_wasm_data, embedded_web_wasm_len,
                                 embedded_web_wasm_uncompressed_len, embedded_web_wasm_is_gzip,
                                 &web_wasm, &web_wasm_len) != 0 ||
        embedded_resource_unpack(embedded_web_template_data, embedded_web_template_len,
                                 embedded_web_template_uncompressed_len, embedded_web_template_is_gzip,
                                 &web_template, &web_template_len) != 0)
    {
        fprintf(stderr, "Error: failed to decompress embedded web resources\n");
        free(web_js);
        free(web_wasm);
        free(web_template);
        return 1;
    }

    printf("  Writing budo.js (%zu bytes)...\n", web_js_len);
    {
        char js_path[PATH_MAX];
        snprintf(js_path, sizeof(js_path), "%s/budo.js", output_dir);
        if (write_binary_file(js_path, web_js, web_js_len) != 0)
        {
            free(web_js);
            free(web_wasm);
            free(web_template);
            return 1;
        }
    }

    printf("  Writing budo.wasm (%zu bytes)...\n", web_wasm_len);
    {
        char wasm_path[PATH_MAX];
        snprintf(wasm_path, sizeof(wasm_path), "%s/budo.wasm", output_dir);
        if (write_binary_file(wasm_path, web_wasm, web_wasm_len) != 0)
        {
            free(web_js);
            free(web_wasm);
            free(web_template);
            return 1;
        }
    }

    char icon_href[256] = "";
    {
        char app_json_path[PATH_MAX];
        snprintf(app_json_path, sizeof(app_json_path), "%s/app.json", project_dir);
        char *json = json_util_read_file(app_json_path, 65536);
        if (json)
        {
            char icon_name[256];
            if (json_get_string(json, "icon", icon_name, sizeof(icon_name), "") && icon_name[0])
            {
                
                char icon_src[PATH_MAX];
                snprintf(icon_src, sizeof(icon_src), "%s/%s", project_dir, icon_name);
                struct stat icon_st;
                if (stat(icon_src, &icon_st) == 0 && S_ISREG(icon_st.st_mode))
                {
                    
                    const char *dot = strrchr(icon_name, '.');
                    const char *ext = dot ? dot : "";

                    snprintf(icon_href, sizeof(icon_href), "favicon%s", ext);

                    char icon_dst[PATH_MAX];
                    snprintf(icon_dst, sizeof(icon_dst), "%s/%s", output_dir, icon_href);
                    copy_file(icon_src, icon_dst);
                    printf("  Copied icon: %s → %s\n", icon_name, icon_href);
                }
            }
            free(json);
        }
    }

    printf("  Generating index.html...\n");
    {

        const char *tmpl_str = (const char *)web_template;

        char *html = render_template(tmpl_str, metadata.name, description, icon_href);
        if (!html)
        {
            fprintf(stderr, "Error: failed to render HTML template\n");
            return 1;
        }

        const char *anchor = "<script async src=\"budo.js\"></script>";
        char *pos = strstr(html, anchor);
        if (pos)
        {
            const char *pkg_script = "<script src=\"budo-package.js\"></script>\n    ";
            size_t pkg_len = strlen(pkg_script);
            size_t html_len = strlen(html);
            size_t insert_at = (size_t)(pos - html);

            char *new_html = malloc(html_len + pkg_len + 1);
            if (new_html)
            {
                memcpy(new_html, html, insert_at);
                memcpy(new_html + insert_at, pkg_script, pkg_len);
                memcpy(new_html + insert_at + pkg_len, html + insert_at, html_len - insert_at + 1);
                free(html);
                html = new_html;
            }
        }

        char html_path[PATH_MAX];
        snprintf(html_path, sizeof(html_path), "%s/index.html", output_dir);
        if (write_text_file(html_path, html) != 0)
        {
            free(html);
            free(web_js);
            free(web_wasm);
            free(web_template);
            return 1;
        }
        free(html);
    }

    free(web_js);
    free(web_wasm);
    free(web_template);

    printf("\n");
    printf("Web export complete!\n");
    printf("Output: %s\n", output_dir);
    printf("\n");
    printf("To serve locally:\n");
    printf("  budo web-serve <project-dir>\n");
    printf("  or serve %s with any static HTTP server\n", output_dir);
    printf("\n");

    return 0;
}