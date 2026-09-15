#ifndef DEBOUNCE_SOUND_H
#define DEBOUNCE_SOUND_H

#include <stdint.h>

void debounce_sound_set_volume(double volume);
void debounce_sound_play(void);
void debounce_sound_play_click(int64_t click_state);
void debounce_sound_play_wheel(int64_t vertical);
void debounce_sound_play_dragged(void);

#endif
