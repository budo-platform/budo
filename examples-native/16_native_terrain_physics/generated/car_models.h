#ifndef NATIVE_TERRAIN_CAR_MODELS_H
#define NATIVE_TERRAIN_CAR_MODELS_H

#include <stddef.h>
#include <stdint.h>

typedef struct CarModelVertex { float position[3]; float normal[3]; } CarModelVertex;
typedef struct CarModelData { const char *name; const CarModelVertex *vertices; size_t vertex_count; const uint16_t *indices; size_t index_count; } CarModelData;

extern const CarModelData budo_car_models[];
extern const size_t budo_car_model_count;

#endif