/*
 * dg_sound.c — DOOM sound backend for ImposOS
 *
 * Implements DG_sound_module (SFX) and DG_music_module (stubs).
 * Parses DMX sound lumps from the WAD and feeds them to the
 * software mixer, which is driven by the AC'97 IRQ handler.
 *
 * DMX sound format:
 *   uint16_t format;        (3 = valid DMX sound)
 *   uint16_t sample_rate;   (typically 11025)
 *   uint32_t num_samples;
 *   uint8_t  samples[];     (8-bit unsigned, 128 = silence)
 */

#include "doomfeatures.h"

#ifdef FEATURE_SOUND

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

#include <kernel/ac97.h>
#include <kernel/audio_mixer.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* Satisfy extern references in i_sound.c's I_BindSoundVariables() */
int   use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

/* ── DMX sound lump header ────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t format;
    uint16_t sample_rate;
    uint32_t num_samples;
    /* uint8_t samples[] follow */
} dmx_header_t;

#define DMX_FORMAT_VALID    3
#define DMX_HEADER_SIZE     8   /* sizeof(dmx_header_t) */

/* ── Sound module callbacks ───────────────────────────────────────── */

static boolean DG_SndInit(boolean use_sfx_prefix) {
    (void)use_sfx_prefix;

    if (!ac97_is_available()) {
        serial_printf("DG_Sound: AC97 not available, sound disabled\n");
        return false;
    }

    serial_printf("DG_Sound: initialized (mixer rate=%u)\n", ac97_get_sample_rate());
    return true;
}

static void DG_SndShutdown(void) {
    /* Stop all mixer channels */
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++)
        mixer_stop(i);
}

static int DG_SndGetSfxLumpNum(sfxinfo_t *sfxinfo) {
    char namebuf[16];

    /* DOOM prefixes sound lump names with "ds" */
    snprintf(namebuf, sizeof(namebuf), "ds%s", sfxinfo->name);

    return W_GetNumForName(namebuf);
}

static void DG_SndUpdate(void) {
    /* No-op: mixing happens in AC'97 IRQ handler */
}

static void DG_SndUpdateParams(int channel, int vol, int sep) {
    mixer_set_params(channel, vol, sep);
}

static int DG_SndStartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
    int lumpnum = sfxinfo->lumpnum;
    int lumplen = W_LumpLength(lumpnum);

    if (lumplen < (int)DMX_HEADER_SIZE)
        return -1;

    /* Cache the lump data */
    const uint8_t *lumpdata = W_CacheLumpNum(lumpnum, PU_STATIC);
    if (!lumpdata) return -1;

    const dmx_header_t *hdr = (const dmx_header_t *)lumpdata;

    if (hdr->format != DMX_FORMAT_VALID) {
        serial_printf("DG_Sound: bad DMX format %u for lump %d\n",
                       hdr->format, lumpnum);
        return -1;
    }

    /* Validate that the lump has enough data */
    uint32_t num_samples = hdr->num_samples;
    if ((int)(DMX_HEADER_SIZE + num_samples) > lumplen)
        num_samples = lumplen - DMX_HEADER_SIZE;

    if (num_samples == 0) return -1;

    const uint8_t *pcm = lumpdata + DMX_HEADER_SIZE;

    /* Stop any sound already playing on this channel */
    mixer_stop(channel);

    /* Play: 8-bit unsigned mono, at the lump's native sample rate */
    int ch = mixer_play(pcm, num_samples, hdr->sample_rate,
                        8,       /* bits */
                        1,       /* mono */
                        0,       /* unsigned */
                        vol, sep, channel);

    return (ch >= 0) ? ch : -1;
}

static void DG_SndStopSound(int channel) {
    mixer_stop(channel);
}

static boolean DG_SndIsPlaying(int channel) {
    return mixer_is_playing(channel) ? true : false;
}

static void DG_SndCacheSounds(sfxinfo_t *sounds, int num_sounds) {
    for (int i = 0; i < num_sounds; i++) {
        if (sounds[i].lumpnum > 0) {
            W_CacheLumpNum(sounds[i].lumpnum, PU_STATIC);
        }
    }
}

/* ── Sound module export ──────────────────────────────────────────── */

static snddevice_t dg_sound_devices[] = {
    SNDDEVICE_SB,
};

sound_module_t DG_sound_module = {
    .sound_devices     = dg_sound_devices,
    .num_sound_devices = sizeof(dg_sound_devices) / sizeof(*dg_sound_devices),
    .Init              = DG_SndInit,
    .Shutdown          = DG_SndShutdown,
    .GetSfxLumpNum     = DG_SndGetSfxLumpNum,
    .Update            = DG_SndUpdate,
    .UpdateSoundParams = DG_SndUpdateParams,
    .StartSound        = DG_SndStartSound,
    .StopSound         = DG_SndStopSound,
    .SoundIsPlaying    = DG_SndIsPlaying,
    .CacheSounds       = DG_SndCacheSounds,
};

/* ── Music module stubs (no music playback) ───────────────────────── */

static boolean DG_MusInit(void) { return false; }
static void    DG_MusShutdown(void) {}
static void    DG_MusSetVol(int vol) { (void)vol; }
static void    DG_MusPause(void) {}
static void    DG_MusResume(void) {}
static void   *DG_MusRegister(void *data, int len) { (void)data; (void)len; return NULL; }
static void    DG_MusUnregister(void *handle) { (void)handle; }
static void    DG_MusPlay(void *handle, boolean loop) { (void)handle; (void)loop; }
static void    DG_MusStop(void) {}
static boolean DG_MusIsPlaying(void) { return false; }
static void    DG_MusPoll(void) {}

static snddevice_t dg_music_devices[] = {
    SNDDEVICE_SB,
};

music_module_t DG_music_module = {
    .sound_devices     = dg_music_devices,
    .num_sound_devices = sizeof(dg_music_devices) / sizeof(*dg_music_devices),
    .Init              = DG_MusInit,
    .Shutdown          = DG_MusShutdown,
    .SetMusicVolume    = DG_MusSetVol,
    .PauseMusic        = DG_MusPause,
    .ResumeMusic       = DG_MusResume,
    .RegisterSong      = DG_MusRegister,
    .UnRegisterSong    = DG_MusUnregister,
    .PlaySong          = DG_MusPlay,
    .StopSong          = DG_MusStop,
    .MusicIsPlaying    = DG_MusIsPlaying,
    .Poll              = DG_MusPoll,
};

#endif /* FEATURE_SOUND */
