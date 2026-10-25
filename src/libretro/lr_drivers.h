#ifndef LR_DRIVERS_H
#define LR_DRIVERS_H

#include <stdint.h>

/* Video, audio and timer drivers that hand ZEsarUX's output to lr_core.c. */

extern int scrlibretro_init(void);
extern int set_scrdriver_libretro(void);
extern int audiolibretro_init(void);
extern int set_audiodriver_libretro(void);

extern uint32_t *lr_pixels;
extern int lr_width;
extern int lr_height;

/* Set by the video driver each time a frame is presented. */
extern int lr_frame_presented;

/* Interleaved stereo, written by audio_send_frame, drained by retro_run. */
#define LR_AUDIO_RING 16384
extern int16_t lr_audio_ring[LR_AUDIO_RING * 2];
extern unsigned lr_audio_write;
extern unsigned lr_audio_read;

#endif
