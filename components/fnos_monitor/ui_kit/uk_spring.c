#include "uk_spring.h"

uk_spring_response_t uk_spring_response_for(float elapsed, uint32_t duration,
                                           float factor, float ratio)
{
    if (!duration) return (uk_spring_response_t){0};
    /* The LVGL thread shares exact coefficients across simultaneous meters and
       glyphs. Cache calculations, not a quantized approximation of the curve. */
    static struct {
        float elapsed, factor, ratio;
        uint32_t duration;
        uk_spring_response_t response;
    } cache;
    if (cache.duration != duration || cache.elapsed != elapsed ||
        cache.factor != factor || cache.ratio != ratio) {
        CalcDampedSpringMotionParams(&cache.response, elapsed / 1000.f,
                                    factor * 1000.f / duration, ratio);
        cache.duration = duration;
        cache.elapsed = elapsed;
        cache.factor = factor;
        cache.ratio = ratio;
    }
    return cache.response;
}
