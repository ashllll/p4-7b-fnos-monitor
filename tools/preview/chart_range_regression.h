/* Runs at the existing chart geometry seam, before the product UI is built. */
static unsigned s_range_announcements;
static void range_announced(lv_event_t *e)
{
    (void)e;
    s_range_announcements++;
}

static int32_t range_cursor_y(lv_obj_t *chart)
{
    lv_obj_update_layout(chart);
    lv_refr_now(lv_display_get_default());
    lv_chart_cursor_t *cursor;
    LV_LL_READ(&((lv_chart_t *)chart)->cursor_ll, cursor) {
        if (cursor->dir != LV_DIR_NONE) return cursor->pos.y;
    }
    assert(!"range regression requires an attached visible cursor");
    return 0;
}

static void range_advance(uint32_t ms)
{
    vtick_advance(ms);
    lv_anim_refr_now();
}

static void verify_chart_range_continuity(void)
{
    bool saved_no_settle = s_no_settle;
    s_no_settle = true;
    lv_obj_set_style_bg_color(lv_screen_active(), uk_c(UK_BG), 0);
    lv_obj_t *caption = uk_label(lv_screen_active(), UK_FONT_CJK_12, UK_T1, "量程");
    lv_obj_set_pos(caption, 0, 420);
    lv_obj_t *chart = uk_trend_create(lv_screen_active(), 32, 1);
    lv_obj_set_size(chart, 800, 400);
    uk_trend_enable_flow(chart);
    lv_obj_add_event_cb(chart, range_announced, LV_EVENT_VALUE_CHANGED, NULL);
    uk_trend_set_range(chart, 0, 200);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, 40, 0);
    lv_obj_update_layout(chart);
    int32_t before = range_cursor_y(chart), ymin, ymax;
    s_range_announcements = 0;
    uk_trend_set_range(chart, 0, 100);
    assert(((lv_chart_t *)chart)->ymax[0] == 200 &&
           "shrinking the range must wait for a stable 2000 ms candidate");
    assert(s_range_announcements == 0);
    range_advance(500);
    uk_trend_set_range(chart, 0, 100); /* Repeated refresh does not restart the hold. */
    range_advance(1499);
    assert(range_cursor_y(chart) == before);
    uk_trend_get_range(chart, &ymin, &ymax);
    assert(ymin == 0 && ymax == 200 && s_range_announcements == 0);
    snapshot("chart-range-000-held");
    range_advance(1);
    uk_trend_get_range(chart, &ymin, &ymax);
    assert(ymin == 0 && ymax == 100 && s_range_announcements == 1);
    int32_t previous = before;
    unsigned moved = 0;
    for (unsigned t = 10; t <= 160; t += 10) {
        range_advance(10);
        int32_t y = range_cursor_y(chart);
        assert(y <= previous && y >= 240); /* 40 in 0..100 on a 400 px plot. */
        if (y != previous) moved++;
        previous = y;
        char frame[64]; snprintf(frame, sizeof frame, "chart-range-%03u-contract", t);
        snapshot(frame);
    }
    assert(previous == 240 && moved >= 6);
    assert(((lv_chart_t *)chart)->ymax[0] == 100);
    /* No fabricated samples or smoothing change accompanies range presentation. */
    int32_t *raw = lv_chart_get_series_y_array(chart, uk_trend_raw_series(chart, 0));
    for (unsigned i = 0; i < 32; i++) assert(raw[i] == 40);

    uk_trend_set_range(chart, -100, 300); /* A new peak is contained on this frame. */
    assert(((lv_chart_t *)chart)->ymin[0] == -100 && ((lv_chart_t *)chart)->ymax[0] == 300);
    before = range_cursor_y(chart);
    uk_trend_set_range(chart, -50, 200);
    range_advance(1500);
    uk_trend_set_range(chart, 0, 100); /* Changed candidate restarts its full hold. */
    range_advance(1999);
    assert(range_cursor_y(chart) == before);
    range_advance(81);
    previous = range_cursor_y(chart);
    assert(previous < before);
    uk_trend_set_range(chart, 10, 80); /* In-flight retarget starts at the current frame. */
    assert(range_cursor_y(chart) == previous);
    range_advance(2000);
    assert(range_cursor_y(chart) == previous);
    range_advance(20);
    int32_t mid = range_cursor_y(chart);
    assert(mid < previous);
    uk_trend_set_range(chart, -200, 500); /* Expansion cancels both delayed/active contraction. */
    assert(((lv_chart_t *)chart)->ymin[0] == -200 && ((lv_chart_t *)chart)->ymax[0] == 500);
    range_advance(2200);
    assert(((lv_chart_t *)chart)->ymin[0] == -200 && ((lv_chart_t *)chart)->ymax[0] == 500);
    snapshot("chart-range-300-expand-midway");

    /* A 2 -> 1 range exposes integer-only range animation: the visible line
       must have multiple intermediate heights while the native bound is still 2. */
    uk_trend_clear(chart);
    uk_trend_set_range(chart, 0, 2);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, 1, 0);
    before = range_cursor_y(chart);
    assert(before == 200);
    uk_trend_set_range(chart, 0, 1);
    range_advance(2000);
    moved = 0; previous = before;
    for (unsigned t = 10; t <= 160; t += 10) {
        range_advance(10);
        int32_t y = range_cursor_y(chart);
        assert(y <= previous && y >= 0);
        if (y != previous) moved++;
        previous = y;
    }
    assert(previous == 0 && moved >= 6 && "display projection must retain fractional range bounds");

    /* Semantic/hiding/settling paths finish even during the 2000 ms hold. */
    uk_trend_set_range(chart, 0, 200);
    uk_trend_set_range(chart, 0, 100);
    uk_trend_range_live(chart, false);
    assert(((lv_chart_t *)chart)->ymax[0] == 100);
    uk_trend_set_range(chart, 0, 50);
    assert(((lv_chart_t *)chart)->ymax[0] == 50);
    uk_trend_range_live(chart, true);
    uk_trend_set_range(chart, 0, 40); /* First current range after stale is immediate. */
    assert(((lv_chart_t *)chart)->ymax[0] == 40);
    uk_trend_set_range(chart, 0, 20);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
    range_advance(1);
    assert(((lv_chart_t *)chart)->ymax[0] == 20);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_HIDDEN);
    uk_trend_set_range(chart, 0, 10);
    uk_anim_settle(lv_screen_active());
    assert(((lv_chart_t *)chart)->ymax[0] == 10);
    uk_trend_set_range(chart, 0, 5);
    uk_motion_set_reduced(true);
    range_advance(1);
    assert(((lv_chart_t *)chart)->ymax[0] == 5);
    uk_trend_set_range(chart, 0, 2);
    assert(((lv_chart_t *)chart)->ymax[0] == 2);
    uk_motion_set_reduced(false);
    uk_trend_set_range(chart, 0, 1);
    uk_trend_clear(chart);
    assert(((lv_chart_t *)chart)->ymax[0] == 1);
    uk_trend_set_range(chart, -10, 10);
    assert(((lv_chart_t *)chart)->ymin[0] == -10 && ((lv_chart_t *)chart)->ymax[0] == 10);
    lv_obj_delete(chart); /* Deletes delayed and active callbacks with the chart. */
    lv_obj_delete(caption);
    s_no_settle = saved_no_settle;
    puts("checks: range expansion, 2000 ms stability, continuous critical contraction, retarget, precision, state/hidden/reduced/settle paths PASS");
}

static void verify_trend_runtime_reduced(void)
{
    lv_obj_t *chart = uk_trend_create(lv_screen_active(), 32, 1);
    lv_obj_set_size(chart, 800, 400);
    uk_trend_enable_flow(chart);
    for (int i = 0; i < 32; i++) uk_trend_push(chart, 0, 20, 0);
    lv_obj_update_layout(chart);
    uk_trend_push(chart, 0, 40, 200);
    range_advance(50);
    lv_chart_series_t *raw = uk_trend_raw_series(chart, 0);
    uint32_t at = (lv_chart_get_x_start_point(chart, raw) + 31) % 32;
    int32_t target = lv_chart_get_series_x_array(chart, raw)[at];
    assert(((lv_chart_t *)chart)->xmax[0] < target);
    uk_motion_set_reduced(true);
    range_advance(1);
    assert(((lv_chart_t *)chart)->xmax[0] == target);
    uk_trend_push(chart, 0, 60, 200);
    at = (lv_chart_get_x_start_point(chart, raw) + 31) % 32;
    target = lv_chart_get_series_x_array(chart, raw)[at];
    assert(((lv_chart_t *)chart)->xmax[0] == target);
    assert(lv_chart_get_series_y_array(chart, raw)[at] == 60);
    uk_motion_set_reduced(false);
    uk_trend_push(chart, 0, 80, 200);
    at = (lv_chart_get_x_start_point(chart, raw) + 31) % 32;
    target = lv_chart_get_series_x_array(chart, raw)[at];
    assert(((lv_chart_t *)chart)->xmax[0] < target);
    range_advance(200);
    assert(((lv_chart_t *)chart)->xmax[0] == target);
    lv_obj_delete(chart);
    puts("checks: runtime reduced settles active time windows, retains fresh data, restores subsequent motion PASS");
}
