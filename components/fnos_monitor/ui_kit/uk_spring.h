#pragma once
#include "uk_theme.h"
#include <math.h>

/* Exact critically damped response. Time is in milliseconds, velocity in
   units/ms. Retargeting preserves both position and velocity. */
typedef struct { float displacement, velocity, displacement_rate, velocity_rate; } uk_spring_response_t;
float uk_spring_decay(float normalized_time);
static inline uk_spring_response_t uk_spring_response(uint32_t elapsed, uint32_t duration)
{
    if (!duration) return (uk_spring_response_t){0};
    const float w = (float)UK_TELEMETRY_DAMPING / duration;
    const float t = elapsed, e = uk_spring_decay(t / duration);
    /* Interpolated exp can overestimate the first sub-tick displacement by
       <0.03px at long response settings. Never begin opposite the target. */
    const float displacement=LV_MIN(1.f,(1 + w*t)*e);
    return (uk_spring_response_t){displacement, t*e, -w*w*t*e, (1 - w*t)*e};
}
static inline float uk_spring_value(float from, float velocity, float target,
                                    uk_spring_response_t r, float *out_velocity)
{
    *out_velocity = (from-target)*r.displacement_rate + velocity*r.velocity_rate;
    return target + (from-target)*r.displacement + velocity*r.velocity;
}
