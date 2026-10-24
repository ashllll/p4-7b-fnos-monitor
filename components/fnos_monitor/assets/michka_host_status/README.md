# Michka host-status glyphs

This folder contains 18×18 LVGL 9 image descriptors for the Michka host-state
glyphs: a green Pac-Man for online and a red skull for offline. The vector path
geometry and viewBoxes come directly from upstream `app.css`; the offline body
uses the project's `UK_DANGER` red while the original dark facial details
remain.

## Source and changes

- Upstream: [Michka `app.css`, lines 715–716](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/src/Monitor/wwwroot/css/app.css#L715-L716).
- Host-state styling: lines 768–789 of that same file. Upstream theme colors
  are `--green: #36e07a` and `--red: #ff5470` (lines 12–13).
- Project adaptation: offline skull body uses `#ff6b6b`, matching the project's
  documented Michka palette adaptation in
  [`docs/ui-michka-slate-2026-10-22.md`](../../../../docs/ui-michka-slate-2026-10-22.md);
  the geometry and `#141414` face details remain unchanged.
- Checked source checkout: commit `3213285d6c22e786c0d4349ee3c0813cc98ab16d`;
  the `v1.6.0` tag is `4d4321983d67e19cf7ca2cf4155217aa8d984e6e`. The CSS file
  is byte-identical at the tag and checked commit. Its SHA-256 is
  `1c8b8bdf923fc95030b8008f19e8f1b22433255697a64326352c120924544427`.
- Original license: [upstream `LICENSE`](https://github.com/michaeldallariva/michka/blob/3213285d6c22e786c0d4349ee3c0813cc98ab16d/LICENSE),
  CC BY-NC 4.0. Attribution and the directory-only license notice are in
  [LICENSE](LICENSE). This does not change the root firmware license.

The Pac-Man path remains in `viewBox="0 0 100 100"`. The skull remains in
`viewBox="4.17 3.17 55.65 55.65"`. The source geometry is unchanged. Local
changes are the static green fill for the CSS mask, the skull body's danger-red
fill (`#ff6b6b`, replacing the upstream white fill and adapting its `--red`
palette), and 18px antialiased raster maps.
The SVG `width`/`height` of 360 only gives Quick Look enough source pixels for
rasterization; it does not change either `viewBox` or path coordinates.

## Generated image format

`michka_host_status.h` exports `michka_host_status_online` and
`michka_host_status_offline` as `const lv_image_dsc_t`. Both are 18×18,
`LV_COLOR_FORMAT_ARGB8888`, 72-byte stride, and 1,296 bytes of static pixel
data. Alpha-visible bounds (inclusive, coordinates from the top left) are:

- Online Pac-Man: `(0, 0)` through `(15, 17)`.
- Offline skull: `(2, 0)` through `(15, 16)`.

The SVGs contain no fonts, scripts, or external resources. Rasterization uses
the local macOS Quick Look SVG renderer at 576×576 (32× supersampling); the
included Python-standard-library generator removes the white thumbnail matte,
area-averages 32×32 blocks in premultiplied-alpha space, and writes the static
LVGL pixel maps. The generator is only for asset regeneration and is not a
firmware dependency.

Regenerate and run the built-in checks with:

```sh
python3 generate_assets.py
python3 generate_assets.py --check
```

The check validates map dimensions and stride, transparent/opaque alpha
coverage, visible bounds, and the expected green, red, and dark face colors.
