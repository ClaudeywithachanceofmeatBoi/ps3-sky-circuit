/*
 * Sky Circuit — native PS3 port.
 * Draws the road, car, ramps and bumps with tiny3d. Does not launch the browser.
 * Target: firmware 4.92, HEN 3.4.1 (NPDRM HDD game package).
 */
#include <io/pad.h>
#include <string.h>
#include <tiny3d.h>

#include "draw.h"
#include "game.h"
#include "sound.h"

static Game game;
static unsigned prev_bits;

static float stick_x(unsigned int raw)
{
    float v = ((int)raw - 128) / 128.0f;
    if (v > -0.35f && v < 0.35f) return 0.0f;
    if (v < -1.0f) v = -1.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

/* PS3 pads only report new data when something changed (len == 0 otherwise),
 * so keep the last good state per pad and use that while nothing new arrives. */
static padData last_data[MAX_PADS];
static int have_data[MAX_PADS];

static float pad_steer(const padData *d)
{
    float s = stick_x(d->ANA_L_H);
    if (d->BTN_LEFT) s = -1.0f;
    if (d->BTN_RIGHT) s = 1.0f;
    return s;
}

static void read_input(Input *in)
{
    padInfo info;
    const padData *pd[2] = { 0, 0 };
    int i, n = 0;
    unsigned bits = 0;

    memset(in, 0, sizeof(*in));
    memset(&info, 0, sizeof(info));
    ioPadGetInfo(&info);
    for (i = 0; i < MAX_PADS; i++) {
        padData fresh;
        if (!info.status[i]) {
            have_data[i] = 0;
            continue;
        }
        memset(&fresh, 0, sizeof(fresh));
        ioPadGetData(i, &fresh);
        if (fresh.len > 0) {
            last_data[i] = fresh;
            have_data[i] = 1;
        }
        if (!have_data[i]) continue;
        if (n < 2) pd[n] = &last_data[i];
        n++;
    }
    in->two_player = (n >= 2);

    for (i = 0; i < n && i < 2; i++) {
        const padData *d = pd[i];
        if (d->BTN_UP) bits |= 1u;
        if (d->BTN_DOWN) bits |= 2u;
        if (d->BTN_CROSS) bits |= 4u;
        if (d->BTN_START) bits |= 8u;
        if (d->BTN_TRIANGLE) bits |= 16u;
        if (d->BTN_CIRCLE || d->BTN_R1) in->hbrake = 1;
    }

    if (n == 1) {
        /* One controller: R2 gas, L2 reverse, Circle/L1/R1 handbrake, stick or D-pad steers */
        const padData *d = pd[0];
        if (d->BTN_R2) in->gas = 1;
        if (d->BTN_L2) in->brake = 1;
        if (d->BTN_L1) in->hbrake = 1;
        in->left = d->BTN_LEFT;
        in->right = d->BTN_RIGHT;
        in->steer = pad_steer(d);
    } else if (n >= 2) {
        /* Two controllers:
         *   Pad 1: R2 = gas,     stick/D-pad steers the FRONT wheels, L1 = reset front wheels
         *   Pad 2: R2/L2 = reverse, stick/D-pad steers the REAR wheels, L1 = reset rear wheels
         *   Circle/R1 on either pad = handbrake, Start = pause */
        const padData *a = pd[0];
        const padData *b = pd[1];
        if (a->BTN_R2) in->gas = 1;
        if (b->BTN_R2 || b->BTN_L2) in->brake = 1;
        in->left = a->BTN_LEFT;
        in->right = a->BTN_RIGHT;
        in->steer = pad_steer(a);
        in->steer2 = pad_steer(b);
        in->reset1 = a->BTN_L1;
        in->reset2 = b->BTN_L1;
    }

    in->press_up = (bits & 1u) && !(prev_bits & 1u);
    in->press_down = (bits & 2u) && !(prev_bits & 2u);
    in->press_confirm = (bits & 4u) && !(prev_bits & 4u);
    in->press_pause = (bits & 8u) && !(prev_bits & 8u);
    in->press_triangle = (bits & 16u) && !(prev_bits & 16u);
    prev_bits = bits;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tiny3d_Init(1024 * 1024);
    ioPadInit(7);
    snd_init();
    game_init(&game);

    while (1) {
        Input in;
        tiny3d_Clear(0xff70c5ce, TINY3D_CLEAR_ALL);

        tiny3d_AlphaTest(1, 0x10, TINY3D_ALPHA_FUNC_GEQUAL);
        tiny3d_BlendFunc(1,
            TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
            TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ZERO,
            TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD);

        read_input(&in);
        game_frame(&game, &in);
        draw_frame(&game);

        tiny3d_Flip();
    }
    return 0;
}
