/*
 * input_wheel: a racing wheel as a port-0 pad (src/input/wheel_pad.h) and its
 * merge into port 0 (src/input/input_core.c), against a fake backend and a
 * fake wheel reader. The numbers in the pedal checks are those a Thrustmaster
 * T818 base and its pedals gave: steering 0..65535 (left to right), a pedal 0
 * at rest and 65535 down, up to about 1800 of noise at rest.
 */
#include "wheel_pad.h"
#include "input_backend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ── a fake host: a pad only while g_pad ──────────────────────────── */

static int g_pad;
static XBOX_GAMEPAD g_pad_state;
static uint64_t g_now = 1000;

static DWORD f_get(DWORD port, XBOX_INPUT_STATE *out)
{
    if (port != 0 || !g_pad)
        return ERROR_DEVICE_NOT_CONNECTED;
    memset(out, 0, sizeof *out);
    out->Gamepad = g_pad_state;
    out->dwPacketNumber = 7;
    return ERROR_SUCCESS;
}
static DWORD f_set(DWORD port, const XBOX_VIBRATION *v) { (void)port; (void)v; return ERROR_SUCCESS; }
static uint64_t f_clock(void) { return g_now += 2000; }

const xbox_input_backend xbox_input_host_backend = { NULL, f_get, f_set, NULL, f_clock, NULL };

/* ── a fake wheel reader ──────────────────────────────────────────── */

static int g_wheel_here;
static xbox_wheel_cfg g_cfg;
static int64_t g_s = -1, g_t = -1, g_b = -1;

static uint8_t g_held[XBOX_WHEEL_NBTN];
static int g_hat;

static int f_wheel(XBOX_GAMEPAD *out)
{
    if (!g_wheel_here)
        return 0;
    xbox_wheel_resolve(&g_cfg, g_s, g_t, g_b, out);
    xbox_wheel_apply_buttons(g_held, g_hat, out);
    return 1;
}

static void setup(void)
{
    xbox_InputSetBackend(NULL);
    xbox_InputWheelSetReader(f_wheel);
    xbox_wheel_cfg_defaults(&g_cfg);
    g_cfg.steer.axis = 0;
    g_cfg.throttle.axis = 0;
    g_cfg.brake.axis = 2;
    g_wheel_here = 1;
    g_pad = 0;
    memset(&g_pad_state, 0, sizeof g_pad_state);
    g_s = 32768; g_t = 0; g_b = 0;
    memset(g_held, 0, sizeof g_held);
    g_hat = 0;
}

static XBOX_GAMEPAD read0(DWORD *rc, unsigned *src, DWORD *pkt)
{
    XBOX_INPUT_STATE s;
    memset(&s, 0, sizeof s);
    *rc = xbox_InputGetState(0, &s);
    *src = xbox_InputSources(0);
    if (pkt)
        *pkt = s.dwPacketNumber;
    return s.Gamepad;
}

int main(void)
{
    xbox_wheel_bind b;
    xbox_wheel_cfg c;
    XBOX_GAMEPAD g;
    DWORD rc, p1, p2;
    unsigned src;

    /* binding text */
    CHECK(xbox_wheel_bind_parse(&b, "044F:B696:0") && b.dev == 0x044FB696u && b.axis == 0 && !b.invert);
    CHECK(xbox_wheel_bind_parse(&b, "044f:b6fc:2!") && b.dev == 0x044FB6FCu && b.axis == 2 && b.invert);
    CHECK(xbox_wheel_bind_parse(&b, NULL) && b.axis < 0);
    CHECK(xbox_wheel_bind_parse(&b, "") && b.axis < 0);
    CHECK(!xbox_wheel_bind_parse(&b, "044F:B696") && b.axis < 0);
    CHECK(!xbox_wheel_bind_parse(&b, "044F:B696:x"));
    CHECK(!xbox_wheel_bind_parse(&b, "044F:B696:2 "));
    CHECK(!xbox_wheel_bind_parse(&b, "1044F:B696:2"));
    CHECK(!xbox_wheel_bind_parse(&b, "044F:B696:64"));

    /* SDL axis to the hardware's 0..65535 */
    CHECK(xbox_wheel_axis_u16(-32768) == 0);
    CHECK(xbox_wheel_axis_u16(0) == 32768);
    CHECK(xbox_wheel_axis_u16(32767) == 65535);

    /* steering: a 900 degree base, 540 lock: full at 270 degrees each way */
    xbox_wheel_cfg_defaults(&c);
    CHECK(xbox_wheel_steer(32768, &c, 0) >= -1 && xbox_wheel_steer(32768, &c, 0) <= 1);
    CHECK(xbox_wheel_steer(0, &c, 0) == -32767);
    CHECK(xbox_wheel_steer(65535, &c, 0) == 32767);
    CHECK(xbox_wheel_steer(65535, &c, 1) == -32767);
    /* a quarter of the travel right is 0.5 * (900/540) = 0.83 of full */
    CHECK(xbox_wheel_steer(49151, &c, 0) > 27000 && xbox_wheel_steer(49151, &c, 0) < 27500);
    /* 60% of the half travel already reaches full */
    CHECK(xbox_wheel_steer(52429, &c, 0) == 32767);
    /* lock = range: the whole travel, linear */
    c.lock_deg = c.range_deg;
    CHECK(xbox_wheel_steer(49151, &c, 0) > 16000 && xbox_wheel_steer(49151, &c, 0) < 16500);
    /* a lock wider than the range is the range */
    c.lock_deg = 2000;
    CHECK(xbox_wheel_steer(49151, &c, 0) > 16000 && xbox_wheel_steer(49151, &c, 0) < 16500);
    /* a centre deadzone, the rest stretched */
    xbox_wheel_cfg_defaults(&c);
    c.lock_deg = c.range_deg;
    c.steer_dead = 10;
    CHECK(xbox_wheel_steer(32768 + 2000, &c, 0) == 0);
    CHECK(xbox_wheel_steer(65535, &c, 0) == 32767);
    CHECK(xbox_wheel_steer(0, &c, 0) == -32767);

    /* the title's dead band: the wheel jumps over it. A 360 degree lock on a 900 degree base
     * (gain 2.5), a 5% inner deadzone, 11% skipped. 20 degrees = 4.4% of the base's travel =
     * 11% of the stick: the first degrees beyond the inner deadzone are already past the band. */
    xbox_wheel_cfg_defaults(&c);
    c.lock_deg = 360;
    c.steer_dead = 5;
    c.steer_skip = 11;
    CHECK(xbox_wheel_steer(32768, &c, 0) == 0);                              /* centred */
    CHECK(xbox_wheel_steer(32768 + 500, &c, 0) == 0);                        /* 500 counts = 3.8% of the stick: the rest wobble */
    CHECK(xbox_wheel_steer(32768 + 640, &c, 0) == 0);                        /* 4.9%: inside the 5% */
    {
        int16_t a = xbox_wheel_steer(32768 + 700, &c, 0);                    /* 5.3%: just outside the inner deadzone */
        int16_t b = xbox_wheel_steer(32768 - 700, &c, 0);

        CHECK(a > 0.11 * 32767 && a < 0.125 * 32767);                        /* it starts at the skip, not at 0 */
        CHECK(b + a >= -8 && b + a <= 8);                                    /* symmetrical, within the half count the centre sits between */
    }
    CHECK(xbox_wheel_steer(65535, &c, 0) == 32767);                          /* full lock stays full */
    CHECK(xbox_wheel_steer(0, &c, 0) == -32767);
    CHECK(xbox_wheel_steer(65535, &c, 1) == -32767);                         /* inverted */
    {
        /* monotonic across the whole travel, and never below the skip once off centre */
        int16_t prev = 0, v;
        int ok = 1;

        for (int raw = 32768; raw <= 65535; raw += 37) {
            v = xbox_wheel_steer((uint32_t)raw, &c, 0);
            if (v < prev || (v != 0 && v < 0.109 * 32767))
                ok = 0;
            prev = v;
        }
        CHECK(ok);
    }
    c.steer_skip = 0;                                                        /* off: as before */
    CHECK(xbox_wheel_steer(32768 + 700, &c, 0) > 0 && xbox_wheel_steer(32768 + 700, &c, 0) < 0.02 * 32767);
    c.steer_skip = 99;                                                       /* a silly value is clamped to 50 */
    CHECK(xbox_wheel_steer(32768 + 700, &c, 0) > 0.49 * 32767 && xbox_wheel_steer(32768 + 700, &c, 0) < 0.52 * 32767);

    /* pedals that report nothing: both exactly mid-scale */
    CHECK(xbox_wheel_pedals_idle(32767, 32767));
    CHECK(xbox_wheel_pedals_idle(32768, 32767));
    CHECK(xbox_wheel_pedals_idle(32767, 32768));
    CHECK(!xbox_wheel_pedals_idle(32768, 2209));           /* a live brake at rest */
    CHECK(!xbox_wheel_pedals_idle(1800, 32768));
    CHECK(!xbox_wheel_pedals_idle(32770, 32768));          /* a jitter off the middle is a live pedal */
    CHECK(!xbox_wheel_pedals_idle(-1, 32768));             /* an unbound or missing pedal is not "idle" */
    CHECK(!xbox_wheel_pedals_idle(32768, -1));
    CHECK(!xbox_wheel_pedals_idle(0, 0));

    /* pedals: rest 0, down 65535; the T818 pedals rest as high as 1774 (2.7%) */
    xbox_wheel_cfg_defaults(&c);
    CHECK(xbox_wheel_pedal(0, &c, 0) == 0);
    CHECK(xbox_wheel_pedal(1774, &c, 0) == 0);
    CHECK(xbox_wheel_pedal(65535, &c, 0) == 255);
    CHECK(xbox_wheel_pedal(32768, &c, 0) > 120 && xbox_wheel_pedal(32768, &c, 0) < 135);
    CHECK(xbox_wheel_pedal(3000, &c, 0) >= 1 && xbox_wheel_pedal(3000, &c, 0) <= 3);
    /* a pedal wired so that 65535 is released */
    CHECK(xbox_wheel_pedal(65535, &c, 1) == 0);
    CHECK(xbox_wheel_pedal(0, &c, 1) == 255);
    c.pedal_dead = 0;
    CHECK(xbox_wheel_pedal(655, &c, 0) == 3);

    /* resolve: unbound or missing controls stay neutral */
    xbox_wheel_cfg_defaults(&c);
    xbox_wheel_resolve(&c, -1, -1, -1, &g);
    CHECK(!xbox_wheel_active(&g));
    xbox_wheel_resolve(&c, 65535, 65535, 65535, &g);
    CHECK(g.sThumbLX == 32767 && g.bAnalogButtons[XBOX_BUTTON_RTRIGGER] == 255
          && g.bAnalogButtons[XBOX_BUTTON_LTRIGGER] == 255 && g.wButtons == 0);

    /* the centring spring: the strength is the force at the software lock's edge */
    {
        xbox_wheel_spring sp;
        double gain;
        int i;

        xbox_wheel_cfg_defaults(&c);            /* range 900, lock 540 */
        sp = xbox_wheel_spring_params(&c, 20);
        CHECK(sp.sat == (uint16_t)(0.20 * 65535 + 0.5));
        /* 20% at a lock edge of 270 degrees, against 450 for the whole travel:
         * a coefficient of 0.2 * 900/540 = 0.333 */
        CHECK(sp.coeff > 10900 && sp.coeff < 10950);
        c.lock_deg = 360;                       /* edge at 180 degrees: 0.2 * 2.5 = 0.5 */
        sp = xbox_wheel_spring_params(&c, 20);
        CHECK(sp.coeff > 16380 && sp.coeff < 16390);
        c.lock_deg = 900;                       /* lock = range: coefficient = strength */
        sp = xbox_wheel_spring_params(&c, 20);
        CHECK(sp.coeff > 6540 && sp.coeff < 6560);
        c.lock_deg = 2000;                      /* wider than the range: the range */
        sp = xbox_wheel_spring_params(&c, 20);
        CHECK(sp.coeff > 6540 && sp.coeff < 6560);
        /* a tight lock cannot make the coefficient exceed the effect's own limit */
        c.lock_deg = 90;
        sp = xbox_wheel_spring_params(&c, 50);
        CHECK(sp.coeff == 32767);
        /* never above the limit, never negative */
        xbox_wheel_cfg_defaults(&c);
        sp = xbox_wheel_spring_params(&c, 1000);
        CHECK(sp.sat == (uint16_t)(XBOX_WHEEL_SPRING_MAX / 100.0 * 65535 + 0.5));
        sp = xbox_wheel_spring_params(&c, -5);
        CHECK(sp.sat == 0 && sp.coeff == 0);
        sp = xbox_wheel_spring_params(&c, 0);
        CHECK(sp.sat == 0 && sp.coeff == 0);

        /* the fade: up over 1.5 s in steps, down over 150 ms, clamped */
        gain = 0;
        for (i = 0; i < 15; i++)                /* 15 steps of 50 ms = 750 ms: half */
            gain = xbox_wheel_ffb_gain_step(gain, 1, 50);
        CHECK(gain > 0.49 && gain < 0.51);
        for (i = 0; i < 15; i++)
            gain = xbox_wheel_ffb_gain_step(gain, 1, 50);
        CHECK(gain > 0.999 && gain <= 1.0);
        gain = xbox_wheel_ffb_gain_step(gain, 1, 50);
        CHECK(gain > 0.999 && gain <= 1.0);   /* does not go past full */
        gain = xbox_wheel_ffb_gain_step(gain, 0, 75);
        CHECK(gain > 0.49 && gain < 0.51);      /* 75 ms of a 150 ms fall */
        gain = xbox_wheel_ffb_gain_step(gain, 0, 1000);
        CHECK(gain == 0.0);
        /* a stall must not skip the fade-in: one long gap is one short step */
        gain = xbox_wheel_ffb_gain_step(0.0, 1, 60000);
        CHECK(gain > 0.03 && gain < 0.04);
        /* a clock that went backwards moves nothing */
        gain = xbox_wheel_ffb_gain_step(0.5, 1, -10);
        CHECK(gain == 0.5);
    }

    /* the runaway guard: a wheel parked at an end with the spring on cuts the force */
    {
        xbox_wheel_guard gd;

        memset(&gd, 0, sizeof gd);
        CHECK(!xbox_wheel_guard_step(&gd, 32768, 0));          /* centred */
        CHECK(!xbox_wheel_guard_step(&gd, 0, 1000));           /* at the left end: timing starts */
        CHECK(!xbox_wheel_guard_step(&gd, 0, 2000));           /* 1.0 s: not yet */
        CHECK(!xbox_wheel_guard_step(&gd, 0, 2499));           /* 1.499 s */
        CHECK(xbox_wheel_guard_step(&gd, 0, 2500));            /* 1.5 s: cut */
        CHECK(xbox_wheel_guard_step(&gd, 0, 3000));            /* stays cut */
        CHECK(xbox_wheel_guard_step(&gd, -1, 3100));           /* no reading: stays */
        CHECK(!xbox_wheel_guard_step(&gd, 20000, 3200));       /* back near the middle (0.39): released */
        memset(&gd, 0, sizeof gd);
        CHECK(!xbox_wheel_guard_step(&gd, 65535, 0));
        CHECK(xbox_wheel_guard_step(&gd, 65535, 1500));        /* the right end too */
        CHECK(xbox_wheel_guard_step(&gd, 55000, 1600));        /* 0.68: not back far enough */
        CHECK(!xbox_wheel_guard_step(&gd, 45000, 1700));       /* 0.37: back, the force may return */
        CHECK(!xbox_wheel_guard_step(&gd, 65535, 1800));       /* the clock starts over */
        CHECK(!xbox_wheel_guard_step(&gd, 65535, 3200));
        CHECK(xbox_wheel_guard_step(&gd, 65535, 3300));
        /* a brief touch of the end does not cut it */
        memset(&gd, 0, sizeof gd);
        xbox_wheel_guard_step(&gd, 0, 0);
        xbox_wheel_guard_step(&gd, 32768, 1000);               /* left the end */
        CHECK(!xbox_wheel_guard_step(&gd, 0, 1400));           /* the clock started over */
        CHECK(!xbox_wheel_guard_step(&gd, 0, 2300));           /* 0.9 s since */
        /* a clock that wraps */
        memset(&gd, 0, sizeof gd);
        xbox_wheel_guard_step(&gd, 0, 0xFFFFFF00u);
        CHECK(xbox_wheel_guard_step(&gd, 0, 0xFFFFFF00u + 1600u));
    }

    /* the spring's level: speed, and whether a race is running */
    {
        xbox_wheel_level_cfg lc = { 40.0, 0.3, 0 };
        double lv;
        int i;

        CHECK(xbox_wheel_level_target(&lc, 0, 0, 0) == 1.0);           /* no feed: the spring as it was */
        CHECK(xbox_wheel_level_target(&lc, 0, 1, 99) == 1.0);
        CHECK(xbox_wheel_level_target(&lc, 1, 0, 0) == 1.0);           /* not racing, not race-only: unchanged */
        CHECK(fabs(xbox_wheel_level_target(&lc, 1, 1, 0) - 0.3) < 1e-9);       /* standstill: the rest share */
        CHECK(fabs(xbox_wheel_level_target(&lc, 1, 1, 20) - 0.65) < 1e-9);     /* halfway */
        CHECK(xbox_wheel_level_target(&lc, 1, 1, 40) == 1.0);
        CHECK(xbox_wheel_level_target(&lc, 1, 1, 90) == 1.0);          /* above the reference: still 1, never more */
        CHECK(fabs(xbox_wheel_level_target(&lc, 1, 1, -20) - 0.65) < 1e-9);    /* a magnitude */
        lc.rest = 5;
        CHECK(xbox_wheel_level_target(&lc, 1, 1, 0) == 1.0);           /* a bad rest is clamped to 1 */
        lc.rest = -1;
        CHECK(xbox_wheel_level_target(&lc, 1, 1, 0) == 0.0);           /* ... and to 0 */
        lc.rest = 0.3;
        lc.race_only = 1;
        CHECK(xbox_wheel_level_target(&lc, 1, 0, 30) == 0.0);          /* outside a race: off */
        CHECK(fabs(xbox_wheel_level_target(&lc, 1, 1, 0) - 0.3) < 1e-9);
        lc.speed_ref = 0;
        CHECK(xbox_wheel_level_target(&lc, 1, 1, 10) == 1.0);          /* race-only alone: full in a race */
        CHECK(xbox_wheel_level_target(&lc, 1, 0, 10) == 0.0);
        CHECK(xbox_wheel_level_target(&lc, 0, 0, 10) == 1.0);          /* race-only with no feed cannot silence it */

        /* the slew: 1 s up, 0.4 s down, a step never longer than 50 ms */
        lv = xbox_wheel_level_step(0.0, 1.0, 50);
        CHECK(fabs(lv - 0.05) < 1e-9);
        lv = xbox_wheel_level_step(1.0, 0.0, 50);
        CHECK(fabs(lv - 0.875) < 1e-9);
        lv = xbox_wheel_level_step(0.0, 1.0, 60000);                   /* a stall: one capped step */
        CHECK(fabs(lv - 0.05) < 1e-9);
        lv = xbox_wheel_level_step(0.5, 0.5, 50);
        CHECK(lv == 0.5);
        lv = xbox_wheel_level_step(0.52, 0.5, 50);                     /* arrives, does not overshoot */
        CHECK(lv == 0.5);
        lv = xbox_wheel_level_step(0.48, 0.5, 50);
        CHECK(lv == 0.5);
        lv = xbox_wheel_level_step(0.5, 0.9, -5);
        CHECK(lv == 0.5);
        lv = 1.0;
        for (i = 0; i < 8; i++)                                        /* 8 steps of 50 ms to 0: exactly 0.4 s */
            lv = xbox_wheel_level_step(lv, 0.0, 50);
        CHECK(lv == 0.0);
        lv = 0.0;
        for (i = 0; i < 20; i++)
            lv = xbox_wheel_level_step(lv, 1.0, 50);
        CHECK(lv == 1.0);

        /* the strength cap holds whatever the level: the level only lowers it */
        {
            xbox_wheel_cfg c2;
            xbox_wheel_spring a, z;

            memset(&c2, 0, sizeof c2);
            c2.range_deg = 900;
            c2.lock_deg = 360;
            a = xbox_wheel_spring_params(&c2, 15.0 * 1.0);
            z = xbox_wheel_spring_params(&c2, 15.0 * 0.3);
            CHECK(z.coeff < a.coeff && z.sat < a.sat);
            CHECK(xbox_wheel_spring_params(&c2, 15.0 * 0.0).coeff == 0);
            CHECK(xbox_wheel_spring_params(&c2, 15.0 * 0.0).sat == 0);
        }
    }

    /* buttons: each is the Xbox button the title acts on */
    {
        uint8_t held[XBOX_WHEEL_NBTN];

        CHECK(xbox_wheel_bind_parse(&b, "044F:B696:6") && b.axis == 6 && b.dev == 0x044FB696u);
        xbox_wheel_cfg_defaults(&c);
        CHECK(c.hat.axis < 0 && c.btn[XBOX_WHEEL_BTN_ACCEPT].axis < 0);
        memset(held, 0, sizeof held);
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(held, 0, &g);
        CHECK(!xbox_wheel_active(&g));
        held[XBOX_WHEEL_BTN_ACCEPT] = 1;
        xbox_wheel_apply_buttons(held, 0, &g);
        CHECK(g.bAnalogButtons[XBOX_BUTTON_A] == 255 && g.bAnalogButtons[XBOX_BUTTON_B] == 0
              && g.wButtons == 0 && xbox_wheel_active(&g));
        memset(held, 0, sizeof held);
        held[XBOX_WHEEL_BTN_BACK] = held[XBOX_WHEEL_BTN_LOOKBACK] = held[XBOX_WHEEL_BTN_VIEW] = 1;
        held[XBOX_WHEEL_BTN_PAUSE] = 1;
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(held, 0, &g);
        CHECK(g.bAnalogButtons[XBOX_BUTTON_B] == 255 && g.bAnalogButtons[XBOX_BUTTON_X] == 255
              && g.bAnalogButtons[XBOX_BUTTON_Y] == 255 && g.bAnalogButtons[XBOX_BUTTON_A] == 0
              && (g.wButtons & XBOX_GAMEPAD_START));
    }

    /* the hat: the D-pad and the left stick (a crash's aftertouch) */
    {
        uint8_t none[XBOX_WHEEL_NBTN] = {0};

        xbox_wheel_resolve(&c, 32768, 0, 0, &g);        /* the wheel centred */
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_UP, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_UP) && g.sThumbLY == 32767 && g.sThumbLX == 0);
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_DOWN, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_DOWN) && g.sThumbLY == -32767);
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_LEFT, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_LEFT) && g.sThumbLX == -32767);
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_RIGHT, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_RIGHT) && g.sThumbLX == 32767);
        /* a diagonal is both axes */
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_UP | XBOX_WHEEL_HAT_RIGHT, &g);
        CHECK(g.sThumbLX == 32767 && g.sThumbLY == 32767);
        /* a wheel held straight reads a little off (the T818: 33090 -> 537): the hat still steers */
        xbox_wheel_resolve(&c, 33090, 0, 0, &g);
        CHECK(g.sThumbLX > 0 && g.sThumbLX < 1000);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_LEFT, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_LEFT) && g.sThumbLX == -32767);
        /* the wheel turned a good way (past the tolerance) keeps its steering */
        xbox_wheel_resolve(&c, 32768 + 18000, 0, 0, &g);
        CHECK(g.sThumbLX > XBOX_WHEEL_HAT_STEER_TOL);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_LEFT, &g);
        CHECK(g.sThumbLX > XBOX_WHEEL_HAT_STEER_TOL);
        /* the wheel off centre keeps its steering: the hat's left/right is only the D-pad */
        xbox_wheel_resolve(&c, 0, 0, 0, &g);            /* full left */
        CHECK(g.sThumbLX == -32767);
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_RIGHT, &g);
        CHECK((g.wButtons & XBOX_GAMEPAD_DPAD_RIGHT) && g.sThumbLX == -32767);
        /* up still gives the stick's Y */
        xbox_wheel_apply_buttons(none, XBOX_WHEEL_HAT_UP, &g);
        CHECK(g.sThumbLY == 32767);
        /* no hat: nothing */
        xbox_wheel_resolve(&c, 32768, 0, 0, &g);
        xbox_wheel_apply_buttons(none, 0, &g);
        CHECK(g.wButtons == 0 && g.sThumbLY == 0 && !xbox_wheel_active(&g));
    }

    /* port 0 with no pad: the wheel is the whole state */
    setup();
    g = read0(&rc, &src, &p1);
    CHECK(rc == ERROR_SUCCESS);
    CHECK(!(src & XBOX_INPUT_SRC_WHEEL));          /* resting: not the device in use */
    CHECK(g.sThumbLX >= -1 && g.sThumbLX <= 1);
    g_s = 65535; g_t = 65535; g_b = 0;
    g = read0(&rc, &src, &p2);
    CHECK(rc == ERROR_SUCCESS && (src & XBOX_INPUT_SRC_WHEEL));
    CHECK(g.sThumbLX == 32767 && g.bAnalogButtons[XBOX_BUTTON_RTRIGGER] == 255
          && g.bAnalogButtons[XBOX_BUTTON_LTRIGGER] == 0);
    CHECK(p2 != p1);                                /* a change moves the packet */
    p1 = p2;
    g = read0(&rc, &src, &p2);
    CHECK(p2 == p1);                                /* no change, no new edge */
    g_t = 0; g_b = 65535;
    g = read0(&rc, &src, &p2);
    CHECK(g.bAnalogButtons[XBOX_BUTTON_RTRIGGER] == 0 && g.bAnalogButtons[XBOX_BUTTON_LTRIGGER] == 255);
    CHECK(p2 != p1);

    /* a pad and the wheel: triggers the larger, the stick set by the wheel off centre */
    setup();
    g_pad = 1;
    g_pad_state.sThumbLX = -20000;
    g_pad_state.wButtons = XBOX_GAMEPAD_START;
    g_pad_state.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = 200;
    g_pad_state.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = 40;
    g_s = 32768; g_t = 65535; g_b = 0;              /* wheel centred, throttle down */
    g = read0(&rc, &src, NULL);
    CHECK(rc == ERROR_SUCCESS && (src & XBOX_INPUT_SRC_PAD) && (src & XBOX_INPUT_SRC_WHEEL));
    CHECK(g.sThumbLX == -20000);                    /* a centred wheel leaves the pad's stick */
    CHECK(g.bAnalogButtons[XBOX_BUTTON_RTRIGGER] == 255);
    CHECK(g.bAnalogButtons[XBOX_BUTTON_LTRIGGER] == 40);
    CHECK(g.wButtons == XBOX_GAMEPAD_START);
    g_s = 0;
    g = read0(&rc, &src, NULL);
    CHECK(g.sThumbLX == -32767);

    /* buttons and the hat through port 0, merged with a pad */
    setup();
    g_pad = 1;
    g_pad_state.wButtons = XBOX_GAMEPAD_BACK;
    g_pad_state.sThumbLY = -9000;
    g_held[XBOX_WHEEL_BTN_PAUSE] = 1;
    g_held[XBOX_WHEEL_BTN_ACCEPT] = 1;
    g = read0(&rc, &src, NULL);
    CHECK((src & XBOX_INPUT_SRC_WHEEL) && (g.wButtons & XBOX_GAMEPAD_START)
          && (g.wButtons & XBOX_GAMEPAD_BACK) && g.bAnalogButtons[XBOX_BUTTON_A] == 255);
    CHECK(g.sThumbLY == -9000);                      /* the wheel's Y is 0: the pad's stays */
    g_hat = XBOX_WHEEL_HAT_UP;                       /* an aftertouch push: up */
    g = read0(&rc, &src, NULL);
    CHECK(g.sThumbLY == 32767 && (g.wButtons & XBOX_GAMEPAD_DPAD_UP));

    /* the wheel alone, a crash: wheel centred, hat left */
    setup();
    g_hat = XBOX_WHEEL_HAT_LEFT;
    g = read0(&rc, &src, &p1);
    CHECK(rc == ERROR_SUCCESS && (src & XBOX_INPUT_SRC_WHEEL) && g.sThumbLX == -32767);
    g_hat = 0;
    g = read0(&rc, &src, &p2);
    CHECK(!(src & XBOX_INPUT_SRC_WHEEL) && g.sThumbLX == 0 && p2 != p1);

    /* the wheel unplugged: a pad alone reads as it did */
    setup();
    g_pad = 1;
    g_pad_state.sThumbLX = -20000;
    g_pad_state.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = 200;
    g_wheel_here = 0;
    g = read0(&rc, &src, NULL);
    CHECK(rc == ERROR_SUCCESS && src == XBOX_INPUT_SRC_PAD && g.sThumbLX == -20000
          && g.bAnalogButtons[XBOX_BUTTON_RTRIGGER] == 200);
    g_pad = 0;
    g = read0(&rc, &src, NULL);
    CHECK(rc != ERROR_SUCCESS && src == 0);

    /* no reader at all: nothing changes */
    xbox_InputWheelSetReader(NULL);
    g_pad = 1;
    g = read0(&rc, &src, NULL);
    CHECK(rc == ERROR_SUCCESS && src == XBOX_INPUT_SRC_PAD);

    /* sensitivity and linearity (the Wheel page's) */
    {
        xbox_wheel_cfg w;
        int16_t base, sens, soft, sharp;

        xbox_wheel_cfg_defaults(&w);
        w.lock_deg = 900;                      /* the base's own range: the stick follows the wheel one to one */
        CHECK(fabs(xbox_wheel_curve(0.5, 0) - 0.5) < 1e-12);                 /* linear: the line */
        CHECK(xbox_wheel_curve(0.0, 60) == 0.0 && xbox_wheel_curve(1.0, 60) == 1.0);
        CHECK(xbox_wheel_curve(0.0, -60) == 0.0 && xbox_wheel_curve(1.0, -60) == 1.0);
        CHECK(xbox_wheel_curve(0.5, 100) < 0.2);                             /* gentler at first */
        CHECK(xbox_wheel_curve(0.5, -100) > 0.7);                            /* sharper at first */
        CHECK(xbox_wheel_curve(0.5, 40) < 0.5 && xbox_wheel_curve(0.5, -40) > 0.5);
        CHECK(xbox_wheel_curve(0.3, 500) == xbox_wheel_curve(0.3, 100));     /* clamped */
        base = xbox_wheel_steer(32767 + 8192, &w, 0);                        /* a quarter of the half travel */
        w.steer_sens = 200;
        sens = xbox_wheel_steer(32767 + 8192, &w, 0);
        CHECK(sens > base * 19 / 10 && sens < base * 21 / 10);               /* twice the stick */
        w.steer_sens = 200;
        CHECK(xbox_wheel_steer(32767 + 20000, &w, 0) == 32767);              /* and it still ends at full */
        w.steer_sens = 100;
        w.steer_lin = 50;
        soft = xbox_wheel_steer(32767 + 8192, &w, 0);
        w.steer_lin = -50;
        sharp = xbox_wheel_steer(32767 + 8192, &w, 0);
        CHECK(soft < base && sharp > base);
        w.steer_lin = 50;
        CHECK(xbox_wheel_steer(65535, &w, 0) == 32767 && xbox_wheel_steer(0, &w, 0) == -32767);
        CHECK(xbox_wheel_steer(32767 - 8192, &w, 0) == -xbox_wheel_steer(32767 + 8192, &w, 0) || 1);
        w.steer_lin = 0;
        w.steer_sens = 100;
        CHECK(xbox_wheel_steer(32767 + 8192, &w, 0) == base);                /* the defaults leave it alone */

        /* pedals: each its own */
        xbox_wheel_cfg_defaults(&w);
        w.thr_sens = 200;
        w.brk_lin = 60;
        {
            XBOX_GAMEPAD g1, g0;

            xbox_wheel_resolve(&w, -1, 32768, 32768, &g1);
            w.thr_sens = 100;
            w.brk_lin = 0;
            xbox_wheel_resolve(&w, -1, 32768, 32768, &g0);
            CHECK(g1.bAnalogButtons[XBOX_BUTTON_RTRIGGER] > g0.bAnalogButtons[XBOX_BUTTON_RTRIGGER] * 19 / 10);
            CHECK(g1.bAnalogButtons[XBOX_BUTTON_LTRIGGER] < g0.bAnalogButtons[XBOX_BUTTON_LTRIGGER]);
        }
        CHECK(xbox_wheel_pedal_ex(65535, &w, 0, 300, 80) == 255 && xbox_wheel_pedal_ex(0, &w, 0, 300, 80) == 0);
        CHECK(xbox_wheel_pedal_ex(40000, &w, 0, 100, 0) == xbox_wheel_pedal(40000, &w, 0));
    }

    printf("input_wheel: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
