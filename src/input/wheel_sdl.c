/**
 * xbox_input wheel -- the SDL glue for wheel_pad.h.
 *
 * Opens the joysticks the wheel bindings name (RECOMP_WHEEL_STEER, _THROTTLE,
 * _BRAKE) by USB vendor and product id, as raw joysticks: a wheel base or a
 * set of pedals is a HID device SDL has no game controller mapping for, and
 * a mapped one would also be taken as a pad, so the pad backend asks
 * xbox_InputWheelClaims and leaves a device named here alone.
 *
 * The wheel and the pedals are often two devices (a base on one cable, load
 * cell pedals on another); each is opened once and shared by the controls
 * that name it. A device that is not there is looked for again every second,
 * so plugging the pedals in later works.
 *
 * The first reads of a freshly opened device are not used: some report a
 * mid-travel value for one poll before the real one.
 *
 * RECOMP_WHEEL_LOG=1 prints each device opened and, once a second while a
 * control moves, the raw axes. The axis index is SDL's, which may not be the
 * order Windows' game controller panel shows: this is how to find it.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "wheel_pad.h"
#include "input_backend.h"
#include "recomp_env.h"
#include "xr_log.h"

#define WHEEL_DEVS    3         /* steering, throttle, brake: at most three devices */
#define WHEEL_WARMUP  3         /* reads of a new device that are not used */
#define WHEEL_RESCAN_MS 1000

typedef struct wheel_dev {
    uint32_t     dev;           /* vid << 16 | pid; 0: slot free */
    SDL_Joystick *j;
    int          warm;          /* reads still to skip */
    uint64_t     btn_prev;      /* RECOMP_WHEEL_LOG: buttons down at the last read */
    int          hat_prev;      /* ... and the hat 0 */
} wheel_dev;

static xbox_wheel_cfg g_cfg;
static wheel_dev g_dev[WHEEL_DEVS];
static int g_on;
static Uint32 g_last_scan;
static int g_log;
static Uint32 g_log_t;


static wheel_dev *dev_slot(uint32_t dev)
{
    int i;

    for (i = 0; i < WHEEL_DEVS; i++)
        if (g_dev[i].dev == dev)
            return &g_dev[i];
    return NULL;
}

static void dev_want(const xbox_wheel_bind *b)
{
    if (b->axis < 0 || dev_slot(b->dev))
        return;
    for (int i = 0; i < WHEEL_DEVS; i++)
        if (!g_dev[i].dev) {
            g_dev[i].dev = b->dev;
            return;
        }
}

int xbox_InputWheelClaims(unsigned vid, unsigned pid)
{
    return g_on && dev_slot((uint32_t)(vid << 16 | pid)) != NULL;
}

/* ---- force feedback: the centring spring (wheel_pad.h) --------------------
 *
 * Opened with the steering device, only when RECOMP_WHEEL_FFB_SPRING asks.
 * The effect is created once and runs for good; what changes is the base's
 * gain, which is faded up after the start and down at once whenever a host
 * output must be silent (the overlay, the window not in front, the end of the
 * run: xbox_InputOutputMuted). The gain is the only thing the title's poll
 * touches. A lock guards the handle, because the end of the run closes it from
 * another thread than the one that polls.
 *
 * Refused, and logged, if the base cannot do a spring or a gain: without a gain
 * there is no fade, and the fade is the safety. */
static SDL_Haptic *g_hap;
static int g_spring_id = -1;
static int g_rum_ok, g_kick_ok;      /* the base can play them, and they are asked for */
static int g_rum_id = -1;           /* the rumble: a sine effect whose strength follows the title's motors */
static double g_rum_pct;            /* RECOMP_WHEEL_RUMBLE: the strength at the title's full rumble, percent; 0 = off */
static int g_rum_level = -1;        /* the magnitude the base has */
static int g_rum_period = 40;       /* and its period, ms */
static double g_eng_pct;            /* RECOMP_WHEEL_ENGINE: a light engine vibration while racing, percent of the base's force; 0 = off */
static int g_kick_id = -1;          /* the weight on the wheel when the car is hit: a constant force along the steering axis */
static double g_kick_pct;           /* RECOMP_WHEEL_KICK: percent of the base's force at the hardest hit; 0 = off */
static int g_kick_flip;             /* RECOMP_WHEEL_KICK_FLIP=1: the other way, if the weight comes out on the wrong side */
static Uint32 g_kick_t;
static double g_kick_last;
static double g_jolt_lv;            /* a hit's or a landing's shake, 0..1, dying away over 250 ms */
static double g_title_lv;           /* the title's rumble, 0..1 */
static double g_eng_lv;             /* the engine's, 0..1 */
static double g_spring_pct;         /* RECOMP_WHEEL_FFB_SPRING, 0 = off */
static double g_ffb_gain;           /* 0..1, what the base is given */
static int g_ffb_gain_set = -1;     /* the percent last given to the base */
static Uint32 g_ffb_t;
static int g_ffb_muted = -1;        /* logged transitions */
static int g_ffb_trip_logged;       /* the guard's cut was announced */
static SDL_mutex *g_ffb_lock;
static xbox_wheel_guard g_ffb_guard;    /* the runaway guard (wheel_pad.h) */
static SDL_HapticEffect g_spring_fx;    /* the effect as last given to the base */
static xbox_wheel_level_cfg g_level_cfg;
static double g_level = -1;             /* the spring's level now; <0 until the first poll sets it */
static int g_level_coeff = -1;          /* the coefficient the base has */
static int g_level_racing = -1;         /* logged transitions */
static Uint32 g_level_log_t;            /* the once-a-second line (RECOMP_WHEEL_LOG) */

static void ffb_lock(void)   { if (g_ffb_lock) SDL_LockMutex(g_ffb_lock); }
static void ffb_unlock(void) { if (g_ffb_lock) SDL_UnlockMutex(g_ffb_lock); }

/* Force off and the handle let go. The lock is held by the caller. */
static void ffb_close_locked(void)
{
    if (!g_hap)
        return;
    SDL_HapticSetGain(g_hap, 0);
    SDL_HapticStopAll(g_hap);
    if (g_spring_id >= 0)
        SDL_HapticDestroyEffect(g_hap, g_spring_id);
    if (g_rum_id >= 0)
        SDL_HapticDestroyEffect(g_hap, g_rum_id);
    g_rum_id = -1;
    if (g_kick_id >= 0)
        SDL_HapticDestroyEffect(g_hap, g_kick_id);
    g_kick_id = -1;
    SDL_HapticClose(g_hap);
    g_hap = NULL;
    g_spring_id = -1;
    g_ffb_gain = 0;
    g_ffb_gain_set = -1;
}

static void ffb_open(SDL_Joystick *j)
{
    SDL_HapticEffect e;
    xbox_wheel_spring sp;
    unsigned feat;

    if (g_spring_pct <= 0 || g_hap)
        return;
    if (!SDL_WasInit(SDL_INIT_HAPTIC))
        SDL_InitSubSystem(SDL_INIT_HAPTIC);
    if (!SDL_JoystickIsHaptic(j)) {
        XR_NOTICE("[WHEEL] force feedback: the steering device has no haptic interface: spring off\n");
        return;
    }
    ffb_lock();
    g_hap = SDL_HapticOpenFromJoystick(j);
    if (!g_hap) {
        XR_NOTICE("[WHEEL] force feedback: cannot open the haptic interface (%s): spring off\n", SDL_GetError());
        ffb_unlock();
        return;
    }
    feat = SDL_HapticQuery(g_hap);
    if (!(feat & SDL_HAPTIC_SPRING) || !(feat & SDL_HAPTIC_GAIN)) {
        XR_NOTICE("[WHEEL] force feedback: the base lacks a spring or a gain (0x%X): spring off\n", feat);
        ffb_close_locked();
        ffb_unlock();
        return;
    }
    SDL_HapticSetGain(g_hap, 0);            /* silent until the fade-in */
    sp = xbox_wheel_spring_params(&g_cfg, g_spring_pct);
    memset(&e, 0, sizeof e);
    e.type = SDL_HAPTIC_SPRING;
    e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    e.condition.direction.dir[0] = 1;       /* the steering axis */
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = e.condition.left_sat[0] = sp.sat;
    /* Both sides the same, positive: each is "how fast the force grows toward
     * that side", and a spring pulls back from both (SDL's own haptic test does
     * the same). The left one negated is an anti-spring on that side: it pushes
     * the wheel out to the end stop. */
    e.condition.right_coeff[0] = sp.coeff;
    e.condition.left_coeff[0] = sp.coeff;
    g_spring_fx = e;
    g_level = -1;
    g_level_coeff = sp.coeff;
    g_spring_id = SDL_HapticNewEffect(g_hap, &e);
    if (g_spring_id < 0 || SDL_HapticRunEffect(g_hap, g_spring_id, 1) != 0) {
        XR_NOTICE("[WHEEL] force feedback: the spring was refused (%s): spring off\n", SDL_GetError());
        ffb_close_locked();
        ffb_unlock();
        return;
    }
    /* The rumble (a sine) and the hits' weight (a constant force) are made when they are needed, with their final numbers, and
     * let go after: updating a running one did not make the base play it (a test of each kind, played fresh, did). */
    g_rum_ok = (g_rum_pct > 0 || g_eng_pct > 0) && (feat & SDL_HAPTIC_SINE);
    g_kick_ok = g_kick_pct > 0 && (feat & SDL_HAPTIC_CONSTANT);
    if (g_rum_ok)
        XR_NOTICE("[WHEEL] rumble: the title's rumble on the wheel, up to %.0f%%, the engine up to %.0f%%\n", g_rum_pct, g_eng_pct);
    else if (g_rum_pct > 0 || g_eng_pct > 0)
        XR_NOTICE("[WHEEL] rumble: the base has no sine effect (0x%X): rumble off\n", feat);
    if (g_kick_ok)
        XR_NOTICE("[WHEEL] hits: a weight on the wheel toward the side the car is hit from, up to %.0f%%%s\n", g_kick_pct,
                  g_kick_flip ? " (flipped)" : "");
    else if (g_kick_pct > 0)
        XR_NOTICE("[WHEEL] hits: the base has no constant force (0x%X): off\n", feat);
    g_ffb_t = SDL_GetTicks();
    XR_NOTICE("[WHEEL] force feedback: centring spring %.0f%% at the lock's edge "
              "(coefficient %d, saturation %u of 65535), faded in over %.1f s\n",
              g_spring_pct, sp.coeff, sp.sat, XBOX_WHEEL_RAMP_UP_MS / 1000.0);
    ffb_unlock();
}

/* The rumble's sine from the title's rumble, the engine's and a jolt's: the strongest, the period shorter with the engine's speed.
 * A new effect with the final numbers each time they change enough (updating the running one was not played by the base), the old
 * one stopped and let go. The lock is held. */
static void rumble_apply_locked(void)
{
    double lv = g_title_lv * g_rum_pct / 100.0, e = g_eng_lv * g_eng_pct / 100.0, j = g_jolt_lv * g_rum_pct / 100.0;
    int mag, period;

    if (!g_hap || !g_rum_ok)
        return;
    lv = lv > e ? lv : e;
    lv = lv > j ? lv : j;
    mag = (int)(lv * 32767.0 + 0.5);
    if (mag > 32767)
        mag = 32767;
    period = g_eng_lv > 0 ? (int)(50.0 - 25.0 * g_eng_lv + 0.5) : 40;
    if (mag < 400) {                                        /* nothing to feel */
        if (g_rum_id >= 0) {
            SDL_HapticStopEffect(g_hap, g_rum_id);
            SDL_HapticDestroyEffect(g_hap, g_rum_id);
            g_rum_id = -1;
        }
        g_rum_level = 0;
        return;
    }
    if (g_rum_id < 0 || mag - g_rum_level > 1500 || g_rum_level - mag > 1500 || period - g_rum_period > 6
            || g_rum_period - period > 6) {
        SDL_HapticEffect r;
        int id;

        memset(&r, 0, sizeof r);
        r.type = SDL_HAPTIC_SINE;
        r.periodic.direction.type = SDL_HAPTIC_CARTESIAN;
        r.periodic.direction.dir[0] = 1;
        r.periodic.length = SDL_HAPTIC_INFINITY;
        r.periodic.period = (Uint16)period;
        r.periodic.magnitude = (Sint16)mag;
        id = SDL_HapticNewEffect(g_hap, &r);
        if (id >= 0 && SDL_HapticRunEffect(g_hap, id, 1) == 0) {
            if (g_rum_id >= 0) {
                SDL_HapticStopEffect(g_hap, g_rum_id);
                SDL_HapticDestroyEffect(g_hap, g_rum_id);
            }
            g_rum_id = id;
            g_rum_level = mag;
            g_rum_period = period;
        } else if (id >= 0) {
            SDL_HapticDestroyEffect(g_hap, id);
        }
    }
}

/* The title's rumble (input_core.c hands the motors' speeds of port 0): the strongest of the two. */
static void wheel_rumble(unsigned short left, unsigned short right)
{
    unsigned lv = left > right ? left : right;

    if (!g_rum_ok || !g_hap)
        return;
    ffb_lock();
    g_title_lv = (double)lv / 65535.0;
    rumble_apply_locked();
    ffb_unlock();
}

/* A hit: a constant force along the steering axis, to `side` (-1 left, 1 right), `kick` (0..1) of the strength, 450 ms, a new effect
 * each time (the one before is let go when it has ended). In the base's own sense a positive level pushes to the LEFT (a test of
 * the base: +12000 pushed left, -12000 right). The lock is held. */
static void kick_reap(Uint32 now)
{
    if (g_kick_id >= 0 && now - g_kick_t > 600) {
        SDL_HapticStopEffect(g_hap, g_kick_id);
        SDL_HapticDestroyEffect(g_hap, g_kick_id);
        g_kick_id = -1;
    }
}

static void kick_fire(double side, double kick)
{
    SDL_HapticEffect k;
    Uint32 now = SDL_GetTicks();
    int level, id;

    if (!g_kick_ok || !g_hap || kick <= 0.0)
        return;
    if (now - g_kick_t < 120 && kick <= g_kick_last)       /* a run of records of one hit: the first sets it */
        return;
    level = (int)(kick * g_kick_pct / 100.0 * 32767.0 + 0.5) * (side < 0.0 ? 1 : -1) * (g_kick_flip ? -1 : 1);
    if (level > 32767) level = 32767;
    if (level < -32767) level = -32767;
    memset(&k, 0, sizeof k);
    k.type = SDL_HAPTIC_CONSTANT;
    k.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    k.constant.direction.dir[0] = 1;
    k.constant.length = 450;
    k.constant.level = (Sint16)level;
    id = SDL_HapticNewEffect(g_hap, &k);
    if (id >= 0 && SDL_HapticRunEffect(g_hap, id, 1) == 0) {
        if (g_kick_id >= 0) {
            SDL_HapticStopEffect(g_hap, g_kick_id);
            SDL_HapticDestroyEffect(g_hap, g_kick_id);
        }
        g_kick_id = id;
        g_kick_t = now;
        g_kick_last = kick;
        if (g_log)
            XR_NOTICE("[WHEEL] hit: weight %d toward the %s\n", level, level > 0 ? "left" : "right");
    } else if (id >= 0) {
        SDL_HapticDestroyEffect(g_hap, id);
    }
}

/* The title's poll: move the base's gain toward full while output is allowed,
 * toward none when it must be silent. */
static void ffb_tick(Uint32 now, int64_t steer)
{
    int on, pct, tripped, racing = 0;
    double dt, speed = 0, target;
    xbox_wheel_car_fn car;

    if (!g_hap)
        return;
    ffb_lock();
    if (g_hap) {
        /* The guard: the wheel parked near an end stop with the spring on
         * means the force is pushing it there, whatever the cause. */
        tripped = xbox_wheel_guard_step(&g_ffb_guard, steer, (uint32_t)now);
        if (tripped != g_ffb_trip_logged) {
            g_ffb_trip_logged = tripped;
            XR_NOTICE(tripped ? "[WHEEL] force feedback: the wheel sat at an end stop with the spring on: "
                                "force cut until it comes back toward the centre\n"
                              : "[WHEEL] force feedback: the wheel is back toward the centre: force allowed again\n");
        }
        on = !xbox_InputOutputMuted() && !tripped;
        dt = (double)(Uint32)(now - g_ffb_t);
        g_ffb_t = now;
        g_ffb_gain = xbox_wheel_ffb_gain_step(g_ffb_gain, on, dt);
        pct = (int)(g_ffb_gain * 100.0 + 0.5);
        if (pct != g_ffb_gain_set) {
            SDL_HapticSetGain(g_hap, pct);
            g_ffb_gain_set = pct;
        }
        /* The level: the spring's strength by speed, and off outside a race. */
        car = xbox_InputWheelCarSource();
        if (car)
            racing = car(&speed) != 0;
        target = xbox_wheel_level_target(&g_level_cfg, car != NULL, racing, speed);
        if (racing && xbox_InputWheelGripSource()) {
            double side = 0.0, kick = 0.0, jolt = 0.0, gr = xbox_InputWheelGripSource()(&side, &kick, &jolt);

            target *= gr < 0.0 ? 0.0 : gr > 1.0 ? 1.0 : gr;
            if (kick > 0.0)
                kick_fire(side, kick);
            if (jolt > g_jolt_lv)
                g_jolt_lv = jolt > 1.0 ? 1.0 : jolt;
        }
        g_level = g_level < 0 ? target : xbox_wheel_level_step(g_level, target, dt);
        if (g_spring_id >= 0) {
            xbox_wheel_spring sp = xbox_wheel_spring_params(&g_cfg, g_spring_pct * g_level);
            int d = sp.coeff - g_level_coeff;

            if (sp.coeff != g_level_coeff && (d >= XBOX_WHEEL_LEVEL_MIN_COEFF || -d >= XBOX_WHEEL_LEVEL_MIN_COEFF
                                              || g_level <= 0.0 || g_level >= 1.0)) {
                g_spring_fx.condition.right_coeff[0] = g_spring_fx.condition.left_coeff[0] = sp.coeff;
                g_spring_fx.condition.right_sat[0] = g_spring_fx.condition.left_sat[0] = sp.sat;
                if (SDL_HapticUpdateEffect(g_hap, g_spring_id, &g_spring_fx) == 0) {
                    if (g_level_coeff == 0 && sp.coeff > 0)          /* some bases stop an effect at no strength: start it again */
                        SDL_HapticRunEffect(g_hap, g_spring_id, 1);
                    g_level_coeff = sp.coeff;
                }
                else if (g_log)
                    XR_NOTICE("[WHEEL] force feedback: updating the spring failed (%s)\n", SDL_GetError());
            }
        }
        kick_reap(now);
        if (g_rum_ok) {
            /* The engine: light, a little at a standstill and more with speed, only in a race. */
            double sp = speed / 40.0;

            g_eng_lv = (g_eng_pct > 0 && racing) ? 0.25 + 0.75 * (sp > 1.0 ? 1.0 : sp < 0.0 ? 0.0 : sp) : 0.0;
            g_jolt_lv -= dt / 250.0;
            if (g_jolt_lv < 0.0)
                g_jolt_lv = 0.0;
            rumble_apply_locked();
        }
        if (g_log && car && now - g_level_log_t >= 1000) {      /* once a second: what the spring is doing */
            g_level_log_t = now;
            XR_NOTICE("[WHEEL] spring: %s speed %.1f m/s level %.2f coefficient %d of %d, gain %d%%\n",
                      racing ? "racing" : "no race", speed, g_level, g_level_coeff, 32767, g_ffb_gain_set);
        }
        if (g_log && car && racing != g_level_racing) {
            g_level_racing = racing;
            XR_NOTICE("[WHEEL] force feedback: %s\n", racing ? "race running: the spring follows the car"
                                                                : "no race running");
        }
        if (g_log && on != g_ffb_muted) {
            g_ffb_muted = on;
            XR_NOTICE("[WHEEL] force feedback: %s\n", on ? "fading in"
                      : tripped ? "fading out (the runaway guard)"
                                : "muted (overlay, window not in front or ending)");
        }
    }
    ffb_unlock();
}

/* The end of the run, from whichever thread ends it (the rumble's shutdown). */
void xbox_InputWheelShutdown(void)
{
    ffb_lock();
    ffb_close_locked();
    ffb_unlock();
}

/* Open what is named and not open; drop what has gone. */
static void dev_scan(void)
{
    int i, n, k;

    for (i = 0; i < WHEEL_DEVS; i++)
        if (g_dev[i].j && !SDL_JoystickGetAttached(g_dev[i].j)) {
            if (g_dev[i].dev == g_cfg.steer.dev) {
                ffb_lock();
                ffb_close_locked();     /* the handle belongs to the joystick */
                ffb_unlock();
            }
            SDL_JoystickClose(g_dev[i].j);
            g_dev[i].j = NULL;
            if (g_log)
                XR_NOTICE("[WHEEL] %04X:%04X removed\n", g_dev[i].dev >> 16, g_dev[i].dev & 0xFFFF);
        }
    n = SDL_NumJoysticks();
    for (k = 0; k < n; k++) {
        uint32_t id = (uint32_t)(SDL_JoystickGetDeviceVendor(k) << 16 | SDL_JoystickGetDeviceProduct(k));
        wheel_dev *d = dev_slot(id);

        if (!d || d->j)
            continue;
        d->j = SDL_JoystickOpen(k);
        d->warm = WHEEL_WARMUP;
        d->btn_prev = 0;
        d->hat_prev = 0;
        if (d->j)
            XR_NOTICE("[WHEEL] opened %04X:%04X \"%s\", %d axes, %d buttons, %d hats\n",
                      id >> 16, id & 0xFFFF, SDL_JoystickName(d->j),
                      SDL_JoystickNumAxes(d->j), SDL_JoystickNumButtons(d->j),
                      SDL_JoystickNumHats(d->j));
        if (d->j && g_cfg.steer.axis >= 0 && id == g_cfg.steer.dev)
            ffb_open(d->j);
    }
}

/* One control's raw axis (0..65535), or -1: not bound, device missing, or
 * still warming up. */
static int64_t axis_read(const xbox_wheel_bind *b)
{
    wheel_dev *d;

    if (b->axis < 0 || !(d = dev_slot(b->dev)) || !d->j || d->warm > 0
            || b->axis >= SDL_JoystickNumAxes(d->j))
        return -1;
    return (int64_t)xbox_wheel_axis_u16(SDL_JoystickGetAxis(d->j, b->axis));
}

/* Whether a bound button is down (its device open, warmed up, the index there). */
static int button_read(const xbox_wheel_bind *b)
{
    wheel_dev *d;

    if (b->axis < 0 || !(d = dev_slot(b->dev)) || !d->j || d->warm > 0
            || b->axis >= SDL_JoystickNumButtons(d->j))
        return 0;
    return SDL_JoystickGetButton(d->j, b->axis) != 0;
}

/* The bound hat's SDL_HAT_* mask, 0 when centred or not available. */
static int hat_read(const xbox_wheel_bind *b)
{
    wheel_dev *d;

    if (b->axis < 0 || !(d = dev_slot(b->dev)) || !d->j || d->warm > 0
            || b->axis >= SDL_JoystickNumHats(d->j))
        return 0;
    return SDL_JoystickGetHat(d->j, b->axis);
}

/* RECOMP_WHEEL_LOG: each button of an open device going down or up, and each
 * change of its hat, by the index SDL gives it. This is how to check a
 * binding's number against the physical button, bound or not. */
static void log_edges(wheel_dev *d)
{
    uint64_t now = 0;
    int n, i, hat;

    if (!d->j || d->warm > 0)
        return;
    n = SDL_JoystickNumButtons(d->j);
    for (i = 0; i < n && i < 64; i++)
        if (SDL_JoystickGetButton(d->j, i))
            now |= (uint64_t)1 << i;
    for (i = 0; i < n && i < 64; i++)
        if (((now ^ d->btn_prev) >> i) & 1)
            XR_NOTICE("[WHEEL] %04X:%04X button %d %s\n", d->dev >> 16, d->dev & 0xFFFF, i,
                      (now >> i) & 1 ? "down" : "up");
    d->btn_prev = now;
    hat = SDL_JoystickNumHats(d->j) > 0 ? SDL_JoystickGetHat(d->j, 0) : 0;
    if (hat != d->hat_prev)
        XR_NOTICE("[WHEEL] %04X:%04X hat 0 %s%s%s%s%s\n", d->dev >> 16, d->dev & 0xFFFF,
                  hat ? "" : "centred", hat & XBOX_WHEEL_HAT_UP ? "up " : "",
                  hat & XBOX_WHEEL_HAT_DOWN ? "down " : "", hat & XBOX_WHEEL_HAT_LEFT ? "left " : "",
                  hat & XBOX_WHEEL_HAT_RIGHT ? "right" : "");
    d->hat_prev = hat;
}

static int wheel_read(XBOX_GAMEPAD *out)
{
    Uint32 now = SDL_GetTicks();
    int64_t s, t, b;
    uint8_t held[XBOX_WHEEL_NBTN];
    int i, open = 0;

    if (!xbox_InputMainPumpActive())
        SDL_JoystickUpdate();
    if (now - g_last_scan >= WHEEL_RESCAN_MS) {
        g_last_scan = now;
        dev_scan();
    }
    for (i = 0; i < WHEEL_DEVS; i++) {
        if (g_dev[i].j && g_dev[i].warm > 0)
            g_dev[i].warm--;
        open += g_dev[i].j != NULL;
    }
    s = axis_read(&g_cfg.steer);
    ffb_tick(now, s);
    if (!open)
        return 0;
    t = axis_read(&g_cfg.throttle);
    b = axis_read(&g_cfg.brake);
    {
        static int idle_logged;
        int idle = xbox_wheel_pedals_idle(t, b);

        if (idle != idle_logged) {
            idle_logged = idle;
            XR_NOTICE(idle ? "[WHEEL] the pedals read exactly mid-scale on both axes: they look unpowered or not "
                             "started (unplug and replug them); ignored until one moves\n"
                           : "[WHEEL] the pedals are reporting again\n");
        }
        if (idle) {         /* released: not two half-pressed pedals (an inverted pedal rests high) */
            t = g_cfg.throttle.invert ? 65535 : 0;
            b = g_cfg.brake.invert ? 65535 : 0;
        }
    }
    xbox_wheel_resolve(&g_cfg, s, t, b, out);
    for (i = 0; i < XBOX_WHEEL_NBTN; i++)
        held[i] = (uint8_t)button_read(&g_cfg.btn[i]);
    xbox_wheel_apply_buttons(held, hat_read(&g_cfg.hat), out);
    if (g_log)
        for (i = 0; i < WHEEL_DEVS; i++)
            if (g_dev[i].dev)
                log_edges(&g_dev[i]);
    if (g_log && now - g_log_t >= 1000 && (out->sThumbLX || out->bAnalogButtons[XBOX_BUTTON_RTRIGGER]
                                           || out->bAnalogButtons[XBOX_BUTTON_LTRIGGER])) {
        g_log_t = now;
        XR_NOTICE("[WHEEL] steer %lld throttle %lld brake %lld -> lx %d rt %u lt %u\n",
                  (long long)s, (long long)t, (long long)b, out->sThumbLX,
                  out->bAnalogButtons[XBOX_BUTTON_RTRIGGER],
                  out->bAnalogButtons[XBOX_BUTTON_LTRIGGER]);
    }
    return 1;
}

static int bind_env(xbox_wheel_bind *b, recomp_env_id id, const char *what)
{
    const char *v = recomp_env(id);

    if (!xbox_wheel_bind_parse(b, v)) {
        XR_NOTICE("[WHEEL] %s: \"%s\" is not VID:PID:N (hex, hex, decimal; e.g. 044F:B696:0): ignored\n",
                  what, v);
        return 0;
    }
    return b->axis >= 0;
}

/* Called from the SDL pad backend's init, on the thread that initialises
 * SDL. Nothing is opened until a wheel is bound. */
void xbox_InputWheelInit(void)
{
    int any = 0;

    if (g_on)
        return;
    xbox_wheel_cfg_defaults(&g_cfg);
    any |= bind_env(&g_cfg.steer, RENV_INPUT_WHEEL_STEER, "RECOMP_WHEEL_STEER");
    any |= bind_env(&g_cfg.throttle, RENV_INPUT_WHEEL_THROTTLE, "RECOMP_WHEEL_THROTTLE");
    any |= bind_env(&g_cfg.brake, RENV_INPUT_WHEEL_BRAKE, "RECOMP_WHEEL_BRAKE");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_ACCEPT], RENV_INPUT_WHEEL_BTN_ACCEPT, "RECOMP_WHEEL_BTN_ACCEPT");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_BACK], RENV_INPUT_WHEEL_BTN_BACK, "RECOMP_WHEEL_BTN_BACK");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_PAUSE], RENV_INPUT_WHEEL_BTN_PAUSE, "RECOMP_WHEEL_BTN_PAUSE");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_LOOKBACK], RENV_INPUT_WHEEL_BTN_LOOKBACK, "RECOMP_WHEEL_BTN_LOOKBACK");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_VIEW], RENV_INPUT_WHEEL_BTN_VIEW, "RECOMP_WHEEL_BTN_VIEW");
    any |= bind_env(&g_cfg.btn[XBOX_WHEEL_BTN_RECENTRE], RENV_INPUT_WHEEL_BTN_RECENTRE, "RECOMP_WHEEL_BTN_RECENTRE");
    any |= bind_env(&g_cfg.hat, RENV_INPUT_WHEEL_HAT, "RECOMP_WHEEL_HAT");
    if (!any)
        return;
    g_cfg.range_deg = recomp_env_double(RENV_INPUT_WHEEL_RANGE, XBOX_WHEEL_RANGE_DEFAULT);
    g_cfg.lock_deg = recomp_env_double(RENV_INPUT_WHEEL_LOCK, XBOX_WHEEL_LOCK_DEFAULT);
    g_cfg.pedal_dead = recomp_env_double(RENV_INPUT_WHEEL_DEADZONE, XBOX_WHEEL_PEDAL_DEAD_DEFAULT);
    g_cfg.steer_dead = recomp_env_double(RENV_INPUT_WHEEL_STEER_DEAD, 0.0);
    g_cfg.steer_skip = recomp_env_double(RENV_INPUT_WHEEL_STEER_SKIP, 0.0);
    g_cfg.steer_sens = recomp_env_double(RENV_INPUT_WHEEL_STEER_SENS, 100.0);
    g_cfg.steer_lin = recomp_env_double(RENV_INPUT_WHEEL_STEER_LIN, 0.0);
    g_cfg.thr_sens = recomp_env_double(RENV_INPUT_WHEEL_THROTTLE_SENS, 100.0);
    g_cfg.thr_lin = recomp_env_double(RENV_INPUT_WHEEL_THROTTLE_LIN, 0.0);
    g_cfg.brk_sens = recomp_env_double(RENV_INPUT_WHEEL_BRAKE_SENS, 100.0);
    g_cfg.brk_lin = recomp_env_double(RENV_INPUT_WHEEL_BRAKE_LIN, 0.0);
    g_log = recomp_env_on(RENV_INPUT_WHEEL_LOG);
    g_spring_pct = recomp_env_double(RENV_INPUT_WHEEL_FFB_SPRING, 0.0);
    if (g_spring_pct > XBOX_WHEEL_SPRING_MAX) {
        XR_NOTICE("[WHEEL] RECOMP_WHEEL_FFB_SPRING %.0f is above the limit of %.0f: using %.0f\n",
                  g_spring_pct, XBOX_WHEEL_SPRING_MAX, XBOX_WHEEL_SPRING_MAX);
        g_spring_pct = XBOX_WHEEL_SPRING_MAX;
    }
    if (g_spring_pct > 0 && g_cfg.steer.axis < 0) {
        XR_NOTICE("[WHEEL] RECOMP_WHEEL_FFB_SPRING needs RECOMP_WHEEL_STEER (the base is the steering device): spring off\n");
        g_spring_pct = 0;
    }
    g_level_cfg.speed_ref = recomp_env_double(RENV_INPUT_WHEEL_FFB_SPEED, 0.0);
    g_level_cfg.rest = recomp_env_double(RENV_INPUT_WHEEL_FFB_REST, 30.0) / 100.0;
    g_level_cfg.race_only = recomp_env_on(RENV_INPUT_WHEEL_FFB_RACE_ONLY);
    if (g_spring_pct > 0 && (g_level_cfg.speed_ref > 0 || g_level_cfg.race_only))
        XR_NOTICE("[WHEEL] force feedback: spring level%s%s (needs the game to feed the car)\n",
                  g_level_cfg.speed_ref > 0 ? " follows the speed" : "", g_level_cfg.race_only ? ", off outside a race" : "");
    g_rum_pct = recomp_env_double(RENV_INPUT_WHEEL_RUMBLE, 0.0);
    if (g_rum_pct > 100.0)
        g_rum_pct = 100.0;
    g_eng_pct = recomp_env_double(RENV_INPUT_WHEEL_ENGINE, 0.0);
    if (g_eng_pct > 100.0)
        g_eng_pct = 100.0;
    g_kick_pct = recomp_env_double(RENV_INPUT_WHEEL_KICK, 0.0);
    if (g_kick_pct > 100.0)
        g_kick_pct = 100.0;
    g_kick_flip = recomp_env_on(RENV_INPUT_WHEEL_KICK_FLIP);
    if ((g_rum_pct > 0 || g_eng_pct > 0 || g_kick_pct > 0) && g_spring_pct <= 0)
        XR_NOTICE("[WHEEL] RECOMP_WHEEL_RUMBLE needs RECOMP_WHEEL_FFB_SPRING (the force feedback with its fade and guard): rumble off\n");
    if (g_spring_pct > 0 && !g_ffb_lock)
        g_ffb_lock = SDL_CreateMutex();
    dev_want(&g_cfg.steer);
    dev_want(&g_cfg.throttle);
    dev_want(&g_cfg.brake);
    for (int i = 0; i < XBOX_WHEEL_NBTN; i++)
        dev_want(&g_cfg.btn[i]);
    dev_want(&g_cfg.hat);
    if (!SDL_WasInit(SDL_INIT_JOYSTICK))
        SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    g_on = 1;
    dev_scan();
    xbox_InputWheelSetReader(wheel_read);
    if (g_rum_pct > 0 && g_spring_pct > 0)
        xbox_InputWheelSetRumble(wheel_rumble);
    XR_NOTICE("[WHEEL] on: wheel turns %.0f degrees, game lock %.0f, pedal deadzone %.0f%%, "
              "steering deadzone %.1f%%, skip %.1f%%\n",
              g_cfg.range_deg, g_cfg.lock_deg, g_cfg.pedal_dead, g_cfg.steer_dead, g_cfg.steer_skip);
}
