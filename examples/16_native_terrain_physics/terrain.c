#include "terrain.h"

#include <math.h>
#include <stddef.h>

#define TERRAIN_SPACING 1.5f
#define HEIGHT_SCALE 3.2f
#define NOISE_SCALE 0.055f
#define NORMAL_EPSILON 0.9f

static uint32_t hash2(int32_t x, int32_t y)
{
    uint32_t h = (uint32_t)x * UINT32_C(0x27d4eb2d) ^
                 (uint32_t)y * UINT32_C(0x165667b1) ^
                 UINT32_C(0x9e3779b1);
    h = (h ^ (h >> 15)) * UINT32_C(0x85ebca6b);
    h = (h ^ (h >> 13)) * UINT32_C(0xc2b2ae35);
    return h ^ (h >> 16);
}

static float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float mixf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float value_noise(float x, float y)
{
    int32_t ix = (int32_t)floorf(x);
    int32_t iy = (int32_t)floorf(y);
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    float a = (float)hash2(ix, iy) / 4294967296.0f;
    float b = (float)hash2(ix + 1, iy) / 4294967296.0f;
    float c = (float)hash2(ix, iy + 1) / 4294967296.0f;
    float d = (float)hash2(ix + 1, iy + 1) / 4294967296.0f;
    return mixf(mixf(a, b, fade(fx)), mixf(c, d, fade(fx)), fade(fy)) *
               2.0f -
           1.0f;
}

float terrain_fbm(float x, float y, int octaves, float persistence)
{
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maximum = 0.0f;
    int octave;
    for (octave = 0; octave < octaves; ++octave)
    {
        total += value_noise(x * frequency, y * frequency) * amplitude;
        maximum += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    return total / maximum;
}

float terrain_height(float x, float y)
{
    return terrain_fbm(x * NOISE_SCALE, y * NOISE_SCALE, 4, 0.45f) * HEIGHT_SCALE;
}

Vec3 terrain_normal(float x, float y)
{
    float left = terrain_height(x - NORMAL_EPSILON, y);
    float right = terrain_height(x + NORMAL_EPSILON, y);
    float down = terrain_height(x, y - NORMAL_EPSILON);
    float up = terrain_height(x, y + NORMAL_EPSILON);
    return vec3_normalize(vec3((left - right) / (2.0f * NORMAL_EPSILON),
                               (down - up) / (2.0f * NORMAL_EPSILON), 1.0f));
}

void terrain_build(TerrainVertex vertices[TERRAIN_VERTEX_COUNT],
                   uint16_t indices[TERRAIN_INDEX_COUNT], float center_x,
                   float center_y)
{
    float origin_x = floorf(center_x / 12.0f) * 12.0f -
                     TERRAIN_CELLS * TERRAIN_SPACING * 0.5f;
    float origin_y = floorf(center_y / 12.0f) * 12.0f -
                     TERRAIN_CELLS * TERRAIN_SPACING * 0.5f;
    size_t vertex = 0;
    size_t index = 0;
    int y;
    int x;
    for (y = 0; y < TERRAIN_SIDE; ++y)
    {
        for (x = 0; x < TERRAIN_SIDE; ++x)
        {
            float world_x = origin_x + x * TERRAIN_SPACING;
            float world_y = origin_y + y * TERRAIN_SPACING;
            Vec3 normal = terrain_normal(world_x, world_y);
            vertices[vertex].position[0] = world_x;
            vertices[vertex].position[1] = world_y;
            vertices[vertex].position[2] = terrain_height(world_x, world_y);
            vertices[vertex].normal[0] = normal.x;
            vertices[vertex].normal[1] = normal.y;
            vertices[vertex].normal[2] = normal.z;
            ++vertex;
        }
    }
    for (y = 0; y < TERRAIN_CELLS; ++y)
    {
        for (x = 0; x < TERRAIN_CELLS; ++x)
        {
            uint16_t a = (uint16_t)(y * TERRAIN_SIDE + x);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + TERRAIN_SIDE);
            uint16_t d = (uint16_t)(c + 1);
            indices[index++] = a;
            indices[index++] = b;
            indices[index++] = d;
            indices[index++] = a;
            indices[index++] = d;
            indices[index++] = c;
        }
    }
}