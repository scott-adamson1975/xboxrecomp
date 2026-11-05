/* overlay_wheel.h -- the racing wheel's pages in the settings overlay (the Wheel and Wheel test pages), declared for overlay_ui.cpp.
 * Everything is in overlay_wheel.inc and overlay_wheel_row.inc, textually included by overlay_ui.cpp, so that the wheel work stays
 * out of the shared file but for a handful of one-line hooks. The wheel's settings themselves are in xbox_settings_wheel_*.inc
 * and overlay_wheel_meta.inc. */
#ifndef XBOX_OVERLAY_WHEEL_H
#define XBOX_OVERLAY_WHEEL_H

struct Row;

static int  wheel_rows(int page, Row *r);                        /* the page's rows, below its tab; returns the count */
static void wheel_frame();                                       /* once a frame while the page list shows: devices, capture */
static bool wheel_capturing();                                   /* a "press now" capture is waiting */
static bool wheel_cancel_capture();                              /* B / Esc: ends it; true if there was one */
static void wheel_bind_activate(int idx);                        /* Enter / A / a tap on a binding row */
static void wheel_bind_clear(int idx);                           /* Backspace / Y */
static bool wheel_hint(const Row &r, char *hint, size_t n);      /* the hint of a binding or a live row; true: attention colour */
static void wheel_close();                                       /* the overlay is shut: the handles go */

#endif
