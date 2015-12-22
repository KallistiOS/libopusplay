/* Opusplay for KallistiOS ##version##

   opusplay.c
   Copyright (C) 2015 Lawrence Sebald

   Adapted from:

   sndoggvorbis.c
   Copyright (C) 2001, 2002 Thorsten Titze
   Copyright (C) 2002, 2003, 2004 Dan Potter

   An Ogg/Vorbis player library using sndstream and functions provided by
   ivorbisfile (Tremor).
*/

#include <string.h>

#include <kos/thread.h>
#include <kos/dbglog.h>
#include <kos/sem.h>
#include <arch/timer.h>
#include <dc/sound/stream.h>

#include <opusfile/opusfile.h>

/* Thread-state related defines */
#define STATUS_INIT     0
#define STATUS_READY    1
#define STATUS_STARTING 2
#define STATUS_PLAYING  3
#define STATUS_STOPPING 4
#define STATUS_QUIT     5
#define STATUS_ZOMBIE   6
#define STATUS_REINIT   7
#define STATUS_RESTART  8
#define STATUS_LOADING  9
#define STATUS_QUEUEING 10
#define STATUS_QUEUED   11

/* Set the buffer size -- in bytes. */
#define BUF_SIZE 65536

/* Buffer-related variables. */
static uint8 pcm_buffer[BUF_SIZE + 16384];
static uint8 *pcm_ptr = pcm_buffer;

static int32 pcm_count = 0;
static int32 last_read = 0;

/* Library status related variables */
static OggOpusFile *vf;
static int queue_enabled;
static volatile int loop_mode;
static volatile int status;
static semaphore_t halt_sem = SEM_INITIALIZER(0);
static int volume = 240;
static snd_stream_hnd_t stream_hnd = SND_STREAM_INVALID;

/* Set the volume of the streaming channel. */
void opusplay_set_volume(int vol) {
    volume = vol;
    snd_stream_volume(stream_hnd, vol);
}

/* Is anything currently playing? */
int opusplay_is_playing(void) {
    if(status == STATUS_PLAYING || status == STATUS_STARTING ||
       status == STATUS_QUEUEING || status == STATUS_QUEUED)
        return 1;

    return 0;
}

/* Enable/disable queued waiting */
void opusplay_queue_enable(void) {
    queue_enabled = 1;
}
void opusplay_queue_disable(void) {
    queue_enabled = 0;
}

/* Wait for the song to be queued */
void opusplay_queue_wait(void) {
    /* Make sure we've loaded ok */
    while (status != STATUS_QUEUED)
        thd_pass();
}

/* Queue the song to start if it's in QUEUED. */
void opusplay_queue_go(void) {
    /* Make sure we're ready */
    opusplay_queue_wait();

    /* Tell it to go */
    status = STATUS_STARTING;
    sem_signal(&halt_sem);
}

/* Halt the calling thread until the decoder is ready to start decoding a new
   stream. */
void opusplay_wait_start(void) {
    while(status != STATUS_READY)
        thd_pass();
}

/* This function is called by the streaming server when it needs more PCM data
   for its internal buffer. */
static void *stream_callback(snd_stream_hnd_t hnd, int size, int *size_out)
{
    int pcm_decoded = 0;
    (void)hnd;

    /* Check if the callback requests more data than our buffer can hold */
    if(size > BUF_SIZE)
        size = BUF_SIZE;

	/* Shift the last data the AICA driver took out of the PCM Buffer */
    if(last_read > 0) {
        pcm_count -= last_read;

        /* Make sure we don't underrun... */
        if(pcm_count < 0)
            pcm_count = 0;

        memcpy(pcm_buffer, pcm_buffer + last_read, pcm_count);
        pcm_ptr = pcm_buffer + pcm_count;
    }

    /* If our buffer doesn't have enough data to satisfy the callback decode
       chunks until we have enough PCM samples buffered */
    while(pcm_count < size) {
        pcm_decoded = op_read_stereo(vf, (opus_int16 *)pcm_ptr, 2048);

        /* Are we at the end of the stream? If so and looping is enabled, then
           go ahead and seek back to the beginning of the file. */
        if (pcm_decoded == 0 && loop_mode) {
            if(op_raw_seek(vf, 0) < 0) {
                dbglog(DBG_ERROR, "opusplay: can't op_raw_seek to the "
                       "beginning of the file to loop!\n");
            }
            else {
                /* Try to read again, now that we rewound the file. */
                pcm_decoded = op_read_stereo(vf, (opus_int16 *)pcm_ptr, 2048);
            }
        }

        pcm_count += pcm_decoded * 4;
        pcm_ptr += pcm_decoded * 4;

        if(pcm_decoded == 0)
            break;
    }

    if(pcm_count > size)
        *size_out = size;
    else
        *size_out = pcm_count;

    /* Let the next callback know how many bytes the last callback grabbed. */
    last_read = *size_out;

    if(pcm_decoded == 0)
        return NULL;
    else
        return pcm_buffer;
}

/* This function actually implements the real main loop for decoding Opus
   streams. This handles playback, polling and all kinds of other fun stuff. */
static void opusplay_thread(void) {
    int stat;

    /* Allocate the lower level stream that we'll use to play */
    stream_hnd = snd_stream_alloc(NULL, SND_STREAM_BUFFER_MAX);
    if(stream_hnd == SND_STREAM_INVALID) {
        status = STATUS_ZOMBIE;
        return;
    }

    while(status != STATUS_QUIT) {
        switch(status) {
            case STATUS_INIT:
                status = STATUS_READY;
                break;

            case STATUS_READY:
            case STATUS_QUEUED:
                sem_wait(&halt_sem);
                break;

            case STATUS_QUEUEING:
                snd_stream_reinit(stream_hnd, &stream_callback);
                snd_stream_queue_enable(stream_hnd);
                snd_stream_start(stream_hnd, 48000, 1);
                snd_stream_volume(stream_hnd, volume);

                if (status != STATUS_STOPPING)
                    status = STATUS_QUEUED;
                break;

            case STATUS_STARTING:
                if(queue_enabled) {
                    snd_stream_queue_go(stream_hnd);
                }
                else {
                    snd_stream_reinit(stream_hnd, &stream_callback);
                    snd_stream_start(stream_hnd, 48000, 1);
                }

                snd_stream_volume(stream_hnd, volume);
                status = STATUS_PLAYING;
                break;

            case STATUS_PLAYING:
                /* Feed the stream if it needs more data. */
                if((stat = snd_stream_poll(stream_hnd)) >= 0) {
                    /* Sleep until the next buffer is needed... */
                    thd_sleep(50);
                    break;
                }
                /* The Opus stream didn't have any more data (and we aren't
                   looping), so the stream can be cleaned up. This will fall
                   through to the STATUS_STOPPING case to do the cleanup... */

            case STATUS_STOPPING:
                snd_stream_stop(stream_hnd);

                /* Reset our PCM buffer */
                pcm_count = 0;
                last_read = 0;
                pcm_ptr = pcm_buffer;

                /* Clean up the opusfile handle too. */
                op_free(vf);
                status = STATUS_READY;
                break;
        }
    }

    snd_stream_stop(stream_hnd);
    snd_stream_destroy(stream_hnd);
    status = STATUS_ZOMBIE;
}

/* Play an Opus stream from a given file on the VFS, looping if desired. */
int opusplay_play_file(const char *file, int loop) {
    int err;

    /* Don't try to play a new file if we're already playing one. */
    if(status != STATUS_READY) {
        dbglog(DBG_DEBUG, "opusplay_play_file: already playing file!\n");
        return -1;
    }

    /* Open up the Opus file. */
    if(!(vf = op_open_file(file, &err))) {
        dbglog(DBG_DEBUG, "opusplay_play_file: cannot open Opus bitstream "
               "(err: %d)\n", err);
        return -2;
    }

    /* Set up the initial state for this new file. */
    loop_mode = loop;

    if(queue_enabled)
        status = STATUS_QUEUEING;
    else
        status = STATUS_STARTING;

    /* Signal to the decoding thread that we're ready to go. */
    sem_signal(&halt_sem);

    return 0;
}

/* Stop playback of the current stream and return to the ready state. */
int opusplay_stop(void) {
    /* If we're not actually playing anything, we can't stop playing... */
    if(!opusplay_is_playing())
        return -1;

    /* Wait for the thread to finish starting, if needed. */
    while(status == STATUS_STARTING)
        thd_pass();

    status = STATUS_STOPPING;

    /* Wait until thread is at STATUS_READY before returning... */
    while(status != STATUS_READY)
        thd_pass();

    return 0;
}

/* Internal functions...

   These are only externally accessible to be able to be called from main.c.
   You should really not call these from outside of the library. */

/* "Main loop" of the decoding thread. Not meant to be called outside of the
   library. */
void _opusplay_mainloop(void) {
    /* Set up the initial state for the thread */
    sem_init(&halt_sem, 0);
    status = STATUS_INIT;
    queue_enabled = 0;

    /* Call the actual thread loop. This won't return until the thread has
       finished running */
    opusplay_thread();

    /* Clean up... */
    sem_destroy(&halt_sem);
}

/* Internal function to shut down the player thread. */
void _opusplay_thd_quit(void) {
    /* Signal to the decoder thread that it should exit. */
    status = STATUS_QUIT;

    /* Wake the thread up, in case it's sleeping... */
    sem_signal(&halt_sem);

    /* Wait until the thread has died to return to the caller. */
    while(status != STATUS_ZOMBIE)
        thd_pass();
}
