#include "core/ts_strip.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <file.ts>\n", argv[0]);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file)
    {
        perror(argv[1]);
        return 1;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *source = malloc(size > 0 ? (size_t)size : 1);
    if (!source || fread(source, 1, (size_t)size, file) != (size_t)size)
    {
        fclose(file);
        return 1;
    }
    fclose(file);
    char *output = NULL;
    size_t length = 0;
    if (!ts_strip_types(source, (size_t)size, &output, &length))
        return 1;
    fwrite(output, 1, length, stdout);
    free(output);
    free(source);
    return 0;
}