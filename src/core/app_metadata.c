#include "app_metadata.h"
#include "json_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

static void extract_basename(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0)
        return;

    char *tmp = strdup(path);
    if (!tmp)
    {
        snprintf(out, out_size, "Budo App");
        return;
    }

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    const char *base = basename(tmp);
    if (base && base[0] != '\0' && strcmp(base, ".") != 0 && strcmp(base, "/") != 0)
        snprintf(out, out_size, "%s", base);
    else
        snprintf(out, out_size, "Budo App");
    free(tmp);
}

bool app_metadata_parse_version(const char *json, char *out, size_t out_size,
                                char *error, size_t error_size)
{
    char version[256] = "";
    char version_name[256] = "";
    bool has_version = json && json_get_string(json, "version", version, sizeof(version), NULL);
    bool has_version_name = json && json_get_string(json, "version_name", version_name,
                                                    sizeof(version_name), NULL);
    if (error && error_size)
        error[0] = '\0';
    if (has_version && has_version_name && strcmp(version, version_name) != 0)
    {
        if (error && error_size)
            snprintf(error, error_size,
                     "Conflicting app.json version and version_name values");
        return false;
    }
    if (out && out_size && ((has_version && version[0]) || (has_version_name && version_name[0])))
        snprintf(out, out_size, "%s", has_version ? version : version_name);
    return true;
}

bool app_metadata_load(const char *project_dir, AppMetadata *out)
{
    if (!out)
        return false;

    memset(out, 0, sizeof(*out));
    extract_basename(project_dir, out->name, sizeof(out->name));
    snprintf(out->author, sizeof(out->author), "Unknown");
    snprintf(out->version, sizeof(out->version), "1.0");
    out->valid = true;

    if (!project_dir)
        return false;

    char path[4096];
    snprintf(path, sizeof(path), "%s/app.json", project_dir);

    char *json = json_util_read_file(path, 1024 * 1024);
    if (!json)
        return false;

    char buf[256];
    if (json_get_string(json, "name", buf, sizeof(buf), "") && buf[0])
        snprintf(out->name, sizeof(out->name), "%s", buf);

    if (json_get_string(json, "author", buf, sizeof(buf), "") && buf[0])
        snprintf(out->author, sizeof(out->author), "%s", buf);

    if (json_get_string(json, "date", buf, sizeof(buf), "") && buf[0])
        snprintf(out->date, sizeof(out->date), "%s", buf);

    char version_error[128];
    if (!app_metadata_parse_version(json, out->version, sizeof(out->version),
                                    version_error, sizeof(version_error)))
    {
        fprintf(stderr, "Error: %s\n", version_error);
        out->valid = false;
    }

    if (json_get_string(json, "orientation", buf, sizeof(buf), "") && buf[0])
        snprintf(out->orientation, sizeof(out->orientation), "%s", buf);

    out->neural_enabled = json_get_bool(json, "neural", false);
    out->filesystem_enabled = json_get_bool(json, "filesystem", false);

    free(json);
    return out->valid;
}