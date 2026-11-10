/* wheel_probe.h -- what the settings overlay's Wheel pages need from the SDL wheel glue (wheel_sdl.c): the joysticks that are there
 * with their live axes and buttons, and "press the control now" capture for the bindings (RECOMP_WHEEL_STEER and the others).
 *
 * The overlay does not link wheel_sdl.c (a host without SDL joysticks has none), so the glue registers a table of functions here
 * and the overlay asks for it: NULL when the host has no wheel support. Everything runs on the overlay's thread. The glue opens
 * its own handles to the joysticks (SDL counts them, so the game's are not disturbed) and lets them go when the pages are left
 * (close). Nothing here changes a setting: a captured binding comes back as the text of RECOMP_WHEEL_* ("044F:B696:0"). */
#ifndef XBOX_INPUT_WHEEL_PROBE_H
#define XBOX_INPUT_WHEEL_PROBE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XBOX_WHEEL_PROBE_DEVS 4
#define XBOX_WHEEL_PROBE_AXES 12

typedef struct xbox_wheel_probe_dev {
    unsigned vid, pid;
    char     name[40];
    int      axes, buttons, hats;               /* what the device has (axes clipped to XBOX_WHEEL_PROBE_AXES) */
    uint16_t axis[XBOX_WHEEL_PROBE_AXES];       /* each 0..65535, as the hardware reports */
    uint64_t down;                              /* button n down: bit n (the first 64) */
    int      hat;                               /* hat 0, as SDL_HAT_*: 1 up, 2 right, 4 down, 8 left */
    int      bound;                             /* a wheel binding names this device */
} xbox_wheel_probe_dev;

/* What the capture waits for. */
enum { XBOX_WHEEL_CAP_STEER,    /* an axis that moves (the wheel turned) */
       XBOX_WHEEL_CAP_PEDAL,    /* an axis that moves (a pedal pressed); one that rests high is marked inverted */
       XBOX_WHEEL_CAP_BUTTON,   /* a button pressed */
       XBOX_WHEEL_CAP_HAT };    /* the hat pushed in a direction */

typedef struct xbox_wheel_probe_api {
    /* Fills out[] (at most max) with the joysticks that are there, a wheel's rather than a game pad's, and returns how many.
     * Opens them on first use; new ones are seen within a second. */
    int  (*devices)(xbox_wheel_probe_dev *out, int max);
    /* Capture: start, then step each frame. step returns 0 while it waits (the controls are read for a moment first, so what
     * is held at the start does not count) and 1 with bind filled in ("VID:PID:N", pedals "!" when inverted). */
    void (*capture_start)(int kind);
    int  (*capture_step)(char *bind, size_t n);
    void (*capture_cancel)(void);
    /* Lets the probe's handles go (the pages are left, the overlay is shut). */
    void (*close)(void);
    /* What the game is given from the wheel now, as the pad: steering stick X (-32768..32767), throttle and brake (0..255).
     * Returns 1 while the wheel is on, 0 when it is not (then nothing is filled). */
    int  (*output)(int *steer, int *throttle, int *brake);
} xbox_wheel_probe_api;

void xbox_InputWheelSetProbe(const xbox_wheel_probe_api *api);      /* input_core.c; wheel_sdl.c registers */
const xbox_wheel_probe_api *xbox_InputWheelProbeApi(void);          /* NULL: none */

#ifdef __cplusplus
}
#endif

#endif /* XBOX_INPUT_WHEEL_PROBE_H */
