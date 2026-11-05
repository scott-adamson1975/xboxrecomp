/* overlay_wheel.inc -- the racing wheel's pages in the settings overlay, included by overlay_ui.cpp (declared in overlay_wheel.h).
 *
 *   Wheel       the bindings (press the control now: the steering axis, the pedals, the hat and the buttons), then the settings
 *               of xbox_settings_wheel_set.inc (rotation, sensitivity, linearity, force feedback).
 *   Wheel test  every joystick that is there with its live axes, buttons and hat, and below them what the game is given from the
 *               wheel (after the lock, the sensitivity and the linearity), to check the bindings and the curves by eye.
 *
 * The joysticks come from the SDL glue through wheel_probe.h; where the host has none the pages say so. A binding is the text of
 * a RECOMP_WHEEL_* variable ("044F:B696:0"), kept as the text setting wheel.bind_* (xbox_settings_wheel_text.inc) and applied
 * after a restart; a variable set in a launcher still wins and locks the row. */
#include "wheel_probe.h"

struct WheelBind {
    const char    *key;         /* the text setting */
    recomp_env_id  env;         /* its variable, named in the hint of a locked row */
    const char    *label;
    int            cap;         /* XBOX_WHEEL_CAP_* */
    const char    *what;        /* the waiting row's prompt */
};

static const WheelBind k_wbind[] = {
    { "wheel.bind_steer",    RENV_INPUT_WHEEL_STEER,        "Steering axis",       XBOX_WHEEL_CAP_STEER,  "Turn the wheel" },
    { "wheel.bind_throttle", RENV_INPUT_WHEEL_THROTTLE,     "Throttle pedal",      XBOX_WHEEL_CAP_PEDAL,  "Press the throttle" },
    { "wheel.bind_brake",    RENV_INPUT_WHEEL_BRAKE,        "Brake pedal",         XBOX_WHEEL_CAP_PEDAL,  "Press the brake" },
    { "wheel.bind_hat",      RENV_INPUT_WHEEL_HAT,          "D-pad (hat)",         XBOX_WHEEL_CAP_HAT,    "Push the D-pad" },
    { "wheel.bind_accept",   RENV_INPUT_WHEEL_BTN_ACCEPT,   "Accept (A, boost)",   XBOX_WHEEL_CAP_BUTTON, "Press a button" },
    { "wheel.bind_back",     RENV_INPUT_WHEEL_BTN_BACK,     "Back (B, crashbreaker)", XBOX_WHEEL_CAP_BUTTON, "Press a button" },
    { "wheel.bind_pause",    RENV_INPUT_WHEEL_BTN_PAUSE,    "Pause (Start)",       XBOX_WHEEL_CAP_BUTTON, "Press a button" },
    { "wheel.bind_lookback", RENV_INPUT_WHEEL_BTN_LOOKBACK, "Look back (X)",       XBOX_WHEEL_CAP_BUTTON, "Press a button" },
    { "wheel.bind_view",     RENV_INPUT_WHEEL_BTN_VIEW,     "Change view (Y)",     XBOX_WHEEL_CAP_BUTTON, "Press a button" },
    { "wheel.bind_recentre", RENV_INPUT_WHEEL_BTN_RECENTRE, "Recentre view (R3)",  XBOX_WHEEL_CAP_BUTTON, "Press a button" },
};
#define WHEEL_NBIND ((int)(sizeof k_wbind / sizeof k_wbind[0]))
#define WHEEL_CAP_MS 10000

static int s_wb_cap = -1;               /* the binding being captured, -1 none */
static uint32_t s_wb_start;
static bool s_wb_open;                  /* the probe's handles are open (a Wheel page shows) */
static char s_wb_msg[160];              /* what the last capture did */
static xbox_wheel_probe_dev s_wd[XBOX_WHEEL_PROBE_DEVS];
static int s_wd_n;

/* The live rows (R_LIVE): the row's action indexes these. */
#define WHEEL_LIVE_MAX 48
struct WLive { float v; bool bar, bipolar; char label[56], val[64]; };
static WLive s_wl[WHEEL_LIVE_MAX];

static const xbox_wheel_probe_api *wheel_api() { return xbox_InputWheelProbeApi(); }

static bool wheel_capturing() { return s_wb_cap >= 0; }

/* The device named "VID:PID" as the player knows it: its name when it is there, else the ids. */
static void wheel_dev_name(unsigned vid, unsigned pid, char *out, size_t n, bool *there)
{
    *there = false;
    for (int i = 0; i < s_wd_n; i++)
        if (s_wd[i].vid == vid && s_wd[i].pid == pid) {
            snprintf(out, n, "%.18s", s_wd[i].name);
            *there = true;
            return;
        }
    snprintf(out, n, "%04X:%04X", vid, pid);
}

/* A binding's text for its row: "T818 axis 0", "Not set", "Waiting...". */
static void wheel_bind_value(int idx, char *out, size_t n, int *locked)
{
    const WheelBind &b = k_wbind[idx];
    const char *v = xbox_SettingsText(b.key, locked);
    unsigned vid, pid;
    int num;
    char tail = 0, dev[40];
    bool there;

    if (s_wb_cap == idx) {
        int left = (int)((WHEEL_CAP_MS - (now_ms() - s_wb_start) + 999) / 1000);
        snprintf(out, n, "%s... %d", b.what, left < 0 ? 0 : left);
        return;
    }
    if (!v || !*v) {
        snprintf(out, n, "Not set");
        return;
    }
    if (sscanf(v, "%x:%x:%d%c", &vid, &pid, &num, &tail) < 3) {
        snprintf(out, n, "%.40s", v);
        return;
    }
    wheel_dev_name(vid, pid, dev, sizeof dev, &there);
    snprintf(out, n, "%s %s %d%s%s", dev, b.cap == XBOX_WHEEL_CAP_BUTTON ? "button"
             : b.cap == XBOX_WHEEL_CAP_HAT ? "hat" : "axis", num, tail == '!' ? " (inverted)" : "",
             !there && s_wd_n > 0 ? " (not found)" : "");
}

static void wheel_bind_activate(int idx)
{
    const xbox_wheel_probe_api *api = wheel_api();
    int locked = 0;

    xbox_SettingsText(k_wbind[idx].key, &locked);
    if (!api || locked)
        return;
    if (s_wb_cap == idx) {              /* pressed again: stop waiting */
        api->capture_cancel();
        s_wb_cap = -1;
        return;
    }
    api->capture_start(k_wbind[idx].cap);
    s_wb_cap = idx;
    s_wb_start = now_ms();
    s_wb_msg[0] = 0;
}

static void wheel_bind_clear(int idx)
{
    int locked = 0;

    xbox_SettingsText(k_wbind[idx].key, &locked);
    if (locked || !xbox_SettingsText(k_wbind[idx].key, NULL)[0])
        return;
    xbox_SettingsSetText(k_wbind[idx].key, "");
    changed();
    xbox_OverlayWantRestart("The wheel's bindings changed");
    snprintf(s_wb_msg, sizeof s_wb_msg, "%s: cleared. Applies after a restart.", k_wbind[idx].label);
}

/* The Wheel test page is there only while a wheel is on and its steering axis is bound. */
static bool wheel_page_shown(int page)
{
    (void)page;
    return xbox_SettingValue(XS_WHEEL_ENABLE) && xbox_SettingsText("wheel.bind_steer", NULL)[0] != 0;
}

static bool wheel_cancel_capture()
{
    if (s_wb_cap < 0)
        return false;
    if (wheel_api())
        wheel_api()->capture_cancel();
    s_wb_cap = -1;
    return true;
}

static void wheel_close()
{
    if (s_wb_open && wheel_api())
        wheel_api()->close();
    s_wb_open = false;
    s_wb_cap = -1;
    s_wd_n = 0;
}

/* Once a frame while the pages show (not while the folder browser or the mixer cover them). */
static void wheel_frame()
{
    const xbox_wheel_probe_api *api = wheel_api();

    if (!api)
        return;
    if (s_page != P_WHEEL && s_page != P_WHEELTEST) {
        wheel_close();
        return;
    }
    s_wb_open = true;
    if (s_wb_cap >= 0) {
        char bind[48];

        if (api->capture_step(bind, sizeof bind)) {
            const WheelBind &b = k_wbind[s_wb_cap];
            char shown[96];
            int locked = 0;

            xbox_SettingsText(b.key, &locked);
            if (!locked)
                xbox_SettingsSetText(b.key, bind);
            s_wb_cap = -1;
            changed();
            xbox_OverlayWantRestart("The wheel's bindings changed");
            s_wd_n = api->devices(s_wd, XBOX_WHEEL_PROBE_DEVS);
            wheel_bind_value((int)(&b - k_wbind), shown, sizeof shown, &locked);
            snprintf(s_wb_msg, sizeof s_wb_msg, "%s: %s. Applies after a restart.", b.label, shown);
            menusnd_play(XBOX_OVERLAY_SND_CHANGE);
        } else if ((uint32_t)(now_ms() - s_wb_start) > (uint32_t)WHEEL_CAP_MS) {
            api->capture_cancel();
            snprintf(s_wb_msg, sizeof s_wb_msg, "%s: nothing moved, so nothing was changed.", k_wbind[s_wb_cap].label);
            s_wb_cap = -1;
        }
        return;
    }
    s_wd_n = api->devices(s_wd, XBOX_WHEEL_PROBE_DEVS);
}

/* The held keys do nothing while a capture waits (a press may be the control being picked). */
static void wheel_swallow_input()
{
    if (s_wb_cap < 0)
        return;
    memset(s_steps, 0, sizeof s_steps);
    s_activate = s_reset = s_page_delta = 0;
}

/* ── the rows ──────────────────────────────────────────────────────── */

static int wheel_live_add(Row *r, int n, int *k, const char *label, float v, bool bar, bool bipolar, const char *val)
{
    if (*k >= WHEEL_LIVE_MAX || n >= OVERLAY_ROWS - 6)
        return n;
    WLive &w = s_wl[*k];
    w.v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
    w.bar = bar;
    w.bipolar = bipolar;
    snprintf(w.label, sizeof w.label, "%s", label);
    snprintf(w.val, sizeof w.val, "%s", val);
    r[n++] = Row{ R_LIVE, XS_COUNT, *k, w.label };
    (*k)++;
    return n;
}

static int wheel_rows(int page, Row *r)
{
    int n = 0;
    const xbox_wheel_probe_api *api = wheel_api();
    static char keys_note[200], dev_line[XBOX_WHEEL_PROBE_DEVS][96];
#define WINFO(t) r[n++] = Row{ R_INFO, XS_COUNT, 0, (t) }
#define WSET(i)  r[n++] = Row{ R_SETTING, (i), 0, NULL }

    if (page == P_WHEEL) {
        const bool steer = xbox_SettingsText("wheel.bind_steer", NULL)[0] != 0;

        WSET(XS_WHEEL_ENABLE);
        if (!xbox_SettingValue(XS_WHEEL_ENABLE)) {
            WINFO("The wheel is off: it is not used, whatever is set up. Turn Use the wheel on (applies after a restart) to see its settings.");
            return n;
        }
        if (s_wb_msg[0])
            WINFO(s_wb_msg);
        if (!api)
            WINFO("This build has no wheel support (the SDL host has). The settings below do nothing here.");
        else if (steer)
            WINFO("A wheel is set up. Changes here apply after a restart; the Wheel test page shows the controls live.");
        else
            WINFO("No wheel is set up yet: pick Steering axis below, then turn the wheel. Changes apply after a restart.");
        if (api) {
            snprintf(keys_note, sizeof keys_note, "A binding: %s starts it, then use the control. %s cancels, %s clears it.",
                     act_name(0, true), act_name(1, true), act_name(3, true));
            WINFO(keys_note);
            for (int i = 0; i < WHEEL_NBIND; i++)
                r[n++] = Row{ R_BIND, XS_COUNT, i, k_wbind[i].label };
        }
        WSET(XS_WHEEL_RANGE); WSET(XS_WHEEL_LOCK);
        WSET(XS_WHEEL_STEER_SENS); WSET(XS_WHEEL_STEER_LIN); WSET(XS_WHEEL_STEER_DEAD); WSET(XS_WHEEL_STEER_SKIP);
        WSET(XS_WHEEL_PEDAL_DEAD);
        WSET(XS_WHEEL_THROTTLE_SENS); WSET(XS_WHEEL_THROTTLE_LIN);
        WSET(XS_WHEEL_BRAKE_SENS); WSET(XS_WHEEL_BRAKE_LIN);
        WSET(XS_WHEEL_FFB_SPRING);
        if (xbox_SettingValue(XS_WHEEL_FFB_SPRING) > 0) {
            WSET(XS_WHEEL_FFB_SPEED); WSET(XS_WHEEL_FFB_REST); WSET(XS_WHEEL_FFB_RACE_ONLY);
            WSET(XS_WHEEL_FFB_AIR); WSET(XS_WHEEL_FFB_DRIFT);
            WSET(XS_WHEEL_RUMBLE); WSET(XS_WHEEL_ENGINE); WSET(XS_WHEEL_KICK); WSET(XS_WHEEL_KICK_FLIP);
        }
        r[n++] = Row{ R_ACTION, XS_COUNT, A_RESET_PAGE, "Reset this page's settings to the defaults" };
        return n;
    }

    /* P_WHEELTEST */
    {
        int k = 0, ax, outs[3];
        char v[64];

        WINFO("Move the wheel, press the pedals and the buttons: each shows here as it moves.");
        if (!api) {
            WINFO("This build has no wheel support (the SDL host has).");
        } else if (s_wd_n == 0) {
            WINFO("No wheel, pedals or other joystick found yet. Plug it in: this list updates by itself.");
        }
        for (int d = 0; d < s_wd_n; d++) {
            const xbox_wheel_probe_dev &dv = s_wd[d];
            const int shown = dv.axes < 8 ? dv.axes : 8;

            if (n >= OVERLAY_ROWS - 8)
                break;
            snprintf(dev_line[d], sizeof dev_line[d], "%s (%04X:%04X): %d axes, %d buttons, %d hats%s", dv.name, dv.vid, dv.pid,
                     dv.axes, dv.buttons, dv.hats, dv.bound ? ", used by the wheel" : "");
            WINFO(dev_line[d]);
            for (ax = 0; ax < shown; ax++) {
                char lab[56];

                snprintf(lab, sizeof lab, "Axis %d", ax);
                snprintf(v, sizeof v, "%d%%", (int)(dv.axis[ax] * 100.0f / 65535.0f + 0.5f));
                n = wheel_live_add(r, n, &k, lab, dv.axis[ax] / 65535.0f, true, false, v);
            }
            {
                char list[64] = "";
                size_t at = 0;

                for (int b = 0; b < 64 && b < dv.buttons && at + 6 < sizeof list; b++)
                    if ((dv.down >> b) & 1)
                        at += (size_t)snprintf(list + at, sizeof list - at, "%s%d", at ? " " : "", b);
                if (dv.hat && at + 12 < sizeof list)
                    snprintf(list + at, sizeof list - at, "%shat %s%s%s%s", at ? "  " : "",
                             dv.hat & 1 ? "up " : "", dv.hat & 4 ? "down " : "", dv.hat & 8 ? "left " : "",
                             dv.hat & 2 ? "right" : "");
                n = wheel_live_add(r, n, &k, "Buttons down", 0.0f, false, false, list[0] ? list : "none");
            }
        }
        if (api && api->output(&outs[0], &outs[1], &outs[2])) {
            WINFO("What the game is given (after the lock, sensitivity and linearity set on the Wheel page):");
            snprintf(v, sizeof v, "%+d%%", (int)(outs[0] * 100.0f / 32767.0f));
            n = wheel_live_add(r, n, &k, "Steering stick", outs[0] / 32767.0f, true, true, v);
            snprintf(v, sizeof v, "%d%%", (int)(outs[1] * 100.0f / 255.0f + 0.5f));
            n = wheel_live_add(r, n, &k, "Throttle", outs[1] / 255.0f, true, false, v);
            snprintf(v, sizeof v, "%d%%", (int)(outs[2] * 100.0f / 255.0f + 0.5f));
            n = wheel_live_add(r, n, &k, "Brake", outs[2] / 255.0f, true, false, v);
        } else if (api) {
            WINFO("The game is given nothing from the wheel yet: bind the steering axis on the Wheel page and restart.");
        }
    }
    return n;
#undef WSET
#undef WINFO
}

static bool wheel_hint(const Row &r, char *hint, size_t n)
{
    hint[0] = 0;
    if (r.kind == R_BIND) {
        const WheelBind &b = k_wbind[r.action];
        int locked = 0;

        xbox_SettingsText(b.key, &locked);
        if (locked) {
            snprintf(hint, n, "Set by the environment (%s); unset it to change it here.", recomp_env_name(b.env));
            return true;
        }
        if (s_wb_cap == r.action)
            snprintf(hint, n, "%s now. %s stops waiting.", b.what, act_name(1, true));
        else
            snprintf(hint, n, "%s starts it: then %s. %s clears the binding. Applies after a restart. It is the text of %s.",
                     act_name(0, true), b.cap == XBOX_WHEEL_CAP_BUTTON ? "press the button you want"
                     : b.cap == XBOX_WHEEL_CAP_HAT ? "push the D-pad" : b.cap == XBOX_WHEEL_CAP_STEER
                     ? "turn the wheel a good way" : "press the pedal all the way", act_name(3, true), recomp_env_name(b.env));
    } else if (r.kind == R_LIVE) {
        snprintf(hint, n, "Live: this follows the control as you use it. The axis number is what the bindings use.");
    }
    return false;
}
