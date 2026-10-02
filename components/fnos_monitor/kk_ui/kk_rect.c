// kk_rect：KK rect 模型 → LVGL 坐标。见 kk_rect.h 与 docs/ui-kk.md §4。
#include "kk_rect.h"

void kk_place(lv_obj_t *obj, lv_obj_t *parent, kk_rect_t r)
{
    int pw = lv_obj_get_content_width(parent);
    int ph = lv_obj_get_content_height(parent);
    if (pw <= 0) pw = lv_obj_get_width(parent);
    if (ph <= 0) ph = lv_obj_get_height(parent);

    int w = (int)(pw * (r.amax_x - r.amin_x) + r.size_w);
    int h = (int)(ph * (r.amax_y - r.amin_y) + r.size_h);
    int x = (int)(pw * r.amin_x) + r.pos_x;
    int y = (int)(ph * r.amin_y) + r.pos_y;

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w > 1 ? w : 1, h > 1 ? h : 1);
}
