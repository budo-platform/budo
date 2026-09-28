#include "native/native_project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            fprintf(stderr, "native_project_test: check failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition);                            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    char *contents;
    long length;
    if (!file || fseek(file, 0, SEEK_END) != 0)
        return NULL;
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return NULL;
    }
    contents = malloc((size_t)length + 1);
    if (!contents || fread(contents, 1, (size_t)length, file) != (size_t)length)
    {
        free(contents);
        fclose(file);
        return NULL;
    }
    contents[length] = '\0';
    fclose(file);
    return contents;
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

int main(int argc, char **argv)
{
    NativeProject project;
    NativeProject single_file;
    NativeProject invalid;
    NativeProjectError error;
    char path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char invalid_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char android_path[BUDO_NATIVE_PROJECT_MAX_PATH];
    char *generated;

    CHECK(argc == 5);
    CHECK(native_project_load(argv[1], &project, &error));
    CHECK(project.schema_version == 1);
    CHECK(project.c_standard == 11);
    CHECK(project.sources.count == 2);
    CHECK(strcmp(project.sources.items[1], "src/counter.c") == 0);
    CHECK(project.include_directories.count == 1);
    CHECK(project.assets.count == 1);
    CHECK(project.definitions.count == 1);
    CHECK(project.modules.count == 1);
    CHECK(strcmp(project.output_name, "Native_Project_Fixture") == 0);

    snprintf(path, sizeof(path), "%s/main.c", argv[1]);
    CHECK(native_project_load(path, &single_file, &error));
    CHECK(single_file.single_file);
    CHECK(single_file.sources.count == 1);
    CHECK(strcmp(single_file.name, "main") == 0);

    snprintf(invalid_path, sizeof(invalid_path), "%s", argv[2]);
    CHECK(!native_project_load(invalid_path, &invalid, &error));
    CHECK(strstr(error.message, "Invalid project-relative source path") != NULL);

    CHECK(native_project_generate(&project, argv[3], argv[4], &error));
    snprintf(path, sizeof(path), "%s/CMakeLists.txt", argv[3]);
    generated = read_file(path);
    CHECK(generated != NULL);
    CHECK(strstr(generated, "budo::native_launcher") != NULL);
    CHECK(strstr(generated, "find_package(BudoNative CONFIG REQUIRED") != NULL);
    CHECK(strstr(generated, "find_package(Threads REQUIRED)") != NULL);
    CHECK(strstr(generated, "find_package(OpenGL REQUIRED)") != NULL);
    CHECK(strstr(generated, "find_package(Freetype REQUIRED)") != NULL);
    CHECK(strstr(generated, "find_package(Fontconfig REQUIRED)") != NULL);
    CHECK(strstr(generated, "budo_native_configure_application") != NULL);
    CHECK(strstr(generated, "CMAKE_EXPORT_COMPILE_COMMANDS ON") != NULL);
    CHECK(strstr(generated, "src/counter.c") != NULL);
    CHECK(strstr(generated, "NATIVE_FIXTURE=1") != NULL);
    free(generated);

    snprintf(path, sizeof(path), "%s/budo-native-project.json", argv[3]);
    generated = read_file(path);
    CHECK(generated != NULL);
    CHECK(strstr(generated, "\"sources\": [\"main.c\", \"src/counter.c\"]") != NULL);
    CHECK(strstr(generated, "\"c_standard\": 11") != NULL);
    CHECK(strstr(generated, "\"definitions\": [\"NATIVE_FIXTURE=1\"]") != NULL);
    CHECK(strstr(generated, "\"modules\": [\"core\"]") != NULL);
    free(generated);

    snprintf(android_path, sizeof(android_path), "%s/android-app", argv[3]);
    CHECK(native_project_generate_android(&project, android_path, &error));
    snprintf(path, sizeof(path), "%s/android-app/main.c", argv[3]);
    CHECK(file_exists(path));
    snprintf(path, sizeof(path), "%s/android-app/src/counter.c", argv[3]);
    CHECK(file_exists(path));
    snprintf(path, sizeof(path), "%s/android-app/include/counter.h", argv[3]);
    CHECK(file_exists(path));
    snprintf(path, sizeof(path), "%s/android-app/assets/message.txt", argv[3]);
    CHECK(!file_exists(path));
    snprintf(path, sizeof(path), "%s/android-app/budo-native.json", argv[3]);
    CHECK(!file_exists(path));
    snprintf(path, sizeof(path), "%s/android-app/budo-native-sources.cmake", argv[3]);
    generated = read_file(path);
    CHECK(generated != NULL);
    CHECK(strstr(generated, "set(BUDO_HAS_NATIVE_APP ON)") != NULL);
    CHECK(strstr(generated, "${CMAKE_CURRENT_LIST_DIR}/src/counter.c") != NULL);
    CHECK(strstr(generated, "${CMAKE_CURRENT_LIST_DIR}/include") != NULL);
    CHECK(strstr(generated, "NATIVE_FIXTURE=1") != NULL);
    CHECK(strstr(generated, "BUDO_NATIVE_APP_C_STANDARD 11") != NULL);
    free(generated);

    puts("native_project_test: ok");
    return 0;
}