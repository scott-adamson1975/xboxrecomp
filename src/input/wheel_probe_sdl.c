/**
 * xbox_input wheel -- the settings overlay's view of the joysticks (wheel_probe.h).
 *
 * Lists the joysticks that are there with their live axes, buttons and hat, and captures "the control the player operates now" for
 * a binding. It opens its own handles (SDL counts them, so the wheel glue's are not disturbed) and works whether or not a wheel is
 * bound yet: this is how the first binding is made. The overlay's thread calls it, a frame at a time while a Wheel page shows.
 *
 * A game controller (a pad SDL has a mapping for) is left out unless a wheel binding names it: the list is for wheels, pedals,
 * shifters and button boxes.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "wheel_pad.h"
#include "wheel_probe.h"

#define PR_WARM       3         /* reads of a freshly opened device that are not used */
#define PR_RESCAN_MS  500
#define PR_STEER_MOVE 12000     /* an axis has moved this far (of 65535) from where it rested: the wheel turned */
#define PR_PEDAL_MOVE 20000     /* ... a pedal pressed */
#define PR_HIGH_REST  49152     /* a pedal resting above this reads high at rest: inverted */

extern int xbox_InputWheelLastOutput(int *steer, int *throttle, int *brake);    /* wheel_sdl.c */

typedef struct pr_dev {
    uint32_t      id;           /* vid << 16 | pid; 0: free */
    SDL_Joystick *j;
    int           warm;
    uint16_t      base[XBOX_WHEEL_PROBE_AXES];
    uint64_t      base_down;
    int           base_hat;
} pr_dev;

static pr_dev g_pr[XBOX_WHEEL_PROBE_DEVS];
static Uint32 g_pr_scan;
static int g_cap = -1;          /* the kind being captured, -1 none */
static int g_cap_based;         /* the resting values are taken */

static int pr_bound(uint32_t id)
{
    return xbox_InputWheelClaims(id >> 16, id & 0xFFFF);
}

static void pr_close(void)
{
    for (int i = 0; i < XBOX_WHEEL_PROBE_DEVS; i++) {
        if (g_pr[i].j)
            SDL_JoystickClose(g_pr[i].j);
        memset(&g_pr[i], 0, sizeof g_pr[i]);
    }
    g_pr_scan = 0;
    g_cap = -1;
    g_cap_based = 0;
}

static void pr_update(void)
{
    if (!xbox_InputMainPumpActive())
        SDL_JoystickUpdate();
}

/* Opens the joysticks that are new, lets go of those that went. */
static void pr_scan(void)
{
    Uint32 now = SDL_GetTicks();
    int n, k;

    if (g_pr_scan && now - g_pr_scan < PR_RESCAN_MS)
        return;
    g_pr_scan = now ? now : 1;
    if (!SDL_WasInit(SDL_INIT_JOYSTICK) && SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0)
        return;
    for (int i = 0; i < XBOX_WHEEL_PROBE_DEVS; i++)
        if (g_pr[i].j && !SDL_JoystickGetAttached(g_pr[i].j)) {
            SDL_JoystickClose(g_pr[i].j);
            memset(&g_pr[i], 0, sizeof g_pr[i]);
        }
    n = SDL_NumJoysticks();
    for (k = 0; k < n; k++) {
        uint32_t id = (uint32_t)(SDL_JoystickGetDeviceVendor(k) << 16 | SDL_JoystickGetDeviceProduct(k));
        int have = 0, slot = -1;

        if (!id)
            continue;
        for (int i = 0; i < XBOX_WHEEL_PROBE_DEVS; i++) {
            if (g_pr[i].id == id)
                have = 1;
            else if (!g_pr[i].id && slot < 0)
                slot = i;
        }
        if (have || slot < 0)
            continue;
        if (SDL_IsGameController(k) && !pr_bound(id))
            continue;
        g_pr[slot].j = SDL_JoystickOpen(k);
        if (!g_pr[slot].j)
            continue;
        g_pr[slot].id = id;
        g_pr[slot].warm = PR_WARM;
        g_cap_based = 0;            /* a device came: the resting values are taken again */
    }
}

static void pr_read(const pr_dev *d, xbox_wheel_probe_dev *o)
{
    int n, i;
    const char *nm = SDL_JoystickName(d->j);

    memset(o, 0, sizeof *o);
    o->vid = d->id >> 16;
    o->pid = d->id & 0xFFFF;
    snprintf(o->name, sizeof o->name, "%s", nm ? nm : "joystick");
    o->axes = SDL_JoystickNumAxes(d->j);
    o->buttons = SDL_JoystickNumButtons(d->j);
    o->hats = SDL_JoystickNumHats(d->j);
    n = o->axes < XBOX_WHEEL_PROBE_AXES ? o->axes : XBOX_WHEEL_PROBE_AXES;
    for (i = 0; i < n; i++)
        o->axis[i] = (uint16_t)xbox_wheel_axis_u16(SDL_JoystickGetAxis(d->j, i));
    for (i = 0; i < o->buttons && i < 64; i++)
        if (SDL_JoystickGetButton(d->j, i))
            o->down |= (uint64_t)1 << i;
    o->hat = o->hats > 0 ? SDL_JoystickGetHat(d->j, 0) : 0;
    o->bound = pr_bound(d->id);
}

static int pr_devices(xbox_wheel_probe_dev *out, int max)
{
    int n = 0;

    pr_update();
    pr_scan();
    for (int i = 0; i < XBOX_WHEEL_PROBE_DEVS && n < max; i++)
        if (g_pr[i].j) {
            if (g_pr[i].warm > 0)
                g_pr[i].warm--;
            pr_read(&g_pr[i], &out[n++]);
        }
    return n;
}

static void pr_capture_start(int kind)
{
    g_cap = kind;
    g_cap_based = 0;
    g_pr_scan = 0;
    for (int i = 0; i < XBOX_WHEEL_PROBE_DEVS; i++)
        if (g_pr[i].j)
            g_pr[i].warm = PR_WARM;
}

static void pr_capture_cancel(void)
{
    g_cap = -1;
}

/* One frame of the capture. The devices are read in the slot order pr_devices fills them. */
static int pr_capture_step(char *bind, size_t cap)
{
    xbox_wheel_probe_dev cur[XBOX_WHEEL_PROBE_DEVS];
    int slot_of[XBOX_WHEEL_PROBE_DEVS];
    int n, i, k, a, warming = 0;
    int best_k = -1, best_a = -1, best_d = 0, best_base = 0;

    if (g_cap < 0)
        return 0;
    n = pr_devices(cur, XBOX_WHEEL_PROBE_DEVS);
    for (i = 0, k = 0; i < XBOX_WHEEL_PROBE_DEVS; i++)
        if (g_pr[i].j) {
            if (k < n)
                slot_of[k] = i;
            k++;
            if (g_pr[i].warm > 0)
                warming = 1;
        }
    if (warming || n == 0)
        return 0;
    /* The resting state: what is held or off-centre when the capture starts does not count. */
    if (!g_cap_based) {
        for (k = 0; k < n; k++) {
            pr_dev *d = &g_pr[slot_of[k]];

            memcpy(d->base, cur[k].axis, sizeof d->base);
            d->base_down = cur[k].down;
            d->base_hat = cur[k].hat;
        }
        g_cap_based = 1;
        return 0;
    }
    for (k = 0; k < n; k++) {
        pr_dev *d = &g_pr[slot_of[k]];
        const xbox_wheel_probe_dev *c = &cur[k];

        if (g_cap == XBOX_WHEEL_CAP_BUTTON) {
            uint64_t fresh = c->down & ~d->base_down;

            for (a = 0; a < 64; a++)
                if ((fresh >> a) & 1) {
                    snprintf(bind, cap, "%04X:%04X:%d", c->vid, c->pid, a);
                    g_cap = -1;
                    return 1;
                }
            d->base_down &= c->down;        /* one let go may be pressed afresh */
        } else if (g_cap == XBOX_WHEEL_CAP_HAT) {
            if (c->hats > 0 && c->hat && c->hat != d->base_hat) {
                snprintf(bind, cap, "%04X:%04X:0", c->vid, c->pid);
                g_cap = -1;
                return 1;
            }
            d->base_hat = c->hat;
        } else {
            int an = c->axes < XBOX_WHEEL_PROBE_AXES ? c->axes : XBOX_WHEEL_PROBE_AXES;
            int move = g_cap == XBOX_WHEEL_CAP_PEDAL ? PR_PEDAL_MOVE : PR_STEER_MOVE;

            for (a = 0; a < an; a++) {
                int diff = (int)c->axis[a] - (int)d->base[a];

                if (diff < 0)
                    diff = -diff;
                if (diff > move && diff > best_d) {
                    best_d = diff;
                    best_k = k;
                    best_a = a;
                    best_base = d->base[a];
                }
            }
        }
    }
    if (best_k >= 0) {
        const xbox_wheel_probe_dev *c = &cur[best_k];

        snprintf(bind, cap, "%04X:%04X:%d%s", c->vid, c->pid, best_a,
                 g_cap == XBOX_WHEEL_CAP_PEDAL && best_base > PR_HIGH_REST ? "!" : "");
        g_cap = -1;
        return 1;
    }
    return 0;
}

static int pr_output(int *steer, int *throttle, int *brake)
{
    return xbox_InputWheelLastOutput(steer, throttle, brake);
}

static const xbox_wheel_probe_api g_api = {
    pr_devices, pr_capture_start, pr_capture_step, pr_capture_cancel, pr_close, pr_output
};

/* Called from xbox_InputWheelInit (wheel_sdl.c), bound wheel or not. */
void xbox_InputWheelProbeRegister(void)
{
    xbox_InputWheelSetProbe(&g_api);
}
