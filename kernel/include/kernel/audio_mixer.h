/*
 * audio_mixer.h — 16-channel software audio mixer
 *
 * Mixes multiple PCM sources (various rates, 8/16-bit) into a single
 * 48 kHz 16-bit stereo output stream.  Called from the AC'97 IRQ handler.
 */

#ifndef _KERNEL_AUDIO_MIXER_H
#define _KERNEL_AUDIO_MIXER_H

#include <stdint.h>

#define MIXER_MAX_CHANNELS  16

/* Channel state — managed internally */
typedef struct {
    int           active;
    const uint8_t *data;        /* raw PCM (not freed by mixer) */
    uint32_t      data_len;     /* total source samples */
    uint32_t      sample_rate;  /* source rate (e.g. 11025) */
    int           bits;         /* 8 or 16 */
    int           is_signed;    /* 0 = unsigned, 1 = signed */
    int           channels;     /* 1 = mono, 2 = stereo */
    uint32_t      pos_frac;     /* 16.16 fixed-point position */
    uint32_t      step_frac;    /* 16.16 rate ratio: (src_rate << 16) / out_rate */
    int           vol_left;     /* 0–255 */
    int           vol_right;    /* 0–255 */
    int           handle;       /* caller-defined ID */
} mixer_channel_t;

/* Initialize mixer with the given output sample rate (typically 48000) */
void mixer_init(uint32_t output_rate);

/*
 * Start playing a sound.
 *   data     — raw PCM samples (8-bit unsigned or 16-bit signed)
 *   len      — total number of samples (mono) or sample frames (stereo)
 *   rate     — source sample rate in Hz
 *   bits     — 8 or 16
 *   channels — 1 (mono) or 2 (stereo)
 *   is_signed— 0 for unsigned, 1 for signed
 *   vol      — DOOM volume 0–127 (scaled to 0–255)
 *   sep      — DOOM stereo separation 0–254 (0=left, 127=center, 254=right)
 *   handle   — caller-defined channel ID
 *
 * Returns mixer channel index (0–15) or -1 if no channel available.
 */
int mixer_play(const uint8_t *data, uint32_t len, uint32_t rate,
               int bits, int channels, int is_signed,
               int vol, int sep, int handle);

/* Stop a specific mixer channel */
void mixer_stop(int channel);

/* Stop all channels with the given handle */
void mixer_stop_by_handle(int handle);

/* Update volume/separation on a channel */
void mixer_set_params(int channel, int vol, int sep);

/* Query if a channel is actively playing */
int mixer_is_playing(int channel);

/*
 * Render num_frames stereo frames into output buffer.
 * Called from AC'97 IRQ handler — must be fast.
 * output must hold num_frames * 2 int16_t values (L,R interleaved).
 */
void mixer_render(int16_t *output, uint32_t num_frames);

#endif /* _KERNEL_AUDIO_MIXER_H */
