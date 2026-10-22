#pragma once
/* C99 adaptation of d3-shape v3.2.0 src/curve/basis.js (ISC).
 * Same cubic control-point weights; chart ring neighbours replace JS streaming
 * state. Source, original digest and full license: README.md, d3-shape.LICENSE. */
typedef struct { float x, y; } d3_basis_point_t;
typedef struct { d3_basis_point_t p[4]; } d3_basis_segment_t;

static inline d3_basis_segment_t d3_basis_segment(d3_basis_point_t previous,
    d3_basis_point_t a, d3_basis_point_t b, d3_basis_point_t next)
{
    return (d3_basis_segment_t){{
        {(previous.x + 4*a.x + b.x)/6, (previous.y + 4*a.y + b.y)/6},
        {(2*a.x + b.x)/3, (2*a.y + b.y)/3},
        {(a.x + 2*b.x)/3, (a.y + 2*b.y)/3},
        {(a.x + 4*b.x + next.x)/6, (a.y + 4*b.y + next.y)/6}
    }};
}

static inline d3_basis_point_t d3_basis_at(d3_basis_segment_t c, float t)
{
    float u = 1-t;
    return (d3_basis_point_t){
        u*u*u*c.p[0].x + 3*u*u*t*c.p[1].x + 3*u*t*t*c.p[2].x + t*t*t*c.p[3].x,
        u*u*u*c.p[0].y + 3*u*u*t*c.p[1].y + 3*u*t*t*c.p[2].y + t*t*t*c.p[3].y
    };
}
