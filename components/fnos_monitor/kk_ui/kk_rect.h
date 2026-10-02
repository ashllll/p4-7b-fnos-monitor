#pragma once
// KK rect 模型（anchorMin/anchorMax/position/size）→ LVGL 坐标。
// 语义（LVGL 移植口径，见 docs/ui-kk.md §4）：
//   w = parent_w*(amax_x-amin_x) + size_w   （size_w<0 时即"父宽 + 负值"，做边距拉伸）
//   h = parent_h*(amax_y-amin_y) + size_h
//   x = parent_w*amin_x + pos_x             （position = 相对锚点的左上角偏移）
//   y = parent_h*amin_y + pos_y
// 固定锚点（amin==amax）时宽高就是 size 本身。

#include "lvgl.h"

typedef struct {
    float amin_x, amin_y, amax_x, amax_y;
    int   pos_x, pos_y, size_w, size_h;
} kk_rect_t;

static inline kk_rect_t kk_rect(float amin_x, float amin_y, float amax_x, float amax_y,
                                int pos_x, int pos_y, int size_w, int size_h)
{
    kk_rect_t r = { amin_x, amin_y, amax_x, amax_y, pos_x, pos_y, size_w, size_h };
    return r;
}

void kk_place(lv_obj_t *obj, lv_obj_t *parent, kk_rect_t r);
