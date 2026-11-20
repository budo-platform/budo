#define _POSIX_C_SOURCE 200809L
#include "graphics/image_loader.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double median(double samples[7])
{
    for (int sample = 1; sample < 7; sample++)
        for (int previous = sample; previous > 0 && samples[previous] < samples[previous - 1]; previous--)
        {
            double temporary = samples[previous];
            samples[previous] = samples[previous - 1];
            samples[previous - 1] = temporary;
        }
    return samples[3];
}

static void benchmark_time(struct timespec *value)
{
#ifdef _WIN32
    timespec_get(value, TIME_UTC);
#else
    clock_gettime(CLOCK_MONOTONIC, value);
#endif
}

static bool benchmark_cancel(void *opaque)
{
    int *checks = opaque;
    return ++*checks >= 3;
}

static bool benchmark_continue(void *opaque)
{
    (void)opaque;
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4)
    {
        fprintf(stderr, "Usage: %s image [maximum-output-dimension [scaled|full|cancellable|cancel]]\n", argv[0]);
        return 1;
    }
    char *end_argument = NULL;
    long maximum = argc >= 3 ? strtol(argv[2], &end_argument, 10) : 0;
    if (argc >= 3 && (!argv[2][0] || *end_argument || maximum < 1 || maximum > 16384))
        return 1;
    if (argc == 4 && strcmp(argv[3], "scaled") && strcmp(argv[3], "full") &&
        strcmp(argv[3], "cancellable") && strcmp(argv[3], "cancel"))
        return 1;
    bool scaled = argc != 4 || strcmp(argv[3], "full") != 0;
    bool cancel = argc == 4 && strcmp(argv[3], "cancel") == 0;
    bool cancellable = cancel || (argc == 4 && strcmp(argv[3], "cancellable") == 0);
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 1;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 1;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 1;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
    {
        free(bytes);
        fclose(file);
        return 1;
    }
    fclose(file);
    int source_width = 0, source_height = 0;
    if (!image_probe(bytes, (size_t)length, &source_width, &source_height))
    {
        free(bytes);
        return 1;
    }
    double samples[7];
    double resize_samples[7] = {0};
    uint64_t checksum = 0;
    int width = 0, height = 0;
    int output_width = source_width, output_height = source_height;
    if (maximum && (source_width > maximum || source_height > maximum))
    {
        int longest = source_width > source_height ? source_width : source_height;
        output_width = (int)((int64_t)source_width * maximum / longest);
        output_height = (int)((int64_t)source_height * maximum / longest);
        if (!output_width)
            output_width = 1;
        if (!output_height)
            output_height = 1;
    }
    for (int iteration = 0; iteration < 8; iteration++)
    {
        struct timespec start, end;
        int checks = 0;
        benchmark_time(&start);
        uint8_t *pixels = cancellable         ? image_load_rgba8_scaled_cancellable(bytes, (size_t)length,
                                                                                    output_width, output_height, &width, &height, cancel ? benchmark_cancel : benchmark_continue, &checks)
                          : scaled && maximum ? image_load_rgba8_scaled_from_memory(bytes, (size_t)length, output_width, output_height, &width, &height)
                                              : image_load_rgba8_from_memory(bytes, (size_t)length, &width, &height);
        benchmark_time(&end);
        if ((!pixels && !cancel) || (cancel && (pixels || checks != 3)))
        {
            image_free(pixels);
            free(bytes);
            return 1;
        }
        if (iteration)
            samples[iteration - 1] = (end.tv_sec - start.tv_sec) * 1000.0 +
                                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
        if (cancel)
            continue;
        uint8_t *output = pixels;
        if (width != output_width || height != output_height)
        {
            output = malloc((size_t)output_width * output_height * 4);
            if (!output)
            {
                image_free(pixels);
                free(bytes);
                return 1;
            }
            benchmark_time(&start);
            for (int row = 0; row < output_height; row++)
                image_resize_rgba8_row(pixels, width, height, output, output_width, output_height, row);
            benchmark_time(&end);
            if (iteration)
                resize_samples[iteration - 1] = (end.tv_sec - start.tv_sec) * 1000.0 +
                                                (end.tv_nsec - start.tv_nsec) / 1000000.0;
        }
        if (!iteration)
        {
            checksum = UINT64_C(14695981039346656037);
            for (size_t offset = 0; offset < (size_t)output_width * output_height * 4; offset++)
                checksum = (checksum ^ output[offset]) * UINT64_C(1099511628211);
        }
        if (output != pixels)
            free(output);
        image_free(pixels);
    }
    free(bytes);
    if (cancel)
    {
        printf("cancelled %dx%d after first scanline batch: %.3f ms\n", source_width, source_height, median(samples));
        return 0;
    }
    printf("%s %dx%d -> decoded %dx%d (%.2f MiB RGBA) -> %dx%d decode %.3f ms resize %.3f ms checksum %016llx\n",
           cancellable ? "cancellable" : scaled ? "scaled"
                                                : "full",
           source_width, source_height, width, height,
           (double)width * height * 4 / (1024 * 1024), output_width, output_height, median(samples), median(resize_samples),
           (unsigned long long)checksum);
    return 0;
}