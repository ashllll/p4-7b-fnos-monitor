#pragma once
#include "uk_theme.h"
#include <math.h>

#include "third_party/damped_spring.h"

/* The UI uses milliseconds and units/ms; the upstream solver uses seconds. */
typedef tDampedSpringMotionParams uk_spring_response_t;
uk_spring_response_t uk_spring_response_for(float elapsed, uint32_t duration,
                                           float factor, float ratio);
static inline uk_spring_response_t uk_spring_response(uint32_t elapsed, uint32_t duration)
{
    return uk_spring_response_for(elapsed, duration, UK_TELEMETRY_DAMPING,
                                 UK_TELEMETRY_RATIO);
}
static inline float uk_spring_value(float from, float velocity, float target,
                                    uk_spring_response_t r, float *out_velocity)
{
    velocity *= 1000.f;
    UpdateDampedSpringMotion(&from, &velocity, target, &r);
    *out_velocity = velocity / 1000.f;
    return from;
}
