#ifndef NATIVE_TERRAIN_H
#define NATIVE_TERRAIN_H

#include "math3d.h"

#include <stdint.h>

#define TERRAIN_CELLS 64
#define TERRAIN_SIDE (TERRAIN_CELLS + 1)
#define TERRAIN_VERTEX_COUNT (TERRAIN_SIDE * TERRAIN_SIDE)
#define TERRAIN_INDEX_COUNT (TERRAIN_CELLS * TERRAIN_CELLS * 6)

typedef struct TerrainVertex
{
    float position[3];
    float normal[3];
} TerrainVertex;

float terrain_height(float x, float y);
float terrain_fbm(float x, float y, int octaves, float persistence);
Vec3 terrain_normal(float x, float y);
void terrain_build(TerrainVertex vertices[TERRAIN_VERTEX_COUNT],
                   uint16_t indices[TERRAIN_INDEX_COUNT], float center_x,
                   float center_y);

#endif