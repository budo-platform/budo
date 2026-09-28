#ifndef NATIVE_TERRAIN_PHYSICS_H
#define NATIVE_TERRAIN_PHYSICS_H

#include "math3d.h"

#include <stdbool.h>

typedef struct CarControls
{
    float throttle;
    float steer;
} CarControls;

typedef struct PhysicsSettings
{
    float mass;
    float gravity;
    float move_force;
    float linear_drag;
    float rolling_friction;
    float max_speed;
    float turn_rate;
    float vehicle_scale;
} PhysicsSettings;

typedef struct CarForces
{
    Vec3 gravity;
    Vec3 reaction;
    Vec3 resultant;
    Vec3 gravity_tangent;
} CarForces;

typedef struct Car
{
    float x;
    float y;
    float z;
    float heading;
    float vx;
    float vy;
    float vz;
    float yaw_rate;
    float pitch;
    float roll;
    float pitch_rate;
    float roll_rate;
    float airborne_time;
    float ground_z;
    float compression;
    int contact_count;
    bool is_ai;
    float ai_timer;
    float ai_throttle;
    float ai_steer;
    float color[3];
    bool grounded;
} Car;

void physics_reset(PhysicsSettings *settings);
void car_reset(Car *car, float x, float y, float heading, bool is_ai,
               float red, float green, float blue);
CarControls car_ai_controls(Car *car, float dt, float target_x, float target_y,
                            float target_vx, float target_vy);
CarForces car_update(Car *car, const PhysicsSettings *settings, float dt,
                     CarControls controls);
void cars_resolve_collisions(Car *cars, int count,
                             const PhysicsSettings *settings);
bool car_resolve_tree_collision(Car *car, const PhysicsSettings *settings,
                                float tree_x, float tree_y, float radius);
float car_speed(const Car *car);
void car_visual_axes(const Car *car, Vec3 *right, Vec3 *forward, Vec3 *up);
void car_model_matrix(const Car *car, const PhysicsSettings *settings,
                      float out[16]);

#endif