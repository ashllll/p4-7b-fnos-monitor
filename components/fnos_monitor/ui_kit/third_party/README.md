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
