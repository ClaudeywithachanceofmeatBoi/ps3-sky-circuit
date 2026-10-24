#include "sound.h"

#include <math.h>
#include <string.h>
#include <audio/audio.h>
#include <sys/thread.h>
#include <sys/event_queue.h>

#define SR 48000.0f
#define TWO_PI 6.28318530718f
#define MASTER 2.0f
#define MAX_VOICES 10

enum { E_BOOST = 1, E_BEEP, E_EXPLOSION, E_BUMP, E_JUMP, E_FALL };
enum { W_SINE, W_TRI, W_SAW, W_SQUARE, W_NOISE };

typedef struct {
    int active, wave;
    float t;            /* seconds elapsed */
    float dur;          /* total length */
    float f0, f1, fdur; /* frequency: exponential f0 -> f1 over fdur */
    float g0, g1, gdur; /* gain: g0 -> g1 over gdur (exp if gexp, else linear) */
    int gexp;
    float decay;        /* >0: extra exp(-t/decay) amplitude (noise) */
    float lpf0, lpf1, lpdur; /* >0: lowpass cutoff exp lpf0 -> lpf1 */
    float ph, lp;
} Voice;

typedef struct { int type; float p; } Ev;

static volatile Ev evq[32];
static volatile unsigned ev_head, ev_tail;

static volatile float t_eng_freq, t_eng_gain, t_scr_level;
static volatile int snd_ok;

static Voice voices[MAX_VOICES];

static u64 snd_key;
static sys_event_queue_t snd_queue;
static audioPortConfig config;
static sys_ppu_thread_t snd_tid;

/* ---------- game-thread API ---------- */

static void push(int type, float p)
{
    unsigned h = ev_head;
    if (!snd_ok) return;
    if (h - ev_tail >= 32) return;
    evq[h & 31].type = type;
    evq[h & 31].p = p;
    __sync_synchronize();
    ev_head = h + 1;
}

void snd_engine(float speed_ratio, int accelerating)
{
    float r = fabsf(speed_ratio);
    float rc = r < 1.5f ? r : 1.5f;
    t_eng_freq = 40.0f + rc * 110.0f + (accelerating ? 15.0f : 0.0f);
    t_eng_gain = r > 0.02f ? 0.04f + rc * 0.05f : 0.02f;
}

void snd_engine_stop(void) { t_eng_gain = 0.0f; }

void snd_screech(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    t_scr_level = level;
}

void snd_boost(void) { push(E_BOOST, 0.0f); }
void snd_beep(float progress) { push(E_BEEP, progress); }
void snd_explosion(void) { push(E_EXPLOSION, 0.0f); }
void snd_bump(void) { push(E_BUMP, 0.0f); }
void snd_jump(void) { push(E_JUMP, 0.0f); }
void snd_fall(void) { push(E_FALL, 0.0f); }

/* ---------- audio thread ---------- */

static unsigned rng_state = 12345u;
static float noise(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (float)((rng_state >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

static Voice *new_voice(void)
{
    int i;
    for (i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            memset(&voices[i], 0, sizeof(Voice));
            voices[i].active = 1;
            return &voices[i];
        }
    }
    return 0;
}

static void start_event(int type, float p)
{
    Voice *v = new_voice();
    if (!v) return;
    switch (type) {
    case E_BOOST:
        v->wave = W_SAW; v->dur = 0.4f;
        v->f0 = 150.0f; v->f1 = 700.0f; v->fdur = 0.35f;
        v->g0 = 0.14f; v->g1 = 0.0f; v->gdur = 0.4f;
        break;
    case E_BEEP:
        v->wave = W_SQUARE; v->dur = 0.06f;
        v->f0 = 1600.0f + p * 1400.0f; v->f1 = 1000.0f + p * 1000.0f; v->fdur = 0.06f;
        v->g0 = 0.08f + p * 0.12f; v->g1 = 0.0f; v->gdur = 0.06f;
        break;
    case E_BUMP:
        v->wave = W_TRI; v->dur = 0.12f;
        v->f0 = 80.0f; v->f1 = 20.0f; v->fdur = 0.12f;
        v->g0 = 0.15f; v->g1 = 0.0f; v->gdur = 0.12f;
        break;
    case E_JUMP:
        v->wave = W_SINE; v->dur = 0.3f;
        v->f0 = 120.0f; v->f1 = 320.0f; v->fdur = 0.3f;
        v->g0 = 0.15f; v->g1 = 0.0f; v->gdur = 0.3f;
        break;
    case E_FALL:
        v->wave = W_SAW; v->dur = 0.4f;
        v->f0 = 200.0f; v->f1 = 30.0f; v->fdur = 0.4f;
        v->g0 = 0.12f; v->g1 = 0.0f; v->gdur = 0.4f;
        break;
    case E_EXPLOSION: {
        Voice *s;
        v->wave = W_NOISE; v->dur = 2.5f;
        v->decay = 0.6f;
        v->lpf0 = 400.0f; v->lpf1 = 40.0f; v->lpdur = 2.0f;
        v->g0 = 0.9f; v->g1 = 0.0001f; v->gdur = 2.4f; v->gexp = 1;
        s = new_voice();
        if (s) {
            s->wave = W_SINE; s->dur = 0.8f;
            s->f0 = 120.0f; s->f1 = 30.0f; s->fdur = 0.8f;
            s->g0 = 0.7f; s->g1 = 0.001f; s->gdur = 0.8f; s->gexp = 1;
        }
        break;
    }
    default:
        v->active = 0;
    }
}

static float osc(int wave, float ph)
{
    switch (wave) {
    case W_SINE: return sinf(ph * TWO_PI);
    case W_TRI: return 4.0f * fabsf(ph - 0.5f) - 1.0f;
    case W_SAW: return 2.0f * ph - 1.0f;
    default: return ph < 0.5f ? 1.0f : -1.0f;
    }
}

static float voice_sample(Voice *v)
{
    float s, g, f, k;
    float dt = 1.0f / SR;

    if (v->t >= v->dur) { v->active = 0; return 0.0f; }

    k = v->t / v->gdur; if (k > 1.0f) k = 1.0f;
    if (v->gexp) g = v->g0 * powf(v->g1 / v->g0, k);
    else g = v->g0 + (v->g1 - v->g0) * k;

    if (v->wave == W_NOISE) {
        float a;
        s = noise() * expf(-v->t / v->decay);
        k = v->t / v->lpdur; if (k > 1.0f) k = 1.0f;
        f = v->lpf0 * powf(v->lpf1 / v->lpf0, k);
        a = 1.0f - expf(-TWO_PI * f / SR);
        v->lp += (s - v->lp) * a;
        s = v->lp;
    } else {
        k = v->t / v->fdur; if (k > 1.0f) k = 1.0f;
        f = v->f0 * powf(v->f1 / v->f0, k);
        v->ph += f * dt;
        if (v->ph >= 1.0f) v->ph -= 1.0f;
        s = osc(v->wave, v->ph);
    }
    v->t += dt;
    return s * g;
}

/* engine / screech state (audio thread only) */
static float e_ph1, e_ph2, e_freq = 40.0f, e_gain, e_lp1, e_lp2;
static float s_gain, s_x1, s_x2, s_y1, s_y2;

static void fill_block(float *buf)
{
    int i, n;
    float a_eng, a_f1, a_f2;
    float tg_f, tg_g, lvl;
    float b0, b2, a1, a2, k_sg;
    float w0, alpha, cw, fc;

    /* drain events */
    while (ev_tail != ev_head) {
        Ev e;
        e.type = evq[ev_tail & 31].type;
        e.p = evq[ev_tail & 31].p;
        ev_tail++;
        start_event(e.type, e.p);
    }

    tg_f = t_eng_freq; tg_g = t_eng_gain; lvl = t_scr_level;
    a_eng = 1.0f - expf(-1.0f / (0.08f * SR));
    a_f1 = 1.0f - expf(-TWO_PI * 450.0f / SR); /* two cascaded one-poles ~ 450 Hz lowpass */
    a_f2 = a_f1;
    k_sg = 1.0f - expf(-1.0f / ((lvl > 0.0f ? 0.03f : 0.05f) * SR));

    /* screech bandpass (Q = 4), centre follows level */
    fc = 1800.0f + lvl * 1000.0f;
    w0 = TWO_PI * fc / SR;
    alpha = sinf(w0) / (2.0f * 4.0f);
    cw = cosf(w0);
    b0 = alpha / (1.0f + alpha);
    b2 = -alpha / (1.0f + alpha);
    a1 = -2.0f * cw / (1.0f + alpha);
    a2 = (1.0f - alpha) / (1.0f + alpha);

    for (n = 0; n < AUDIO_BLOCK_SAMPLES; n++) {
        float out = 0.0f, x, y;

        /* engine */
        e_freq += (tg_f - e_freq) * a_eng;
        e_gain += (tg_g - e_gain) * a_eng;
        if (e_gain > 0.0005f) {
            float o1, o2;
            e_ph1 += e_freq / SR; if (e_ph1 >= 1.0f) e_ph1 -= 1.0f;
            e_ph2 += e_freq * 0.5f / SR; if (e_ph2 >= 1.0f) e_ph2 -= 1.0f;
            o1 = osc(W_TRI, e_ph1);
            o2 = osc(W_SAW, e_ph2);
            e_lp1 += ((o1 + o2) - e_lp1) * a_f1;
            e_lp2 += (e_lp1 - e_lp2) * a_f2;
            out += e_lp2 * e_gain;
        }

        /* tyre screech */
        s_gain += (0.12f * lvl - s_gain) * k_sg;
        if (s_gain > 0.0005f) {
            x = noise();
            y = b0 * x + b2 * s_x2 - a1 * s_y1 - a2 * s_y2;
            s_x2 = s_x1; s_x1 = x;
            s_y2 = s_y1; s_y1 = y;
            out += y * s_gain;
        } else {
            s_x1 = s_x2 = s_y1 = s_y2 = 0.0f;
        }

        /* one-shots */
        for (i = 0; i < MAX_VOICES; i++)
            if (voices[i].active) out += voice_sample(&voices[i]);

        out *= MASTER;
        if (out > 1.0f) out = 1.0f;
        if (out < -1.0f) out = -1.0f;
        buf[n * 2 + 0] = out;
        buf[n * 2 + 1] = out;
    }
}

static void snd_thread(void *arg)
{
    (void)arg;
    for (;;) {
        sys_event_t event;
        u64 current_block;
        u32 idx;
        float *data;

        sysEventQueueReceive(snd_queue, &event, 20 * 1000);
        current_block = *(u64 *)((u64)config.readIndex);
        idx = (u32)((current_block + 1) % config.numBlocks);
        data = (float *)((u64)config.audioDataStart) + config.channelCount * AUDIO_BLOCK_SAMPLES * idx;
        fill_block(data);
    }
}

void snd_init(void)
{
    audioPortParam params;
    u32 portNum;

    if (audioInit() != 0) return;

    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = AUDIO_PORT_INITLEVEL;
    params.level = 1.0f;
    if (audioPortOpen(&params, &portNum) != 0) return;
    if (audioGetPortConfig(portNum, &config) != 0) return;
    if (audioCreateNotifyEventQueue(&snd_queue, &snd_key) != 0) return;
    if (audioSetNotifyEventQueue(snd_key) != 0) return;
    sysEventQueueDrain(snd_queue);
    if (audioPortStart(portNum) != 0) return;

    snd_ok = 1;
    if (sysThreadCreate(&snd_tid, snd_thread, NULL, 1000, 0x10000, 0, "sky_audio") != 0)
        snd_ok = 0;
}
