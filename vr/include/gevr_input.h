/*
 * gevr_input.h - VR input state and the synthetic N64 pads it is turned into.
 *
 * The GoldenEye engine reads controllers through joyGetStickX/Y(pad) and
 * joyGetButtons(pad, mask), which ultimately index
 * g_ContDataPtr->samples[..].pads[pad]. Rather than rewrite the player code,
 * the VR layer synthesises two N64 pads and lets the engine's own
 * "2.4 Goodhead" dual-controller path consume them unchanged.
 *
 * Goodhead routing, as verified in src/game/bondview2.c:
 *   pad 0 (GEVR_PAD_AIM)  stick -> moveData.analogTurn  / analogPitch
 *   pad 1 (GEVR_PAD_MOVE) stick -> moveData.analogStrafe / analogWalk
 * and for the 2.3/2.4 branch the aim-mode hold comes from pad 0's Z while
 * pad 1's Z is the trigger the fire code reads.
 */
#ifndef GEVR_INPUT_H
#define GEVR_INPUT_H

#include "gevr_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------- N64 pad definitions */

/* Mirrors include/PR/os.h so the shim and the headless tests agree without
 * dragging libultra headers into the VR layer. Static-asserted in the shim. */
#define GEVR_N64_A          0x8000
#define GEVR_N64_B          0x4000
#define GEVR_N64_Z          0x2000
#define GEVR_N64_START      0x1000
#define GEVR_N64_DUP        0x0800
#define GEVR_N64_DDOWN      0x0400
#define GEVR_N64_DLEFT      0x0200
#define GEVR_N64_DRIGHT     0x0100
#define GEVR_N64_L          0x0020
#define GEVR_N64_R          0x0010
#define GEVR_N64_CUP        0x0008
#define GEVR_N64_CDOWN      0x0004
#define GEVR_N64_CLEFT      0x0002
#define GEVR_N64_CRIGHT     0x0001

/* The engine treats |stick| <= 5 as dead and subtracts 5 from what is left,
 * so the usable magnitude is 75 and anything we emit must clear the notch. */
#define GEVR_N64_STICK_MAX      80
#define GEVR_N64_STICK_NOTCH    5

/* In aim mode the engine only pans the crosshair past +/-60 on the aim pad
 * (bondview2.c: aimTurnLeftSpeed / speedVertaUp). Emitting into that band is
 * how controller-relative aiming reaches the gun. */
#define GEVR_N64_AIM_THRESHOLD  60

enum {
    GEVR_PAD_AIM  = 0, /* turn + pitch  */
    GEVR_PAD_MOVE = 1, /* strafe + walk */
    GEVR_PAD_COUNT
};

typedef struct gevr_n64_pad {
    signed char    stick_x;
    signed char    stick_y;
    unsigned short buttons;
} gevr_n64_pad;

/* ------------------------------------------------------- VR input state */

/* Abstract buttons, resolved from whichever interaction profile is bound. */
#define GEVR_BTN_A_RIGHT     (1u << 0)  /* A / index A / vive menu   */
#define GEVR_BTN_B_RIGHT     (1u << 1)  /* B / index B               */
#define GEVR_BTN_X_LEFT      (1u << 2)  /* X / index A               */
#define GEVR_BTN_Y_LEFT      (1u << 3)  /* Y / index B               */
#define GEVR_BTN_MENU        (1u << 4)  /* left menu / system-safe   */
#define GEVR_BTN_STICK_LEFT  (1u << 5)  /* thumbstick click, left    */
#define GEVR_BTN_STICK_RIGHT (1u << 6)  /* thumbstick click, right   */
#define GEVR_BTN_TRIGGER_L   (1u << 7)  /* digitised from analog     */
#define GEVR_BTN_TRIGGER_R   (1u << 8)
#define GEVR_BTN_GRIP_L      (1u << 9)
#define GEVR_BTN_GRIP_R      (1u << 10)

typedef struct gevr_input_state {
    /* Thumbsticks, already in the -1..1 unit square from the runtime. */
    float move_x, move_y;   /* left  */
    float turn_x, turn_y;   /* right */

    float trigger_l, trigger_r; /* 0..1 */
    float grip_l, grip_r;       /* 0..1 */

    unsigned buttons;           /* GEVR_BTN_* held this frame */

    /* Poses in the stage/local reference space. */
    gevr_pose head;
    gevr_pose hand_l;
    gevr_pose hand_r;

    int head_valid;
    int hand_l_valid;
    int hand_r_valid;

    float dt; /* seconds since the previous update */
} gevr_input_state;

/* Haptics the mapper asks for; the XR layer plays them. */
typedef struct gevr_haptic_request {
    int   left;         /* pulse the left hand  */
    int   right;        /* pulse the right hand */
    float amplitude;    /* 0..1 */
    float duration;     /* seconds */
} gevr_haptic_request;

#ifdef __cplusplus
}
#endif

#endif /* GEVR_INPUT_H */
