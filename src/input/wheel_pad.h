/**
 * xbox_input wheel -- a racing wheel and pedals as a port-0 pad.
 *
 * A wheel base and its pedals are plain HID joysticks, not game controllers,
 * so the pad backends never open them. This is the part that does not need a
 * device: the specs that bind an axis of a joystick to a control, and the
 * arithmetic from a 16-bit axis to the Xbox pad's left stick and triggers.
 * wheel_sdl.c is the SDL glue that opens the joysticks and reads the axes;
 * input_core.c merges the result into port 0 like the keyboard and the
 * on-screen controls (a trigger the larger, the stick set by the wheel while
 * it is off centre).
 *
 * Header-only (static inline), no SDL and no globals, so tests/input_wheel
 * runs on every host with no device.
 *
 * A control is bound by text, "VVVV:PPPP:N[!]": the joystick's USB vendor
 * and product ids in hex, then the axis index (decimal, SDL's order), then
 * an optional "!" for an axis that reads high at rest (a pedal wired so that
 * 65535 is released). The wheel and the pedals may be one device or two.
 *
 *   steering   left stick X: the axis is the wheel's whole travel, of which
 *              lock/range reaches full deflection (range: the degrees the
 *              base turns lock to lock, lock: the degrees the game should
 *              treat as full lock)
 *   throttle   right trigger
 *   brake      left trigger
 */
#ifndef XBOX_INPUT_WHEEL_PAD_H
#define XBOX_INPUT_WHEEL_PAD_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xinput_xbox.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where a control comes from. dev is the joystick's vendor and product ids
 * packed (vid << 16 | pid); axis < 0 means the control is not bound. */
typedef struct xbox_wheel_bind {
    uint32_t dev;
    int      axis;
    int      invert;
} xbox_wheel_bind;

/* The wheel's buttons, each the Xbox button the title acts on: ACCEPT is A
 * (confirms in menus; boost, and impact time in a crash), BACK is B (back;
 * the crashbreaker), PAUSE is Start, LOOKBACK is X, VIEW is Y. For these the
 * bind's axis field is the joystick's button index. */
enum { XBOX_WHEEL_BTN_ACCEPT, XBOX_WHEEL_BTN_BACK, XBOX_WHEEL_BTN_PAUSE,
       XBOX_WHEEL_BTN_LOOKBACK, XBOX_WHEEL_BTN_VIEW, XBOX_WHEEL_NBTN };

/* A hat's directions as SDL reports them (SDL_HAT_*), a bitmask. */
#define XBOX_WHEEL_HAT_UP    1
#define XBOX_WHEEL_HAT_RIGHT 2
#define XBOX_WHEEL_HAT_DOWN  4
#define XBOX_WHEEL_HAT_LEFT  8

/* The hat's left and right set the left stick's X only while the wheel's own
 * steering is inside this (of 32767: about a quarter of full lock). */
#define XBOX_WHEEL_HAT_STEER_TOL 8000

typedef struct xbox_wheel_cfg {
    xbox_wheel_bind steer, throttle, brake;
    xbox_wheel_bind btn[XBOX_WHEEL_NBTN];  /* axis: the button index */
    xbox_wheel_bind hat;                   /* axis: the hat index */
    double range_deg;    /* the base's lock to lock, degrees (default 900) */
    double lock_deg;     /* the lock the game gets, degrees (default 540) */
    double pedal_dead;   /* pedal deadzone, percent of travel (default 4) */
    double steer_dead;   /* steering deadzone, percent of the stick's travel, after the lock (default 0) */
    double steer_skip;   /* the band the title ignores around the stick's centre, percent: the wheel jumps over it (default 0) */
} xbox_wheel_cfg;

#define XBOX_WHEEL_RANGE_DEFAULT 900.0
#define XBOX_WHEEL_LOCK_DEFAULT  540.0
#define XBOX_WHEEL_PEDAL_DEAD_DEFAULT 4.0
#define XBOX_WHEEL_CENTRE_TOL 0.0015   /* of the half travel: about 50 of 32768 counts */

static inline void xbox_wheel_cfg_defaults(xbox_wheel_cfg *c)
{
    memset(c, 0, sizeof *c);
    c->steer.axis = c->throttle.axis = c->brake.axis = c->hat.axis = -1;
    for (int i = 0; i < XBOX_WHEEL_NBTN; i++)
        c->btn[i].axis = -1;
    c->range_deg = XBOX_WHEEL_RANGE_DEFAULT;
    c->lock_deg = XBOX_WHEEL_LOCK_DEFAULT;
    c->pedal_dead = XBOX_WHEEL_PEDAL_DEAD_DEFAULT;
}

/* "044F:B696:0" or "044F:B6FC:2!". Returns 1 and fills *b, or 0 and leaves
 * it unbound. NULL or empty is "not bound" (returns 1). */
static inline int xbox_wheel_bind_parse(xbox_wheel_bind *b, const char *s)
{
    char *e;
    unsigned long vid, pid, axis;

    b->dev = 0;
    b->axis = -1;
    b->invert = 0;
    if (!s || !*s)
        return 1;
    vid = strtoul(s, &e, 16);
    if (e == s || *e != ':' || vid > 0xFFFFu)
        return 0;
    s = e + 1;
    pid = strtoul(s, &e, 16);
    if (e == s || *e != ':' || pid > 0xFFFFu)
        return 0;
    s = e + 1;
    axis = strtoul(s, &e, 10);
    if (e == s || axis > 63u)
        return 0;
    if (*e == '!') {
        b->invert = 1;
        e++;
    }
    if (*e)
        return 0;
    b->dev = (uint32_t)(vid << 16 | pid);
    b->axis = (int)axis;
    return 1;
}

static inline double xbox_wheel_clamp(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* An SDL joystick axis (-32768..32767) as the 0..65535 the hardware reports. */
static inline uint32_t xbox_wheel_axis_u16(int16_t v)
{
    return (uint32_t)((int)v + 32768);
}

/* The steering axis (0..65535, centre 32767.5) as the left stick's X.
 * range and lock are degrees: a base that turns 900 degrees lock to lock
 * with a lock of 540 reaches full deflection at 270 degrees each way. A lock
 * wider than the range is the range. The deadzone is a percentage of the
 * travel either side of the centre, the rest stretched to full (steer_dead,
 * of the stick's travel after the lock; steer_skip then lifts what is left
 * above the title's own dead band). */
static inline int16_t xbox_wheel_steer(uint32_t raw, const xbox_wheel_cfg *c, int invert)
{
    double n = ((double)raw - 32767.5) / 32767.5;
    double gain = (c->lock_deg > 0 && c->range_deg > c->lock_deg) ? c->range_deg / c->lock_deg : 1.0;
    double d = xbox_wheel_clamp(c->steer_dead, 0, 95) / 100.0;
    double m;

    /* A wheel at rest reads 32767 or 32768: that is the centre, not a push
     * of one count (about 0.15 degrees of a 900 degree base is tolerated). */
    if (n > -XBOX_WHEEL_CENTRE_TOL && n < XBOX_WHEEL_CENTRE_TOL)
        return 0;
    if (invert)
        n = -n;
    n = xbox_wheel_clamp(n * gain, -1.0, 1.0);
    m = n < 0 ? -n : n;
    if (m <= d)
        return 0;
    m = (m - d) / (1.0 - d);
    if (c->steer_skip > 0) {
        /* The title ignores a band around the stick's centre (a pad stick has
         * play): the first degrees of a wheel would do nothing. Start just
         * outside it, so the first degree already steers. The inner deadzone
         * above is what keeps a wheel that rests a little off centre from
         * creeping. */
        double s = xbox_wheel_clamp(c->steer_skip, 0, 50) / 100.0;

        m = s + (1.0 - s) * m;
    }
    n = n < 0 ? -m : m;
    return (int16_t)(n * 32767.0 + (n < 0 ? -0.5 : 0.5));
}

/* A pedal axis (0..65535) as a trigger, 0..255. Rest is 0 unless invert.
 * Inside the deadzone it reads 0; beyond it the travel is stretched so full
 * stays 255. */
static inline uint8_t xbox_wheel_pedal(uint32_t raw, const xbox_wheel_cfg *c, int invert)
{
    double v = (double)raw / 65535.0;
    double d = xbox_wheel_clamp(c->pedal_dead, 0, 95) / 100.0;

    if (invert)
        v = 1.0 - v;
    v = xbox_wheel_clamp(v, 0.0, 1.0);
    if (v <= d)
        return 0;
    v = (v - d) / (1.0 - d);
    return (uint8_t)(v * 255.0 + 0.5);
}

/* Pedals that report nothing. A pedal box that has not started (unpowered load
 * cells, not calibrated, a bad plug) can sit with every axis at the middle of
 * its range, exactly: 32767 or 32768, the same on both pedals. A live load cell
 * is never that still at both, and a foot never puts two pedals at the exact
 * middle at once. Read as they are those are two half-pressed pedals (throttle
 * and brake together, the brake winning), so both are taken as released until
 * either moves. Both axes must be bound and read (>= 0). */
static inline int xbox_wheel_pedals_idle(int64_t throttle, int64_t brake)
{
    return (throttle == 32767 || throttle == 32768) && (brake == 32767 || brake == 32768);
}

/* The Xbox pad a wheel makes from three raw axes (0..65535 each); an unbound
 * control is passed as <0 and reads as centred / released. */
static inline void xbox_wheel_resolve(const xbox_wheel_cfg *c, int64_t steer,
                                      int64_t throttle, int64_t brake, XBOX_GAMEPAD *out)
{
    memset(out, 0, sizeof *out);
    if (steer >= 0)
        out->sThumbLX = xbox_wheel_steer((uint32_t)steer, c, c->steer.invert);
    if (throttle >= 0)
        out->bAnalogButtons[XBOX_BUTTON_RTRIGGER] =
            xbox_wheel_pedal((uint32_t)throttle, c, c->throttle.invert);
    if (brake >= 0)
        out->bAnalogButtons[XBOX_BUTTON_LTRIGGER] =
            xbox_wheel_pedal((uint32_t)brake, c, c->brake.invert);
}

/* The wheel's buttons and hat onto the pad xbox_wheel_resolve made. held[i]
 * is whether button i (XBOX_WHEEL_BTN_*) is down, hat the SDL hat mask (0
 * centred, or when the hat is not bound).
 *
 * The hat is the D-pad (menus) and also the left stick (a crash's aftertouch
 * is steered with the left stick, and the D-pad does nothing there): up and
 * down are the stick's Y, which driving does not use, and left and right its
 * X only while the wheel's own steering is centred, so the hat never fights
 * the wheel. The keyboard's arrows are both too (docs/controls.md). */
static inline void xbox_wheel_apply_buttons(const uint8_t held[XBOX_WHEEL_NBTN], int hat,
                                            XBOX_GAMEPAD *out)
{
    if (held[XBOX_WHEEL_BTN_ACCEPT])   out->bAnalogButtons[XBOX_BUTTON_A] = 255;
    if (held[XBOX_WHEEL_BTN_BACK])     out->bAnalogButtons[XBOX_BUTTON_B] = 255;
    if (held[XBOX_WHEEL_BTN_LOOKBACK]) out->bAnalogButtons[XBOX_BUTTON_X] = 255;
    if (held[XBOX_WHEEL_BTN_VIEW])     out->bAnalogButtons[XBOX_BUTTON_Y] = 255;
    if (held[XBOX_WHEEL_BTN_PAUSE])    out->wButtons |= XBOX_GAMEPAD_START;
    if (hat & XBOX_WHEEL_HAT_UP) {
        out->wButtons |= XBOX_GAMEPAD_DPAD_UP;
        out->sThumbLY = 32767;
    }
    if (hat & XBOX_WHEEL_HAT_DOWN) {
        out->wButtons |= XBOX_GAMEPAD_DPAD_DOWN;
        out->sThumbLY = (int16_t)(hat & XBOX_WHEEL_HAT_UP ? 0 : -32767);
    }
    /* Left and right: the wheel wins once it is turned past the tolerance. A
     * wheel held "straight" still reads a little off (a T818 rests around 1.6%
     * of full), so the test is not for exactly zero. */
    if (hat & XBOX_WHEEL_HAT_LEFT) {
        out->wButtons |= XBOX_GAMEPAD_DPAD_LEFT;
        if (out->sThumbLX > -XBOX_WHEEL_HAT_STEER_TOL && out->sThumbLX < XBOX_WHEEL_HAT_STEER_TOL)
            out->sThumbLX = -32767;
    }
    if (hat & XBOX_WHEEL_HAT_RIGHT) {
        out->wButtons |= XBOX_GAMEPAD_DPAD_RIGHT;
        if (out->sThumbLX > -XBOX_WHEEL_HAT_STEER_TOL && out->sThumbLX < XBOX_WHEEL_HAT_STEER_TOL)
            out->sThumbLX = 32767;
    }
}

/* Whether the wheel is the device in use: steering off centre, a pedal down,
 * or a button or the hat held. A resting wheel must not flip the title's
 * layout choice. */
static inline int xbox_wheel_active(const XBOX_GAMEPAD *g)
{
    int i;

    for (i = 0; i < 8; i++)
        if (g->bAnalogButtons[i])
            return 1;
    return g->sThumbLX != 0 || g->sThumbLY != 0 || g->wButtons != 0;
}

/* Merge the wheel onto a pad: a button held if either holds it, a trigger or
 * face button the larger of the two, a stick axis set by the wheel while it
 * is off centre (the pad's value stays otherwise). */
static inline void xbox_wheel_merge(XBOX_GAMEPAD *pad, const XBOX_GAMEPAD *w)
{
    int i;

    pad->wButtons |= w->wButtons;
    for (i = 0; i < 8; i++)
        if (w->bAnalogButtons[i] > pad->bAnalogButtons[i])
            pad->bAnalogButtons[i] = w->bAnalogButtons[i];
    if (w->sThumbLX)
        pad->sThumbLX = w->sThumbLX;
    if (w->sThumbLY)
        pad->sThumbLY = w->sThumbLY;
}

/* ---- force feedback: the centring spring --------------------------------
 *
 * A spring effect the base runs itself (no latency from this program) pulling
 * the wheel to the centre. The strength is the force at the software lock's
 * edge as a percentage of the base's maximum; beyond the edge it holds that
 * force, it does not grow, so the spring is never a wall. It is passive (it
 * only resists), capped, and faded in and out, because the base is a 10 Nm
 * direct drive.
 *
 * A condition effect's force is coefficient * (offset from the centre as a
 * fraction of the base's whole range), up to the saturation. So reaching the
 * strength at the lock's edge (lock/2 from the centre, against range/2 for the
 * whole travel) takes a coefficient range/lock times the strength. */
#define XBOX_WHEEL_SPRING_MAX     50.0     /* the strength is never above this, percent */
#define XBOX_WHEEL_RAMP_UP_MS   1500.0     /* from no force to the full strength */
#define XBOX_WHEEL_RAMP_DOWN_MS  150.0     /* and back */
#define XBOX_WHEEL_RAMP_STEP_MS   50.0     /* one step of a fade-in is never longer: a stall must not skip it */

typedef struct xbox_wheel_spring {
    int16_t  coeff;     /* SDL_HapticCondition coefficient, 0..32767 */
    uint16_t sat;       /* SDL_HapticCondition saturation, 0..65535 */
} xbox_wheel_spring;

static inline xbox_wheel_spring xbox_wheel_spring_params(const xbox_wheel_cfg *c, double strength_pct)
{
    xbox_wheel_spring r;
    double s = xbox_wheel_clamp(strength_pct, 0, XBOX_WHEEL_SPRING_MAX) / 100.0;
    double lock = (c->lock_deg > 0 && c->lock_deg < c->range_deg) ? c->lock_deg : c->range_deg;
    double k = lock > 0 ? c->range_deg / lock : 1.0;

    r.coeff = (int16_t)(xbox_wheel_clamp(s * k, 0.0, 1.0) * 32767.0 + 0.5);
    r.sat = (uint16_t)(s * 65535.0 + 0.5);
    return r;
}

/* The force gain (0 none, 1 full) dt_ms on: rising to full over RAMP_UP_MS
 * while on, falling to none over RAMP_DOWN_MS while off. */
static inline double xbox_wheel_ffb_gain_step(double gain, int on, double dt_ms)
{
    if (dt_ms < 0)
        dt_ms = 0;
    if (on) {
        if (dt_ms > XBOX_WHEEL_RAMP_STEP_MS)
            dt_ms = XBOX_WHEEL_RAMP_STEP_MS;
        gain += dt_ms / XBOX_WHEEL_RAMP_UP_MS;
    } else {
        gain -= dt_ms / XBOX_WHEEL_RAMP_DOWN_MS;
    }
    return xbox_wheel_clamp(gain, 0.0, 1.0);
}

/* ---- the spring's level: speed, and whether a race is running ------------
 *
 * The host may feed the glue the car's state (xbox_InputWheelSetCarSource): a
 * callback that says whether a race is running and the car's speed. Two
 * opt-in settings use it:
 *   speed_ref  m/s at which the spring has its whole strength; below it the
 *              spring is weaker, down to `rest` (a fraction of the strength)
 *              at a standstill. 0: the strength does not follow the speed.
 *   race_only  outside a race the spring is off (menus, pauses, results), not
 *              what it is in a race.
 * With no feed set, or neither setting, the level is 1: the spring as it was.
 * The level multiplies the strength before the base's coefficient is worked
 * out (xbox_wheel_spring_params), so the cap on the strength still holds, and it
 * is slewed, so a speed step or a pause is never a step in the force. */
#define XBOX_WHEEL_LEVEL_UP_MS    1000.0   /* from 0 to 1 */
#define XBOX_WHEEL_LEVEL_DOWN_MS   400.0   /* from 1 to 0 */
#define XBOX_WHEEL_LEVEL_STEP_MS    50.0   /* a step is never longer: a stall must not skip the slew */
#define XBOX_WHEEL_LEVEL_MIN_COEFF   330   /* the effect is rewritten only for a change this big (about 1%), or at 0 or 1 */

typedef struct xbox_wheel_level_cfg {
    double speed_ref;       /* m/s; 0 = the speed does not matter */
    double rest;            /* 0..1, the share of the strength at a standstill */
    int    race_only;
} xbox_wheel_level_cfg;

/* The host's car feed: returns 1 while a race is running (the speed is then
 * set, m/s, a magnitude), 0 otherwise. Called from the thread that polls the pad. */
typedef int (*xbox_wheel_car_fn)(double *speed);

static inline double xbox_wheel_level_target(const xbox_wheel_level_cfg *c, int have_feed,
                                             int racing, double speed)
{
    double f, rest;

    if (!have_feed)
        return 1.0;
    if (!racing)
        return c->race_only ? 0.0 : 1.0;
    if (c->speed_ref > 0) {
        f = xbox_wheel_clamp((speed < 0 ? -speed : speed) / c->speed_ref, 0.0, 1.0);
        rest = xbox_wheel_clamp(c->rest, 0.0, 1.0);
        return rest + (1.0 - rest) * f;
    }
    return 1.0;
}

static inline double xbox_wheel_level_step(double cur, double target, double dt_ms)
{
    if (dt_ms < 0)
        dt_ms = 0;
    if (dt_ms > XBOX_WHEEL_LEVEL_STEP_MS)
        dt_ms = XBOX_WHEEL_LEVEL_STEP_MS;
    if (target > cur) {
        cur += dt_ms / XBOX_WHEEL_LEVEL_UP_MS;
        if (cur > target)
            cur = target;
    } else {
        cur -= dt_ms / XBOX_WHEEL_LEVEL_DOWN_MS;
        if (cur < target)
            cur = target;
    }
    return xbox_wheel_clamp(cur, 0.0, 1.0);
}

/* The runaway guard. A device test cannot see which way a base pushes, so a
 * wrongly signed effect (once: a spring that pushed the wheel out to the end
 * stop) must be caught while it runs. If the wheel stays near an end of the
 * base's whole travel for XBOX_WHEEL_GUARD_HOLD_MS with the spring on, the
 * force is cut; it comes back only when the wheel is back toward the middle.
 * A spring pulls toward the centre, so only a hand, or a force in the wrong
 * direction, keeps the wheel there. */
#define XBOX_WHEEL_GUARD_FAR       0.90   /* of the half travel: "near an end stop" */
#define XBOX_WHEEL_GUARD_BACK      0.50   /* the force may return inside this */
#define XBOX_WHEEL_GUARD_HOLD_MS   1500u

typedef struct xbox_wheel_guard {
    int      at_end;    /* the wheel has been near an end since `since` */
    uint32_t since;     /* ms */
    int      tripped;   /* the force is cut */
} xbox_wheel_guard;

/* One reading of the steering axis (0..65535, or <0: none) at now_ms. Returns
 * 1 while the force must stay cut. */
static inline int xbox_wheel_guard_step(xbox_wheel_guard *g, int64_t steer, uint32_t now_ms)
{
    double n;

    if (steer < 0)
        return g->tripped;              /* no reading: keep what it was */
    n = ((double)steer - 32767.5) / 32767.5;
    if (n < 0)
        n = -n;
    if (n > XBOX_WHEEL_GUARD_FAR) {
        if (!g->at_end) {
            g->at_end = 1;
            g->since = now_ms;
        } else if ((uint32_t)(now_ms - g->since) >= XBOX_WHEEL_GUARD_HOLD_MS) {
            g->tripped = 1;
        }
    } else {
        g->at_end = 0;
        if (n < XBOX_WHEEL_GUARD_BACK)
            g->tripped = 0;
    }
    return g->tripped;
}

/* The glue's reader, set by wheel_sdl.c (or a test): fills *out with the
 * wheel's pad and returns 1 while a wheel is configured and its steering
 * device is attached; 0 otherwise. Any thread that polls the pad. */
typedef int (*xbox_wheel_read_fn)(XBOX_GAMEPAD *out);

/* wheel_sdl.c (the SDL pad backend only): start it from the backend's init;
 * ask whether a joystick is one the wheel names, so the pad backend leaves it
 * alone; and whether the window's thread already pumps SDL events
 * (xinput_device.c). */
void xbox_InputWheelInit(void);
void xbox_InputWheelShutdown(void);     /* the end of the run: the force off, the wheel let go */
int  xbox_InputWheelClaims(unsigned vid, unsigned pid);
int  xbox_InputMainPumpActive(void);
void xbox_InputWheelSetReader(xbox_wheel_read_fn fn);
/* The host's car feed for the spring's level (any backend; the SDL glue reads it). NULL: none. */
void xbox_InputWheelSetCarSource(xbox_wheel_car_fn fn);
xbox_wheel_car_fn xbox_InputWheelCarSource(void);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_INPUT_WHEEL_PAD_H */
