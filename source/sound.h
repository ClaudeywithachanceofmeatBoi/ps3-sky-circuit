#ifndef SOUND_H
#define SOUND_H

/* Software synth (same sounds as the HTML version) played through the PS3 audio port. */
void snd_init(void);
void snd_engine(float speed_ratio, int accelerating);
void snd_engine_stop(void);
void snd_screech(float level); /* 0 = silent, 1 = full */
void snd_boost(void);
void snd_beep(float progress);
void snd_explosion(void);
void snd_bump(void);
void snd_jump(void);
void snd_fall(void);

#endif
