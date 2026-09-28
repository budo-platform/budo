#include "physics.h"

#include "terrain.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define CAR_LENGTH 1.5f
#define CAR_WIDTH 0.8f
#define CAR_HEIGHT 0.5f
#define WHEELBASE (CAR_LENGTH * 0.92f)
#define TRACK (CAR_WIDTH * 0.96f)
#define SUSPENSION_TRAVEL 0.78f
#define GROUND_SNAP_EPS 0.06f
#define TAKEOFF_SPEED 21.0f
#define TAKEOFF_VZ_THRESHOLD 3.1f
#define LANDING_RESTITUTION 0.18f
#define REST_VZ_EPS 0.7f
#define MAX_STEER_ANGLE 0.68f
#define LOW_SPEED_STEER 0.66f
#define TIRE_GRIP 13.8f
#define LATERAL_STIFFNESS 8.9f
#define LONGITUDINAL_GRIP 1.48f
#define YAW_RESPONSE 13.5f
#define YAW_DAMPING_GROUND 4.8f
#define YAW_DAMPING_AIR 0.35f
#define AIR_ANGULAR_DAMPING 0.55f
#define BODY_SETTLE_RATE 9.5f
#define SUSPENSION_FOLLOW_RATE 16.0f
#define SLOPE_VELOCITY_FOLLOW_RATE 7.0f
#define COLLISION_RESTITUTION 0.18f
#define COLLISION_FRICTION 0.32f
#define COLLISION_SLOP 0.03f
#define COLLISION_CORRECTION 0.72f

typedef struct Support
{
    int contacts;
    float z;
    float compression;
    Vec3 normal;
} Support;

static float approach(float current, float target, float rate, float dt)
{
    return current + (target - current) * (1.0f - expf(-rate * dt));
}

static float friction(float value, float amount)
{
    if (fabsf(value) <= amount)
        return 0.0f;
    return value - copysignf(amount, value);
}

static float random01(Car *car)
{
    uint32_t bits = (uint32_t)(fabsf(car->x * 1973.0f) +
                               fabsf(car->y * 9277.0f) +
                               car->ai_timer * 26699.0f + 911.0f);
    bits ^= bits << 13;
    bits ^= bits >> 17;
    bits ^= bits << 5;
    return (float)(bits & 0x00ffffffu) / 16777216.0f;
}

void physics_reset(PhysicsSettings *settings)
{
    settings->mass = 1.0f;
    settings->gravity = 9.81f;
    settings->move_force = 42.0f;
    settings->linear_drag = 0.58f;
    settings->rolling_friction = 1.75f;
    settings->max_speed = 120.0f;
    settings->turn_rate = 4.25f;
    settings->vehicle_scale = 2.3f;
}

void car_reset(Car *car, float x, float y, float heading, bool is_ai,
               float red, float green, float blue)
{
    memset(car, 0, sizeof(*car));
    car->x = x;
    car->y = y;
    car->z = terrain_height(x, y);
    car->ground_z = car->z;
    car->heading = heading;
    car->grounded = true;
    car->contact_count = 4;
    car->compression = 1.0f;
    car->is_ai = is_ai;
    car->color[0] = red;
    car->color[1] = green;
    car->color[2] = blue;
}

float car_speed(const Car *car)
{
    return hypotf(car->vx, car->vy);
}

static Support sample_support(const Car *car, Vec3 forward, Vec3 right)
{
    static const float offsets[4][2] = {
        {WHEELBASE * 0.5f, TRACK * 0.5f},
        {WHEELBASE * 0.5f, -TRACK * 0.5f},
        {-WHEELBASE * 0.5f, TRACK * 0.5f},
        {-WHEELBASE * 0.5f, -TRACK * 0.5f}};
    Support support = {0};
    float weight_sum = 0.0f;
    float maximum = -1e30f;
    int index;
    for (index = 0; index < 4; ++index)
    {
        float x = car->x + forward.x * offsets[index][0] +
                  right.x * offsets[index][1];
        float y = car->y + forward.y * offsets[index][0] +
                  right.y * offsets[index][1];
        float height = terrain_height(x, y);
        float above = car->z - height;
        float compression = clampf((SUSPENSION_TRAVEL - fmaxf(0.0f, above)) /
                                       SUSPENSION_TRAVEL,
                                   0.0f, 1.0f);
        if (above <= SUSPENSION_TRAVEL)
        {
            Vec3 normal = terrain_normal(x, y);
            float weight = 0.20f + compression;
            support.contacts++;
            weight_sum += weight;
            support.z += height * weight;
            support.compression += compression;
            support.normal = vec3_add(support.normal,
                                      vec3_scale(normal, weight));
        }
        maximum = fmaxf(maximum, height);
    }
    if (weight_sum > 0.00001f)
    {
        support.z /= weight_sum;
        support.normal = vec3_normalize(support.normal);
        support.compression /= (float)support.contacts;
    }
    else
    {
        support.z = terrain_height(car->x, car->y);
        support.normal = terrain_normal(car->x, car->y);
    }
    support.z = fmaxf(support.z, maximum - 0.28f);
    return support;
}

static float wrap_angle(float value)
{
    while (value > PI)
        value -= 2.0f * PI;
    while (value < -PI)
        value += 2.0f * PI;
    return value;
}

CarControls car_ai_controls(Car *car, float dt, float target_x, float target_y,
                            float target_vx, float target_vy)
{
    CarControls controls;
    float dx;
    float dy;
    float error;
    float alignment;
    car->ai_timer -= dt;
    if (car->ai_timer <= 0.0f)
    {
        car->ai_throttle = 0.55f + random01(car) * 0.45f;
        car->ai_steer = random01(car) * 1.8f - 0.9f;
        car->ai_timer = 4.0f;
    }
    dx = target_x + target_vx * 0.95f - car->x;
    dy = target_y + target_vy * 0.95f - car->y;
    error = wrap_angle(atan2f(dy, dx) - car->heading);
    alignment = fmaxf(0.15f, 1.0f - fminf(1.0f, fabsf(error) / PI));
    controls.throttle = clampf(0.62f + 0.38f * alignment +
                                   car->ai_throttle * 0.10f,
                               -0.25f, 1.0f);
    controls.steer = clampf(-error * 1.35f + car->ai_steer * 0.28f,
                            -1.0f, 1.0f);
    return controls;
}

CarForces car_update(Car *car, const PhysicsSettings *settings, float dt,
                     CarControls controls)
{
    CarForces forces = {0};
    Vec3 forward;
    Vec3 right;
    Support support;
    float speed;
    float forward_speed;
    float side_speed;
    float active_throttle = 0.0f;
    float tire_x = 0.0f;
    float tire_y = 0.0f;
    bool was_grounded = car->grounded;
    bool just_left;
    dt = fminf(dt, 0.02f);
    controls.throttle = clampf(controls.throttle, -1.0f, 1.0f);
    controls.steer = clampf(controls.steer, -1.0f, 1.0f);
    forward = vec3(cosf(car->heading), sinf(car->heading), 0.0f);
    right = vec3(forward.y, -forward.x, 0.0f);
    support = sample_support(car, forward, right);
    speed = car_speed(car);
    {
        float above = car->z - support.z;
        bool maintain = support.contacts > 0 &&
                        above <= SUSPENSION_TRAVEL + GROUND_SNAP_EPS &&
                        (was_grounded || car->vz <= TAKEOFF_VZ_THRESHOLD);
        bool takeoff = was_grounded && speed > TAKEOFF_SPEED &&
                       car->vz > TAKEOFF_VZ_THRESHOLD && above > GROUND_SNAP_EPS;
        car->grounded = maintain && !takeoff;
    }
    car->contact_count = car->grounded ? support.contacts : 0;
    car->compression = car->grounded ? support.compression : 0.0f;
    if (car->grounded)
    {
        car->z = approach(car->z, support.z, SUSPENSION_FOLLOW_RATE, dt);
        if (fabsf(car->z - support.z) < 0.025f)
            car->z = support.z;
        if (car->vz < 0.0f)
            car->vz = 0.0f;
        car->airborne_time = 0.0f;
        car->ground_z = support.z;
    }
    else
        car->airborne_time += dt;
    just_left = was_grounded && !car->grounded;
    forward_speed = car->vx * forward.x + car->vy * forward.y;
    side_speed = car->vx * right.x + car->vy * right.y;
    forces.gravity = vec3(0, 0, -settings->gravity * settings->mass);
    if (car->grounded)
    {
        float gravity_dot = vec3_dot(forces.gravity, support.normal);
        float load = clampf(0.35f + support.compression * 0.95f, 0.35f, 1.3f);
        float grip = TIRE_GRIP * load;
        float drive;
        float lateral;
        float rolling;
        float new_forward;
        forces.reaction = vec3_scale(support.normal, -gravity_dot);
        forces.gravity_tangent = vec3_add(forces.gravity, forces.reaction);
        active_throttle = controls.throttle;
        drive = clampf(active_throttle * settings->move_force /
                           fmaxf(0.1f, settings->mass),
                       -grip * LONGITUDINAL_GRIP, grip * LONGITUDINAL_GRIP);
        lateral = clampf(-side_speed * LATERAL_STIFFNESS, -grip, grip);
        tire_x = forward.x * drive + right.x * lateral;
        tire_y = forward.y * drive + right.y * lateral;
        rolling = settings->rolling_friction * dt *
                  (fabsf(active_throttle) < 0.0001f ? 1.0f : 0.35f);
        new_forward = friction(forward_speed, rolling);
        car->vx += forward.x * (new_forward - forward_speed);
        car->vy += forward.y * (new_forward - forward_speed);
    }
    {
        float drag_scale = car->grounded ? 0.55f : 0.30f;
        float inverse_mass = 1.0f / fmaxf(0.1f, settings->mass);
        float ax = tire_x + (car->grounded ? forces.gravity_tangent.x * inverse_mass : 0) -
                   car->vx * settings->linear_drag * drag_scale;
        float ay = tire_y + (car->grounded ? forces.gravity_tangent.y * inverse_mass : 0) -
                   car->vy * settings->linear_drag * drag_scale;
        car->vx += ax * dt;
        car->vy += ay * dt;
        car->vz += (car->grounded ? 0.0f : -settings->gravity) * dt;
    }
    if (car->grounded)
    {
        float angle = controls.steer * MAX_STEER_ANGLE;
        float low = LOW_SPEED_STEER + (1.0f - LOW_SPEED_STEER) *
                                          clampf(fabsf(forward_speed) / 10.0f, 0, 1);
        float normalized = forward_speed / (fabsf(forward_speed) + 3.0f);
        float target = -sinf(angle) * settings->turn_rate * normalized * low;
        car->yaw_rate = approach(car->yaw_rate, target, YAW_RESPONSE, dt);
        car->yaw_rate *= expf(-YAW_DAMPING_GROUND *
                              fmaxf(0.0f, fabsf(side_speed) - 0.25f) *
                              0.035f * dt);
    }
    else
        car->yaw_rate *= expf(-YAW_DAMPING_AIR * dt);
    car->heading += car->yaw_rate * dt;
    if (just_left)
    {
        float slope_forward = support.normal.x * forward.x + support.normal.y * forward.y;
        float slope_right = support.normal.x * right.x + support.normal.y * right.y;
        car->pitch_rate += clampf(-slope_forward * speed * 0.22f +
                                      controls.throttle * 0.10f,
                                  -1.2f, 1.2f);
        car->roll_rate += clampf(slope_right * speed * 0.18f +
                                     controls.steer * speed * 0.035f,
                                 -1.4f, 1.4f);
    }
    forward = vec3(cosf(car->heading), sinf(car->heading), 0);
    right = vec3(forward.y, -forward.x, 0);
    speed = car_speed(car);
    if (speed > settings->max_speed)
    {
        car->vx *= settings->max_speed / speed;
        car->vy *= settings->max_speed / speed;
    }
    car->x += car->vx * dt;
    car->y += car->vy * dt;
    if (car->grounded)
    {
        Support next = sample_support(car, forward, right);
        float target_vz;
        car->z = approach(car->z, next.z, SUSPENSION_FOLLOW_RATE, dt);
        if (fabsf(car->z - next.z) < 0.018f)
            car->z = next.z;
        car->ground_z = next.z;
        car->contact_count = next.contacts;
        car->compression = next.compression;
        target_vz = -(next.normal.x * car->vx + next.normal.y * car->vy) /
                    fmaxf(0.35f, next.normal.z);
        car->vz = approach(car->vz, target_vz, SLOPE_VELOCITY_FOLLOW_RATE, dt);
        if (car->vz > TAKEOFF_VZ_THRESHOLD && speed < TAKEOFF_SPEED)
            car->vz = TAKEOFF_VZ_THRESHOLD * 0.45f;
        car->pitch *= expf(-BODY_SETTLE_RATE * dt);
        car->roll *= expf(-BODY_SETTLE_RATE * dt);
        car->pitch_rate *= expf(-BODY_SETTLE_RATE * 1.2f * dt);
        car->roll_rate *= expf(-BODY_SETTLE_RATE * 1.2f * dt);
    }
    else
    {
        float height;
        car->z += car->vz * dt;
        car->pitch += car->pitch_rate * dt;
        car->roll += car->roll_rate * dt;
        car->pitch_rate *= expf(-AIR_ANGULAR_DAMPING * dt);
        car->roll_rate *= expf(-AIR_ANGULAR_DAMPING * dt);
        car->pitch = clampf(car->pitch, -1.15f, 1.15f);
        car->roll = clampf(car->roll, -1.25f, 1.25f);
        height = terrain_height(car->x, car->y);
        if (car->z < height)
        {
            Vec3 normal = terrain_normal(car->x, car->y);
            float normal_speed = car->vx * normal.x + car->vy * normal.y +
                                 car->vz * normal.z;
            car->z = height;
            if (normal_speed < 0)
            {
                car->vx -= (1.0f + LANDING_RESTITUTION) * normal_speed * normal.x;
                car->vy -= (1.0f + LANDING_RESTITUTION) * normal_speed * normal.y;
                car->vz -= (1.0f + LANDING_RESTITUTION) * normal_speed * normal.z;
            }
            car->grounded = fabsf(car->vz) < REST_VZ_EPS || normal_speed > -2.8f;
            if (car->grounded)
            {
                car->vz = 0;
                car->airborne_time = 0;
            }
            car->pitch_rate *= 0.42f;
            car->roll_rate *= 0.42f;
        }
    }
    forces.resultant = vec3(forward.x * active_throttle * settings->move_force +
                                (car->grounded ? right.x * -side_speed *
                                                     LATERAL_STIFFNESS * settings->mass
                                               : 0),
                            forward.y * active_throttle * settings->move_force +
                                (car->grounded ? right.y * -side_speed *
                                                     LATERAL_STIFFNESS * settings->mass
                                               : 0),
                            0);
    return forces;
}

static float collision_radius(const PhysicsSettings *settings)
{
    return fmaxf(CAR_LENGTH * settings->vehicle_scale * 0.45f,
                 CAR_WIDTH * settings->vehicle_scale * 0.76f);
}

void cars_resolve_collisions(Car *cars, int count,
                             const PhysicsSettings *settings)
{
    float radius = collision_radius(settings);
    float minimum = radius * 2.0f;
    float inverse_mass = 1.0f / fmaxf(0.1f, settings->mass);
    int i;
    int j;
    for (i = 0; i < count; ++i)
        for (j = i + 1; j < count; ++j)
        {
            float dx = cars[i].x - cars[j].x;
            float dy = cars[i].y - cars[j].y;
            float distance2 = dx * dx + dy * dy;
            float distance;
            float nx;
            float ny;
            float penetration;
            float relative_normal;
            if (distance2 >= minimum * minimum)
                continue;
            if (distance2 < 0.000001f)
            {
                dx = cosf(cars[i].heading);
                dy = sinf(cars[i].heading);
                distance2 = 1;
            }
            distance = sqrtf(distance2);
            nx = dx / distance;
            ny = dy / distance;
            penetration = minimum - distance;
            {
                float correction = fmaxf(0.0f, penetration - COLLISION_SLOP) *
                                   COLLISION_CORRECTION * 0.5f;
                cars[i].x += nx * correction;
                cars[i].y += ny * correction;
                cars[j].x -= nx * correction;
                cars[j].y -= ny * correction;
            }
            relative_normal = (cars[i].vx - cars[j].vx) * nx +
                              (cars[i].vy - cars[j].vy) * ny;
            if (relative_normal < 0)
            {
                float impulse = -(1.0f + COLLISION_RESTITUTION) * relative_normal /
                                (inverse_mass * 2.0f);
                float tangent_x = -ny;
                float tangent_y = nx;
                float relative_tangent;
                float friction_impulse;
                cars[i].vx += impulse * nx * inverse_mass;
                cars[i].vy += impulse * ny * inverse_mass;
                cars[j].vx -= impulse * nx * inverse_mass;
                cars[j].vy -= impulse * ny * inverse_mass;
                relative_tangent = (cars[i].vx - cars[j].vx) * tangent_x +
                                   (cars[i].vy - cars[j].vy) * tangent_y;
                friction_impulse = clampf(-relative_tangent /
                                              (inverse_mass * 2.0f),
                                          -impulse * COLLISION_FRICTION,
                                          impulse * COLLISION_FRICTION);
                cars[i].vx += friction_impulse * tangent_x * inverse_mass;
                cars[i].vy += friction_impulse * tangent_y * inverse_mass;
                cars[j].vx -= friction_impulse * tangent_x * inverse_mass;
                cars[j].vy -= friction_impulse * tangent_y * inverse_mass;
                cars[i].yaw_rate += clampf(relative_tangent * 0.035f, -0.55f, 0.55f);
                cars[j].yaw_rate -= clampf(relative_tangent * 0.035f, -0.55f, 0.55f);
            }
        }
}

bool car_resolve_tree_collision(Car *car, const PhysicsSettings *settings,
                                float tree_x, float tree_y, float radius)
{
    float minimum = collision_radius(settings) + radius;
    float dx = car->x - tree_x;
    float dy = car->y - tree_y;
    float distance2 = dx * dx + dy * dy;
    float distance;
    float nx;
    float ny;
    float normal_speed;
    if (distance2 >= minimum * minimum)
        return false;
    if (distance2 < 0.000001f)
    {
        dx = cosf(car->heading);
        dy = sinf(car->heading);
        distance2 = 1;
    }
    distance = sqrtf(distance2);
    nx = dx / distance;
    ny = dy / distance;
    car->x += nx * fmaxf(0.0f, minimum - distance - COLLISION_SLOP) * 0.92f;
    car->y += ny * fmaxf(0.0f, minimum - distance - COLLISION_SLOP) * 0.92f;
    normal_speed = car->vx * nx + car->vy * ny;
    if (normal_speed < 0)
    {
        float tangent_x = -ny;
        float tangent_y = nx;
        float tangent_speed;
        car->vx -= 1.08f * normal_speed * nx;
        car->vy -= 1.08f * normal_speed * ny;
        tangent_speed = car->vx * tangent_x + car->vy * tangent_y;
        car->vx -= tangent_x * tangent_speed * 0.55f;
        car->vy -= tangent_y * tangent_speed * 0.55f;
        car->yaw_rate += clampf(tangent_speed * 0.045f, -0.75f, 0.75f);
    }
    return true;
}

void car_visual_axes(const Car *car, Vec3 *right, Vec3 *forward, Vec3 *up)
{
    Vec3 f = vec3(cosf(car->heading), sinf(car->heading), 0);
    if (car->grounded)
    {
        *up = terrain_normal(car->x, car->y);
        f = vec3_normalize(vec3_sub(f, vec3_scale(*up, vec3_dot(f, *up))));
        *right = vec3_normalize(vec3_cross(f, *up));
        *forward = f;
    }
    else
    {
        Vec3 base_right = vec3(f.y, -f.x, 0);
        float cp = cosf(car->pitch), sp = sinf(car->pitch);
        float cr = cosf(car->roll), sr = sinf(car->roll);
        Vec3 pitched_forward = vec3(f.x * cp, f.y * cp, sp);
        Vec3 pitched_up = vec3(-f.x * sp, -f.y * sp, cp);
        *right = vec3_sub(vec3_scale(base_right, cr), vec3_scale(pitched_up, sr));
        *forward = pitched_forward;
        *up = vec3_add(vec3_scale(base_right, sr), vec3_scale(pitched_up, cr));
    }
}

void car_model_matrix(const Car *car, const PhysicsSettings *settings,
                      float out[16])
{
    Vec3 right, forward, up;
    float scale = settings->vehicle_scale;
    car_visual_axes(car, &right, &forward, &up);
    out[0] = right.x * CAR_WIDTH * scale;
    out[1] = right.y * CAR_WIDTH * scale;
    out[2] = right.z * CAR_WIDTH * scale;
    out[3] = 0;
    out[4] = forward.x * CAR_LENGTH * scale;
    out[5] = forward.y * CAR_LENGTH * scale;
    out[6] = forward.z * CAR_LENGTH * scale;
    out[7] = 0;
    out[8] = up.x * CAR_HEIGHT * scale;
    out[9] = up.y * CAR_HEIGHT * scale;
    out[10] = up.z * CAR_HEIGHT * scale;
    out[11] = 0;
    out[12] = car->x + up.x * CAR_HEIGHT * scale * 0.5f;
    out[13] = car->y + up.y * CAR_HEIGHT * scale * 0.5f;
    out[14] = car->z + up.z * CAR_HEIGHT * scale * 0.5f;
    out[15] = 1;
}