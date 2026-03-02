/*
 * audio_mixer.c — 16-channel software audio mixer
 *
 * Resamples and mixes multiple PCM sources into a single 48 kHz stereo
 * output stream.  mixer_render() is called from the AC'97 IRQ handler
 * and must be fast — no malloc, no blocking.
 *
 * Resampling uses 16.16 fixed-point stepping for accuracy without
 * floating point.  All intermediate mixing is done in 32-bit to avoid
 * clipping until the final clamp.
 */

#include <kernel/audio_mixer.h>
#include <kernel/io.h>
#include <string.h>

/* ── State ────────────────────────────────────────────────────────── */

static mixer_channel_t channels[MIXER_MAX_CHANNELS];
static uint32_t mixer_output_rate = 48000;

/* Max frames per render call — matches AC97_BUF_SAMPLES in ac97.h */
#define MIXER_MAX_FRAMES 2048

/* Static accumulation buffer — avoids 16 KB stack allocation in IRQ context.
 * Sized for the maximum buffer (2048 stereo frames = 4096 int32_t = 16 KB). */
static int32_t accum_buf[MIXER_MAX_FRAMES * 2];

/* ── Initialization ───────────────────────────────────────────────── */

void mixer_init(uint32_t output_rate) {
    mixer_output_rate = output_rate ? output_rate : 48000;
    memset(channels, 0, sizeof(channels));
}

/* ── Channel management ───────────────────────────────────────────── */

static void compute_vol_lr(int vol, int sep, int *vol_l, int *vol_r) {
    /*
     * DOOM: vol = 0–127, sep = 0–254 (0=left, 127=center, 254=right).
     * Scale vol to 0–255, then apply separation.
     */
    int v = vol * 2;
    if (v > 255) v = 255;

    /* sep: 0 → full left, 127 → center, 254 → full right */
    *vol_l = (v * (254 - sep)) / 127;
    *vol_r = (v * sep) / 127;
    if (*vol_l > 255) *vol_l = 255;
    if (*vol_r > 255) *vol_r = 255;
}

int mixer_play(const uint8_t *data, uint32_t len, uint32_t rate,
               int bits, int chans, int is_signed,
               int vol, int sep, int handle) {
    if (!data || len == 0 || rate == 0) return -1;

    uint32_t flags = irq_save();

    /* Find a free channel */
    int idx = -1;
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (!channels[i].active) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        irq_restore(flags);
        return -1;
    }

    mixer_channel_t *ch = &channels[idx];
    ch->active      = 1;
    ch->data        = data;
    ch->data_len    = len;
    ch->sample_rate = rate;
    ch->bits        = bits;
    ch->is_signed   = is_signed;
    ch->channels    = chans;
    ch->pos_frac    = 0;
    ch->step_frac   = (rate << 16) / mixer_output_rate;
    ch->handle      = handle;

    compute_vol_lr(vol, sep, &ch->vol_left, &ch->vol_right);

    irq_restore(flags);
    return idx;
}

void mixer_stop(int channel) {
    if (channel < 0 || channel >= MIXER_MAX_CHANNELS) return;
    uint32_t flags = irq_save();
    channels[channel].active = 0;
    irq_restore(flags);
}

void mixer_stop_by_handle(int handle) {
    uint32_t flags = irq_save();
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (channels[i].active && channels[i].handle == handle)
            channels[i].active = 0;
    }
    irq_restore(flags);
}

void mixer_set_params(int channel, int vol, int sep) {
    if (channel < 0 || channel >= MIXER_MAX_CHANNELS) return;
    uint32_t flags = irq_save();
    if (channels[channel].active) {
        compute_vol_lr(vol, sep, &channels[channel].vol_left,
                       &channels[channel].vol_right);
    }
    irq_restore(flags);
}

int mixer_is_playing(int channel) {
    if (channel < 0 || channel >= MIXER_MAX_CHANNELS) return 0;
    return channels[channel].active;
}

/* ── Render (called from AC'97 IRQ) ──────────────────────────────── */

void mixer_render(int16_t *output, uint32_t num_frames) {
    uint32_t total_samples = num_frames * 2;    /* stereo: L, R per frame */

    /* Clear accumulation buffer */
    memset(accum_buf, 0, total_samples * sizeof(int32_t));

    for (int c = 0; c < MIXER_MAX_CHANNELS; c++) {
        mixer_channel_t *ch = &channels[c];
        if (!ch->active) continue;

        int vol_l = ch->vol_left;
        int vol_r = ch->vol_right;
        uint32_t pos = ch->pos_frac;
        uint32_t step = ch->step_frac;
        const uint8_t *src = ch->data;
        uint32_t src_len = ch->data_len;
        int bits = ch->bits;
        int is_signed = ch->is_signed;

        for (uint32_t f = 0; f < num_frames; f++) {
            uint32_t src_idx = pos >> 16;

            if (src_idx >= src_len) {
                /* Sound finished */
                ch->active = 0;
                break;
            }

            /* Fetch sample and convert to signed 16-bit */
            int32_t sample;
            if (bits == 8) {
                if (is_signed)
                    sample = ((int8_t)src[src_idx]) << 8;
                else
                    sample = ((int32_t)src[src_idx] - 128) << 8;
            } else {
                /* 16-bit */
                const int16_t *src16 = (const int16_t *)src;
                sample = src16[src_idx];
            }

            /* Apply per-channel volume and accumulate (mono → stereo) */
            accum_buf[f * 2]     += (sample * vol_l) >> 8;
            accum_buf[f * 2 + 1] += (sample * vol_r) >> 8;

            pos += step;
        }

        ch->pos_frac = pos;
    }

    /* Clamp 32-bit → 16-bit and write to output */
    for (uint32_t i = 0; i < total_samples; i++) {
        int32_t s = accum_buf[i];
        if (s > 32767)  s = 32767;
        if (s < -32768) s = -32768;
        output[i] = (int16_t)s;
    }
}
