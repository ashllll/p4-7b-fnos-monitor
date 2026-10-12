#include <stdint.h>

/* Chart-only replacement for LVGL lv_map. Keep its endpoint clamping and
   truncation, including descending input/output ranges. Each difference fits
   uint32_t; their product fits uint64_t even across the full int32 domain.
   After clamping x, the quotient is at most the output span, so the final
   signed result stays between the two int32 output endpoints. */
int32_t fnos_chart_map(int32_t x, int32_t min_in, int32_t max_in,
                       int32_t min_out, int32_t max_out)
{
    if (max_in == min_in) return min_out;
    if (max_in >= min_in && x >= max_in) return max_out;
    if (max_in >= min_in && x <= min_in) return min_out;
    if (max_in <= min_in && x <= max_in) return max_out;
    if (max_in <= min_in && x >= min_in) return min_out;

    int64_t delta_in = (int64_t)max_in - min_in;
    int64_t offset = (int64_t)x - min_in;
    int64_t delta_out = (int64_t)max_out - min_out;
    uint64_t distance = (uint64_t)(offset < 0 ? -offset : offset);
    uint64_t span_in = (uint64_t)(delta_in < 0 ? -delta_in : delta_in);
    uint64_t span_out = (uint64_t)(delta_out < 0 ? -delta_out : delta_out);
    int64_t step = (int64_t)((distance * span_out) / span_in);
    return (int32_t)((int64_t)min_out + (delta_out < 0 ? -step : step));
}
