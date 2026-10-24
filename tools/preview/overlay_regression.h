/* Included by preview.c after its existing native-input and capture helpers. */
static void overlay_wait(unsigned ms)
{
    for (unsigned elapsed = 0; elapsed < ms; ) {
        unsigned dt = LV_MIN(8, ms - elapsed);
        vtick_advance(dt); lv_timer_handler(); elapsed += dt;
    }
}

/* Preserve the event's first frame: the ordinary click helper intentionally
   waits 500ms and would hide an instantaneous overlay behind a settled frame. */
static void overlay_pointer(lv_indev_t *pointer, lv_obj_t *object)
{
    assert(object && !effectively_hidden(object));
    lv_obj_scroll_to_view_recursive(object, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    assert(visible_click_area(object, &area));
    s_touch_point = (lv_point_t){(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    s_touch_down = true; lv_indev_read(pointer);
    vtick_advance(1);
    s_touch_down = false; lv_indev_read(pointer);
    lv_obj_update_layout(lv_screen_active());
}

static void overlay_text(lv_indev_t *pointer, const char *text)
{
    lv_obj_t *label = visible_text_clickable(lv_screen_active(), text);
    assert(label);
    overlay_pointer(pointer, lv_obj_get_parent(label));
}

static lv_obj_t *overlay_surface(const char *heading, bool detail)
{
    lv_obj_t *object = visible_text(lv_screen_active(), heading);
    assert(object);
    if (detail) {
        /* The modal mask is the screen's direct child; its card inherits alpha. */
        while (lv_obj_get_parent(object) != lv_screen_active()) object = lv_obj_get_parent(object);
    } else {
        while (!lv_obj_has_flag(lv_obj_get_parent(object), LV_OBJ_FLAG_USER_1))
            object = lv_obj_get_parent(object);
    }
    return object;
}

static FILE *s_overlay_timeline;
static uint32_t s_overlay_started;
static void overlay_record_start(void)
{
    char path[512]; snprintf(path, sizeof path, "%s/motion.csv", s_outdir);
    s_overlay_timeline = fopen(path, "w");
    assert(s_overlay_timeline);
    fputs("file,time_ms,page,phase\n", s_overlay_timeline);
    s_overlay_started = vtick_get();
}
static void overlay_record_end(void)
{
    evidence_close(s_overlay_timeline, "overlay motion.csv");
    s_overlay_timeline = NULL;
}

static void overlay_frame(const char *name, bool terminal)
{
    s_motion_frame = !terminal;
    snapshot(name);
    s_motion_frame = false;
    if (s_overlay_timeline) fprintf(s_overlay_timeline, "%s.ppm,%u,%d,%s\n", name,
        (unsigned)(vtick_get() - s_overlay_started), fnos_ui_page(), name);
}

static void overlay_assert_opening(lv_obj_t *surface, const char *name)
{
    assert(!lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_opa(surface, 0) < LV_OPA_COVER &&
           "opening must start with a real fade, not an instantaneous show");
    char frame[80]; snprintf(frame, sizeof frame, "overlay-%s-open", name);
    overlay_frame(frame, false);
    overlay_wait(48);
    lv_opa_t alpha = lv_obj_get_style_opa(surface, 0);
    assert(alpha > LV_OPA_TRANSP && alpha < LV_OPA_COVER);
    snprintf(frame, sizeof frame, "overlay-%s-open-mid", name);
    overlay_frame(frame, false);
    overlay_wait(180);
    assert(lv_obj_get_style_opa(surface, 0) == LV_OPA_COVER);
    snprintf(frame, sizeof frame, "overlay-%s-open-stable", name);
    overlay_frame(frame, true);
}

static void overlay_assert_closing(lv_obj_t *surface, const char *name)
{
    assert(!lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN) &&
           "closing must retain the surface until its fade completes");
    overlay_wait(48);
    lv_opa_t alpha = lv_obj_get_style_opa(surface, 0);
    assert(alpha > LV_OPA_TRANSP && alpha < LV_OPA_COVER);
    char frame[80]; snprintf(frame, sizeof frame, "overlay-%s-close-mid", name);
    overlay_frame(frame, false);
    overlay_wait(180);
    assert(lv_obj_has_flag(surface, LV_OBJ_FLAG_HIDDEN));
    snprintf(frame, sizeof frame, "overlay-%s-close-stable", name);
    overlay_frame(frame, true);
}

static void overlay_check_nav_opaque(lv_obj_t *object)
{
    if (!effectively_hidden(object) && lv_obj_get_style_line_width(object, 0) > 0)
        assert(lv_obj_get_style_opa(object, 0) == LV_OPA_COVER &&
               "reduced-motion navigation icons must be fully opaque immediately");
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
        overlay_check_nav_opaque(lv_obj_get_child(object, i));
}

/* All four remaining product plots use the real acquisition-to-UI path. */
static unsigned overlay_visible_charts(lv_obj_t *object, lv_obj_t **charts, unsigned count)
{
    if (effectively_hidden(object)) return count;
    if (lv_obj_has_class(object, &lv_chart_class)) {
        assert(count < 4); charts[count++] = object;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
        count = overlay_visible_charts(lv_obj_get_child(object, i), charts, count);
    return count;
}

static void overlay_stream_probe(bool reduced)
{
    static const int pages[] = {0, 2, 4};
    static const unsigned expected[] = {3, 1, 0};
    unsigned checked = 0;
    s_override_sample.interval_ms = 200;
    s_override_sample.temp_max = 51;
    for (unsigned page = 0; page < 3; page++) {
        fnos_ui_set_page(pages[page]); overlay_wait(800);
        lv_obj_t *charts[4];
        unsigned count = overlay_visible_charts(lv_screen_active(), charts, 0);
        assert(count == expected[page]);
        uint32_t last[4];
        for (unsigned i = 0; i < count; i++) {
            uk_trend_settle(charts[i]);
            lv_chart_series_t *series = uk_trend_series(charts[i], 0);
            uint32_t points = lv_chart_get_point_count(charts[i]);
            /* Track the former head, now an interior visible point. Native point
               lookup clamps the newly arrived future head at the right edge. */
            last[i] = (lv_chart_get_x_start_point(charts[i], series) + points - 1) % points;
        }
        s_override_sample.cpu += 1; s_override_sample.mem += 1;
        s_override_sample.rx_kbs += 8; s_override_sample.tx_kbs += 8;
        s_override_sample.temp_max += 1;
        s_seq += 2;
        vtick_advance(500); lv_timer_handler();
        lv_point_t before[4];
        for (unsigned i = 0; i < count; i++) {
            lv_chart_series_t *series = uk_trend_series(charts[i], 0);
            uint32_t points = lv_chart_get_point_count(charts[i]);
            uint32_t newest = (lv_chart_get_x_start_point(charts[i], series) + points - 1) % points;
            lv_chart_get_point_pos_by_id(charts[i], series, last[i], &before[i]);
            const int32_t *raw = lv_chart_get_series_y_array(charts[i], uk_trend_raw_series(charts[i], 0));
            assert(raw && raw[newest] != LV_CHART_POINT_NONE && "latest real sample must still publish");
        }
        char frame[80];
        snprintf(frame, sizeof frame, "runtime-%s-p%d-before", reduced ? "reduced" : "full", pages[page]);
        overlay_frame(frame, false);
        /* Animation-only time cannot inject a second collector sample. */
        vtick_advance(240); lv_anim_refr_now();
        for (unsigned i = 0; i < count; i++) {
            lv_point_t after;
            lv_chart_get_point_pos_by_id(charts[i], uk_trend_series(charts[i], 0), last[i], &after);
            printf("runtime plot: reduced=%d page=%d chart=%u before=%d after=%d width=%d\n", reduced, pages[page], i, before[i].x, after.x, lv_obj_get_width(charts[i]));
            if (reduced) assert(after.x == before[i].x && "runtime reduced plots must not keep translating");
            else assert(after.x < before[i].x && "restored plots must advance on subsequent real samples");
            checked++;
        }
        snprintf(frame, sizeof frame, "runtime-%s-p%d-after", reduced ? "reduced" : "full", pages[page]);
        overlay_frame(frame, false);
    }
    assert(checked == 4);
}

static void verify_runtime_reduced_motion(lv_indev_t *pointer)
{
    overlay_record_start();
    assert(!UK_NUMBER_REDUCED);
    s_no_settle = true;
    fnos_ui_set_page(3); overlay_wait(800);
    overlay_text(pointer, "设置"); overlay_wait(180);
    overlay_text(pointer, "完整");
    assert(uk_motion_reduced());
    overlay_text(pointer, "返回");
    fnos_ui_set_page(1);
    overlay_check_nav_opaque(lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "存储")));
    overlay_stream_probe(true);
    fnos_ui_set_page(3);
    overlay_text(pointer, "设置"); overlay_wait(180);
    overlay_text(pointer, "精简");
    assert(!uk_motion_reduced());
    overlay_text(pointer, "返回"); overlay_wait(180);
    overlay_stream_probe(false);
    fnos_ui_motion_settle(); uk_anim_settle(lv_screen_active());
    s_no_settle = false;
    overlay_record_end();
    puts("checks: real diagnostic reduced toggle, immediate navigation and four product streams/restore PASS");
}

static void verify_overlay_motion(lv_indev_t *pointer)
{
    overlay_record_start();
    assert(!UK_NUMBER_REDUCED && "overlay-motion probe needs animation-enabled build");
    s_no_settle = true;
    fnos_ui_set_page(1); overlay_wait(800);
    lv_obj_t *volume = visible_text(lv_screen_active(), s_motion_fixture.vols[0].mnt);
    assert(volume);
    overlay_pointer(pointer, lv_obj_get_parent(volume));
    lv_obj_t *detail = overlay_surface("实体详情", true);
    overlay_assert_opening(detail, "detail");
    int page = fnos_ui_page();
    overlay_text(pointer, "返回列表");
    /* The still-visible detail mask consumes a navigation hit during exit. */
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == page && "closing mask must keep background input blocked");
    overlay_assert_closing(detail, "detail");

    fnos_ui_set_page(3); overlay_wait(800);
    overlay_text(pointer, "设置");
    lv_obj_t *diagnostics = overlay_surface("设置与诊断", false);
    overlay_assert_opening(diagnostics, "diagnostics");
    /* Rapid reversal follows current opacity, without jumping to a fixed start. */
    overlay_text(pointer, "返回");
    overlay_wait(40);
    lv_opa_t before = lv_obj_get_style_opa(diagnostics, 0);
    assert(before > 0 && before < LV_OPA_COVER);
    overlay_text(pointer, "设置");
    lv_opa_t after = lv_obj_get_style_opa(diagnostics, 0);
    assert(abs((int)after - (int)before) <= 2 && "reverse must preserve current opacity");
    overlay_frame("overlay-diagnostics-reverse", false);
    overlay_wait(180);
    assert(lv_obj_get_style_opa(diagnostics, 0) == LV_OPA_COVER);
    overlay_text(pointer, "返回");
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == 3 && "diagnostic exit must keep background input blocked");
    overlay_assert_closing(diagnostics, "diagnostics");

    overlay_text(pointer, "配对");
    lv_obj_t *pair = overlay_surface("与 NAS 配对", false);
    overlay_assert_opening(pair, "pair");
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == 3);
    assert(visible_text(lv_screen_active(), "与 NAS 配对"));
    overlay_text(pointer, "关闭");
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == 3 && "exit fade must retain navigation ownership");
    overlay_assert_closing(pair, "pair");

    overlay_text(pointer, "PREVIEW-LAN");
    lv_obj_t *wifi = overlay_surface("接入 Wi-Fi", false);
    overlay_assert_opening(wifi, "wifi");
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == 3);
    assert(visible_text(lv_screen_active(), "接入 Wi-Fi"));
    overlay_text(pointer, "关闭");
    overlay_text(pointer, "网络");
    assert(fnos_ui_page() == 3 && "exit fade must retain navigation ownership");
    overlay_assert_closing(wifi, "wifi");

    /* The real diagnostic row is the only writer used to toggle runtime mode. */
    overlay_text(pointer, "设置"); overlay_wait(180);
    overlay_text(pointer, "完整");
    assert(uk_motion_reduced());
    overlay_text(pointer, "返回");
    assert(lv_obj_has_flag(diagnostics, LV_OBJ_FLAG_HIDDEN));
    overlay_text(pointer, "配对");
    assert(lv_obj_get_style_opa(pair, 0) == LV_OPA_COVER);
    overlay_text(pointer, "关闭");
    assert(lv_obj_has_flag(pair, LV_OBJ_FLAG_HIDDEN));
    overlay_text(pointer, "PREVIEW-LAN");
    assert(lv_obj_get_style_opa(wifi, 0) == LV_OPA_COVER);
    overlay_text(pointer, "关闭");
    assert(lv_obj_has_flag(wifi, LV_OBJ_FLAG_HIDDEN));
    fnos_ui_set_page(1);
    overlay_check_nav_opaque(lv_obj_get_parent(visible_text_clickable(lv_screen_active(), "存储")));
    volume = visible_text(lv_screen_active(), s_motion_fixture.vols[0].mnt);
    assert(volume); overlay_pointer(pointer, lv_obj_get_parent(volume));
    assert(lv_obj_get_style_opa(detail, 0) == LV_OPA_COVER);
    overlay_text(pointer, "返回列表");
    assert(lv_obj_has_flag(detail, LV_OBJ_FLAG_HIDDEN));
    overlay_frame("overlay-reduced-stable", true);
    fnos_ui_set_page(3);
    overlay_text(pointer, "设置");
    overlay_text(pointer, "精简");
    assert(!uk_motion_reduced());
    overlay_text(pointer, "返回"); overlay_wait(180);
    overlay_text(pointer, "配对");
    overlay_assert_opening(pair, "restored-pair");
    overlay_text(pointer, "关闭"); overlay_wait(180);
    fnos_ui_motion_settle(); uk_anim_settle(lv_screen_active());
    s_no_settle = false;
    overlay_record_end();
    puts("checks: native overlay fades, reverse continuity, modal exit input, runtime reduced and restore PASS");
}

/* Separate process: the startup deadline uses the existing frozen board clock. */
static void verify_boot_wifi_motion(void)
{
    overlay_record_start();
    s_no_settle = true;
    wifi_stub(false, false, "", "");
    lv_obj_t *title = NULL;
    for (unsigned elapsed = 0; elapsed < 5000 && !title; elapsed += 8) {
        s_motion_clock_us += 8000;
        overlay_wait(8);
        title = visible_text(lv_screen_active(), "接入 Wi-Fi");
    }
    assert(title && "first boot without credentials must open Wi-Fi setup");
    overlay_assert_opening(overlay_surface("接入 Wi-Fi", false), "boot-wifi");
    fnos_ui_motion_settle();
    s_no_settle = false;
    overlay_record_end();
    puts("checks: first-boot automatic Wi-Fi setup follows the same fade path PASS");
}

/* Peak enters/leaves the real network history, then the actual UI declares range. */
static void verify_product_range_motion(void)
{
    overlay_record_start();
    s_no_settle = true;
    s_motion_fixture.net.rx_kbs = s_motion_fixture.net.tx_kbs = 10;
    s_override_sample.rx_kbs = s_override_sample.tx_kbs = 10;
    s_override_sample.interval_ms = 500;
    fnos_ui_set_page(2); overlay_wait(1000);
    lv_obj_t *title = visible_text_containing(lv_screen_active(), "网络吞吐 ·");
    assert(title);
    lv_obj_t *chart = find_widget(lv_obj_get_parent(lv_obj_get_parent(title)), &lv_chart_class);
    assert(chart);
    s_motion_fixture.net.rx_kbs = s_override_sample.rx_kbs = 100000;
    s_seq++;
    vtick_advance(500); lv_timer_handler();
    int32_t low, expanded, high;
    uk_trend_get_range(chart, &low, &expanded);
    assert(expanded >= 120000 && "real peak must expand the product plot on this frame");
    overlay_frame("range-product-expand", false);
    s_motion_fixture.net.rx_kbs = s_motion_fixture.net.tx_kbs = 10;
    s_override_sample.rx_kbs = s_override_sample.tx_kbs = 10;
    s_seq += CONFIG_FNOS_CHART_WINDOW;
    bool expired = false;
    for (unsigned batch = 0; batch < (CONFIG_FNOS_CHART_WINDOW + 63) / 64 + 2; batch++) {
        vtick_advance(500); lv_timer_handler();
        const int32_t *raw = lv_chart_get_series_y_array(chart, uk_trend_raw_series(chart, 1));
        expired = true;
        for (unsigned i = 0; i < lv_chart_get_point_count(chart); i++)
            if (raw[i] != LV_CHART_POINT_NONE && raw[i] > 10) expired = false;
        if (expired) break;
    }
    assert(expired && "the normal 64-sample reader must consume the complete window");
    uk_trend_get_range(chart, &low, &high);
    assert(high == expanded && "peak leaving the actual history must not snap the range");
    lv_obj_t *note = visible_text_containing(lv_screen_active(), "量程 0");
    assert(note);
    char held[128]; snprintf(held, sizeof held, "%s", lv_label_get_text(note));
    overlay_frame("range-product-held", false);
    vtick_advance(1999); lv_anim_refr_now();
    uk_trend_get_range(chart, &low, &high);
    assert(high == expanded && !strcmp(held, lv_label_get_text(note)));
    vtick_advance(1); lv_anim_refr_now();
    uk_trend_get_range(chart, &low, &high);
    assert(high == 20 && strcmp(held, lv_label_get_text(note)));
    overlay_frame("range-product-start", false);
    vtick_advance(48); lv_anim_refr_now();
    assert(((lv_chart_t *)chart)->ymax[0] > 20);
    overlay_frame("range-product-mid", false);
    vtick_advance(112); lv_anim_refr_now();
    assert(((lv_chart_t *)chart)->ymax[0] == 20);
    overlay_frame("range-product-stable", true);
    overlay_record_end();
    s_no_settle = false;
    puts("checks: real product peak entry/expiry, delayed range and static target note timing PASS");
}
