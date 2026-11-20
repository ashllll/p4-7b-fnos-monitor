#!/usr/bin/env python3
"""Exercise production polyline batching without an ESP32 toolchain."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "components/fnos_monitor/ui_kit/uk.c").read_text()
start = source.index("static void trend_batch_flush(")
end = source.index("static void trend_draw_series(", start)
code = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
typedef struct { int x, y; } lv_point_precise_t;
#include "d3_basis.h"
@BATCH_LIMIT@
enum { LV_OPA_COVER = 255 };
typedef struct {
    struct { void *layer; } base;
    lv_point_precise_t *points;
    unsigned point_cnt;
    unsigned opa;
    unsigned dash_width, dash_gap;
    bool round_start, round_end;
} lv_draw_line_dsc_t;
typedef struct {
    lv_draw_line_dsc_t line;
    lv_point_precise_t points[TREND_DRAW_POINTS];
    unsigned count;
} trend_batch_t;
static unsigned calls, segments;
static void lv_draw_line(void *layer, const lv_draw_line_dsc_t *line) {
    (void)layer;
    calls++;
    segments += line->point_cnt - 1;
}
'''
limit_start = source.index("enum { TREND_DRAW_POINTS = ")
code = code.replace("@BATCH_LIMIT@", source[limit_start:source.index("\n", limit_start)])
checks = r'''
int main(void) {
    /* The renderer reuses shared cubic endpoints instead of evaluating t=0/1. */
    for (int i = -180; i < 180; i++) {
        d3_basis_point_t p[5];
        for (int j = 0; j < 5; j++) p[j] = (d3_basis_point_t){(i+j)*4.25f, (i+j)*(i+j)*.015f};
        d3_basis_segment_t c = d3_basis_segment(p[0],p[1],p[2],p[3]);
        d3_basis_segment_t next = d3_basis_segment(p[1],p[2],p[3],p[4]);
        d3_basis_point_t start = d3_basis_at(c,0), end = d3_basis_at(c,1);
        assert(start.x==c.p[0].x && start.y==c.p[0].y);
        assert(end.x==c.p[3].x && end.y==c.p[3].y);
        assert(end.x==next.p[0].x && end.y==next.p[0].y);
    }
    trend_batch_t b = {0};
    for (int x = 0; x <= 1000; x++) trend_batch_point(&b, (d3_basis_point_t){x, 9});
    assert(b.count == 2 && b.points[0].x == 0 && b.points[1].x == 1000);
    trend_batch_flush(&b);
    assert(calls == 1 && segments == 1);
    /* A missing-value flush must not connect the next run to the old one. */
    trend_batch_point(&b, (d3_basis_point_t){1003, 9});
    trend_batch_flush(&b);
    assert(calls == 1);
    /* Preserve turns, reversals and continuity across a full batch. */
    for (int x = 0; x < 130; x++) trend_batch_point(&b, (d3_basis_point_t){x, x % 2});
    trend_batch_flush(&b);
    assert(segments == 130);
    trend_batch_point(&b, (d3_basis_point_t){0, 9});
    trend_batch_point(&b, (d3_basis_point_t){2, 9});
    trend_batch_point(&b, (d3_basis_point_t){1, 9});
    assert(b.count == 3);
    trend_batch_flush(&b);
    b.line.round_end = true;
    for (int x = 0; x < 4; x++) trend_batch_point(&b, (d3_basis_point_t){x, 9});
    assert(b.count == 4);
    trend_batch_flush(&b);
    b.line.opa = LV_OPA_COVER;
    for (int x = 0; x < 4; x++) trend_batch_point(&b, (d3_basis_point_t){x, 9});
    assert(b.count == 2);
    trend_batch_flush(&b);
    b.line.dash_width = b.line.dash_gap = 2;
    for (int x = 0; x < 4; x++) trend_batch_point(&b, (d3_basis_point_t){x, 9});
    assert(b.count == 4);
}
'''
with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / "batch.c"
    exe = Path(folder) / "batch"
    src.write_text(code + source[start:end] + checks)
    env = dict(os.environ, DEVELOPER_DIR=os.environ.get("DEVELOPER_DIR", "/Library/Developer/CommandLineTools"))
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(root / "components/fnos_monitor/ui_kit/third_party"), str(src), "-lm", "-o", str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True)
print("trend batching: shared endpoints, flat runs, gaps, turns, full batches and rounded caps PASS")
