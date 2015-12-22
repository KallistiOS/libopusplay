/* Opusplay for KallistiOS ##version##

   main.c
   Copyright (C) 2015 Lawrence Sebald

   Adapted from:
   KallistiOS Ogg/Vorbis Decoder Library for KOS (liboggvorbisplay)

   Copyright (C) 2001, 2002 Thorsten Titze
   Copyright (C) 2002, 2003, 2004 Dan Potter
*/

#include <kos/thread.h>
#include <kos/dbglog.h>
#include <dc/sound/stream.h>

#include "opusplay.h"

/* Forward declarations... */
void _opusplay_thd_quit(void);
void _opusplay_mainloop(void);

static kthread_t *thd = NULL;

static void *sndserver_thread(void *data) {
    (void)data;

    dbglog(DBG_DEBUG, "opusplay: thread id is %d\n", thd_get_current()->tid);
    _opusplay_mainloop();
    return NULL;
}

int opusplay_init(void) {
    if(thd) {
        dbglog(DBG_DEBUG, "opusplay: already initialized!\n");
        return -1;
    }

    if(snd_stream_init() < 0) {
        dbglog(DBG_DEBUG, "opusplay: cannot initialize snd_stream!\n");
        return -1;
    }

    dbglog(DBG_DEBUG, "opusplay: initializing [for opus 1.1 + opus 0.6]\n");
    thd = thd_create(0, sndserver_thread, NULL);
    if(thd) {
        /* Wait until the decoder thread is ready */
        opusplay_wait_start();
        dbglog(DBG_DEBUG, "opusplay: successfully created thread\n");
        return 0;
    }
    else {
        dbglog(DBG_DEBUG, "opusplay: error creating thread\n");
        return -1;
    }
}


void opusplay_shutdown(void) {
    if(!thd) {
        dbglog(DBG_DEBUG, "opusplay: not initialized!\n");
        return;
    }

    _opusplay_thd_quit();
    thd_join(thd, NULL);
    thd = NULL;

    dbglog(DBG_DEBUG, "opusplay: exited successfully\n");
}
