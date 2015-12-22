/* Opusplay for KallistiOS ##version##

   opusplay.h
   Copyright (C) 2015 Lawrence Sebald

   Adapted from:
   KallistiOS Ogg/Vorbis Decoder Library for KOS (liboggvorbisplay)
   sndoggvorbis.h
   Copyright (C) 2001 Thorsten Titze
*/

#ifndef OPUSPLAY_OPUSPLAY_H
#define OPUSPLAY_OPUSPLAY_H

#include <sys/cdefs.h>
__BEGIN_DECLS

/* Init/shutdown functions. */
extern int opusplay_init(void);
extern void opusplay_shutdown(void);

/* Start and stop playback. */
extern int opusplay_play_file(const char *file, int loop);
extern int opusplay_stop(void);
extern void opusplay_wait_start(void);

/* Functions for controlling playback. */
extern int opusplay_is_playing(void);
extern void opusplay_set_volume(int vol);

/* Enable/disable queued waiting */
extern void opusplay_queue_enable(void);
extern void opusplay_queue_disable(void);

/* Wait for the song to be queued */
extern void opusplay_queue_wait(void);

/* Queue the song to start if it's in QUEUED */
extern void opusplay_queue_go(void);

__END_DECLS

#endif  /* !OPUSPLAY_OPUSPLAY_H */
