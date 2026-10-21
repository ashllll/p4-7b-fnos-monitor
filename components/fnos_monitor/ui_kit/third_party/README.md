# Damped Springs — Ryan Juckett

Source: https://www.ryanjuckett.com/damped-springs/ (retrieved 2026-10-20).
Original copyright 2008–2012; zlib-style license retained in full in the header.
Extracted original two code blocks: SHA256 `6f1f3c6ebdd7a8d804f9273be8d2e4efb335410b6e7ac74467afa0b0022df392`.

`damped_spring.h` is an explicitly altered C99 port: typedef for the struct,
pointer in place of a reference, static inline linkage, math include and header guard.
Trailing whitespace from the original is normalized.
The equations, epsilon, branches and calculations are unchanged. The UI adapter
uses seconds and rad/s and converts its existing units/ms velocity at the seam.
No runtime download or external runtime dependency is required.

# d3-shape curveBasis — Mike Bostock

Pinned source: https://github.com/d3/d3-shape/blob/v3.2.0/src/curve/basis.js
(retrieved 2026-10-21). Original `basis.js` SHA-256:
`2b75b71399e8d92d021e28bb2b4f7d6c41e3d2dff89f69c877ea7f664698086b`.
Copyright 2010–2022 Mike Bostock; full ISC permission notice retained in
`d3-shape.LICENSE` (SHA-256
`faa682e3e430941f958d26180458f5934a62f58dac4d70ccdd15608c15d0f884`).

`d3_basis.h` is an altered C99 adaptation. Ring-buffer neighbours replace the
JavaScript streaming state; its cubic control-point weights and clamped ends
match the upstream implementation. Cubic evaluation is standard Bernstein
evaluation. No JavaScript runtime is required on the device. An independent
upstream differential covered 102 paths / 1308 cubics; maximum float-coordinate
error was 0.000960. Rendering is a separate LVGL native polyline adapter.
