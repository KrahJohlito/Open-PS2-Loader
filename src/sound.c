/*
 Copyright 2022, Thanks to SP193
 Licenced under Academic Free License version 3.0
 Review OpenPS2Loader README & LICENSE files for further details.
 */

#include <audsrv.h>
#include "include/sound.h"
#include "include/opl.h"
#include "include/ioman.h"
#include "include/themes.h"
#include "include/gui.h"

/*--    Theme Sound Effects    ----------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------*/
extern unsigned char boot_adp[];
extern unsigned int size_boot_adp;

extern unsigned char cancel_adp[];
extern unsigned int size_cancel_adp;

extern unsigned char confirm_adp[];
extern unsigned int size_confirm_adp;

extern unsigned char cursor_adp[];
extern unsigned int size_cursor_adp;

extern unsigned char message_adp[];
extern unsigned int size_message_adp;

extern unsigned char transition_adp[];
extern unsigned int size_transition_adp;

struct sfxEffect
{
    const char *name;
    void *buffer;
    int size;
    int builtin;
    int duration_ms;
};

static struct sfxEffect sfx_files[SFX_COUNT] = {
    {"boot.adp"},
    {"cancel.adp"},
    {"confirm.adp"},
    {"cursor.adp"},
    {"message.adp"},
    {"transition.adp"},
};

static struct audsrv_adpcm_t sfx[SFX_COUNT];
static int audio_initialized = 0;

// Returns 0 if the specified file was read. The sfxEffect structure will not be updated unless the file is successfully read.
static int sfxRead(const char *full_path, struct sfxEffect *sfx)
{
    int adpcm;
    void *buffer;
    int ret, size;

    LOG("SFX: sfxRead('%s')\n", full_path);

    adpcm = open(full_path, O_RDONLY);
    if (adpcm < 0) {
        LOG("SFX: %s: Failed to open adpcm file %s\n", __FUNCTION__, full_path);
        return -ENOENT;
    }

    size = lseek(adpcm, 0, SEEK_END);
    lseek(adpcm, 0L, SEEK_SET);

    buffer = memalign(64, size);
    if (buffer == NULL) {
        LOG("SFX: Failed to allocate memory for SFX\n");
        close(adpcm);
        return -ENOMEM;
    }

    ret = read(adpcm, buffer, size);
    close(adpcm);

    if (ret != size) {
        LOG("SFX: Failed to read SFX: %d (expected %d)\n", ret, size);
        free(buffer);
        return -EIO;
    }

    sfx->buffer = buffer;
    sfx->size = size;
    sfx->builtin = 0;

    return 0;
}

static int sfxCalculateSoundDuration(int nSamples)
{
    float sampleRate = 44100; // 44.1kHz

    // Return duration in milliseconds
    return (nSamples / sampleRate) * 1000;
}

static void sfxInitDefaults(void)
{
    sfx_files[SFX_BOOT].buffer = boot_adp;
    sfx_files[SFX_BOOT].size = size_boot_adp;
    sfx_files[SFX_BOOT].builtin = 1;
    sfx_files[SFX_CANCEL].buffer = cancel_adp;
    sfx_files[SFX_CANCEL].size = size_cancel_adp;
    sfx_files[SFX_CANCEL].builtin = 1;
    sfx_files[SFX_CONFIRM].buffer = confirm_adp;
    sfx_files[SFX_CONFIRM].size = size_confirm_adp;
    sfx_files[SFX_CONFIRM].builtin = 1;
    sfx_files[SFX_CURSOR].buffer = cursor_adp;
    sfx_files[SFX_CURSOR].size = size_cursor_adp;
    sfx_files[SFX_CURSOR].builtin = 1;
    sfx_files[SFX_MESSAGE].buffer = message_adp;
    sfx_files[SFX_MESSAGE].size = size_message_adp;
    sfx_files[SFX_MESSAGE].builtin = 1;
    sfx_files[SFX_TRANSITION].buffer = transition_adp;
    sfx_files[SFX_TRANSITION].size = size_transition_adp;
    sfx_files[SFX_TRANSITION].builtin = 1;
}

// Returns 0 (AUDSRV_ERR_NOERROR) if the sound was loaded successfully.
static int sfxLoad(struct sfxEffect *sfxData, audsrv_adpcm_t *sfx)
{
    int ret;

    // Calculate duration based on number of samples
    sfxData->duration_ms = sfxCalculateSoundDuration(((u32 *)sfxData->buffer)[3]);
    // Estimate duration based on filesize, if the ADPCM header was 0
    if (sfxData->duration_ms == 0)
        sfxData->duration_ms = sfxData->size / 47;

    ret = audsrv_load_adpcm(sfx, sfxData->buffer, sfxData->size);
    if (sfxData->builtin == 0) {
        free(sfxData->buffer);
        sfxData->buffer = NULL; // Mark the buffer as freed.
    }

    return ret;
}

// Returns number of audio files successfully loaded, < 0 if an unrecoverable error occurred.
int sfxInit(int bootSnd)
{
    char sound_path[256];
    char full_path[256];
    int ret, loaded;
    int thmSfxEnabled = 0;
    int i = 1;

    if (!audio_initialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return -1;
    }

    audsrv_adpcm_init();
    sfxInitDefaults();
    audioSetVolume();

    // Check default theme is not current theme
    int themeID = thmGetGuiValue();
    if (themeID != 0) {
        // Get theme path for sfx
        char *thmPath = thmGetFilePath(themeID);
        snprintf(sound_path, sizeof(sound_path), "%ssound", thmPath);

        // Check for custom sfx folder
        DIR *dir = opendir(sound_path);
        if (dir != NULL) {
            thmSfxEnabled = 1;
            closedir(dir);
        }
    }

    loaded = 0;
    i = bootSnd ? 0 : 1;
    for (; i < SFX_COUNT; i++) {
        if (thmSfxEnabled) {
            snprintf(full_path, sizeof(full_path), "%s/%s", sound_path, sfx_files[i].name);
            ret = sfxRead(full_path, &sfx_files[i]);
            if (ret != 0) {
                LOG("SFX: %s could not be loaded. Using default sound %d.\n", full_path, ret);
            }
        } else
            snprintf(full_path, sizeof(full_path), "builtin/%s", sfx_files[i].name);

        ret = sfxLoad(&sfx_files[i], &sfx[i]);
        if (ret == 0) {
            LOG("SFX: Loaded %s, size=%d, duration=%dms\n", full_path, sfx_files[i].size, sfx_files[i].duration_ms);
            loaded++;
        } else {
            LOG("SFX: failed to load %s, error %d\n", full_path, ret);
        }
    }

    return loaded;
}

int sfxGetSoundDuration(int id)
{
    if (!audio_initialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return 0;
    }

    return sfx_files[id].duration_ms;
}

void sfxPlay(int id)
{
    if (!audio_initialized) {
        LOG("SFX: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    if (gEnableSFX) {
        audsrv_ch_play_adpcm(id, &sfx[id]);
    }
}

/*--    Theme Background Music    -------------------------------------------------------------------------------------------
---------------------------------------------------------------------------------------------------------------------------*/

#define BGM_RING_BUFFER_COUNT 16
#define BGM_RING_BUFFER_SIZE  4096
#define BGM_THREAD_BASE_PRIO  0x40
#define BGM_THREAD_STACK_SIZE 0x1000

extern unsigned char bgm_wav[];
extern unsigned int size_bgm_wav;

struct bgm_t
{
    void *buffer;
    int size;

    FILE *file;
    int remaining;
    int channels;
    int bits;
    int freq;
};

static struct bgm_t bgm;
static int bgmThreadID, bgmIoThreadID;
static int outSema, inSema;
static unsigned char terminateFlag, bgmIsPlaying;
static unsigned char rdPtr, wrPtr;
static u8 bgmBuffer[BGM_RING_BUFFER_COUNT][BGM_RING_BUFFER_SIZE] __attribute__((aligned(64)));
static unsigned short int bgmBufferSizes[BGM_RING_BUFFER_COUNT];
static volatile unsigned char bgmThreadRunning, bgmIoThreadRunning;

static u8 bgmThreadStack[BGM_THREAD_STACK_SIZE] __attribute__((aligned(16)));
static u8 bgmIoThreadStack[BGM_THREAD_STACK_SIZE] __attribute__((aligned(16)));

extern void *_gp;

static void bgmThread(void *arg)
{
    bgmThreadRunning = 1;

    while (!terminateFlag) {
        SleepThread();

        while (PollSema(outSema) == outSema) {
            audsrv_wait_audio(bgmBufferSizes[rdPtr]);
            audsrv_play_audio(bgmBuffer[rdPtr], bgmBufferSizes[rdPtr]);
            rdPtr = (rdPtr + 1) % BGM_RING_BUFFER_COUNT;

            SignalSema(inSema);
        }
    }

    audsrv_stop_audio();

    if (bgm.file != NULL)
        fclose(bgm.file);

    rdPtr = 0;
    wrPtr = 0;

    bgmThreadRunning = 0;
    bgmIsPlaying = 0;
}

static int memoryRead(char *dest, char *src, int size)
{
    memcpy(dest, src, size);
    src += size;

    return size;
}

static void bgmIoThread(void *arg)
{
    int sizeToRead, sizeToReadTotal, partsToRead, i;

    bgmIoThreadRunning = 1;
    do {
        if (bgm.file == NULL)
            bgm.remaining = bgm.size;
        else {
            fseek(bgm.file, 0, SEEK_END);
            bgm.remaining = ftell(bgm.file);
            rewind(bgm.file);
        }

        while (bgm.remaining > 0 && !terminateFlag && gEnableBGM) {
            WaitSema(inSema);
            sizeToRead = bgm.remaining > BGM_RING_BUFFER_SIZE ? BGM_RING_BUFFER_SIZE : bgm.remaining;
            bgmBufferSizes[wrPtr] = sizeToRead;
            sizeToReadTotal = sizeToRead;
            partsToRead = 1;

            // Determine the maximum amount of bytes that can be read, taking care of the wraparound at the end of the ring buffer. Since most PS2 peripherals do better at reading large blocks.
            while ((wrPtr + partsToRead < BGM_RING_BUFFER_COUNT) && (bgm.remaining - sizeToReadTotal > 0) && (PollSema(inSema) == inSema)) {
                sizeToRead = bgm.remaining - sizeToReadTotal > BGM_RING_BUFFER_SIZE ? BGM_RING_BUFFER_SIZE : bgm.remaining - sizeToReadTotal;
                sizeToReadTotal += sizeToRead;
                bgmBufferSizes[wrPtr + partsToRead] = sizeToRead;
                partsToRead++;
            }

            if ((bgm.file == NULL ? memoryRead(bgmBuffer[wrPtr], bgm.buffer, sizeToReadTotal) : fread(bgmBuffer[wrPtr], 1, sizeToReadTotal, bgm.file)) != sizeToReadTotal) {
                printf("BGM: I/O error while reading.\n");
                bgmIoThreadRunning = 0;
                return;
            }

            if (bgm.file == NULL)
                bgm.buffer += sizeToReadTotal;

            wrPtr = (wrPtr + partsToRead) % BGM_RING_BUFFER_COUNT;
            bgm.remaining -= sizeToReadTotal;
            for (i = 0; i < partsToRead; i++)
                SignalSema(outSema);
            WakeupThread(bgmThreadID);
        }

        if (bgm.file == NULL)
            bgm.buffer -= bgm.size;

    } while (!terminateFlag && gEnableBGM);

    bgmIoThreadRunning = 0;
    terminateFlag = 1;
    WakeupThread(bgmThreadID);
}

static int bgmReadFormat(void)
{
    char bgmPath[256];

    int themeID = thmGetGuiValue();
    if (themeID != 0) {
        char *thmPath = thmGetFilePath(themeID);
        snprintf(bgmPath, sizeof(bgmPath), "%ssound/bgm.wav", thmPath);

        bgm.file = fopen(bgmPath, "rb");
        if (bgm.file == NULL) {
            LOG("BGM: Failed to open wave file %s\n", bgmPath);
            return -ENOENT;
        }

        fseek(bgm.file, 22, SEEK_SET);
        fread(&bgm.channels, 2, 1, bgm.file);
        rewind(bgm.file);

        fseek(bgm.file, 24, SEEK_SET);
        fread(&bgm.freq, 4, 1, bgm.file);
        rewind(bgm.file);

        fseek(bgm.file, 34, SEEK_SET);
        fread(&bgm.bits, 2, 1, bgm.file);
        rewind(bgm.file);
    } else {
        bgm.file = NULL;
        bgm.buffer = bgm_wav;
        bgm.size = size_bgm_wav;

        bgm.channels = 2;
        bgm.freq = 48000;
        bgm.bits = 16;
    }

    return 0;
}

static int bgmInit(void)
{
    ee_thread_t thread;
    ee_sema_t sema;
    int result;

    terminateFlag = 0;
    rdPtr = 0;
    wrPtr = 0;
    bgmThreadRunning = 0;
    bgmIoThreadRunning = 0;

    sema.max_count = BGM_RING_BUFFER_COUNT;
    sema.init_count = BGM_RING_BUFFER_COUNT;
    sema.attr = 0;
    sema.option = (u32) "bgm-in-sema";
    inSema = CreateSema(&sema);

    if (inSema >= 0) {
        sema.max_count = BGM_RING_BUFFER_COUNT;
        sema.init_count = 0;
        sema.attr = 0;
        sema.option = (u32) "bgm-out-sema";
        outSema = CreateSema(&sema);

        if (outSema < 0) {
            DeleteSema(inSema);
            return outSema;
        }
    } else
        return inSema;

    thread.func = &bgmThread;
    thread.stack = bgmThreadStack;
    thread.stack_size = sizeof(bgmThreadStack);
    thread.gp_reg = &_gp;
    thread.initial_priority = BGM_THREAD_BASE_PRIO;
    thread.attr = 0;
    thread.option = 0;

    // BGM thread will start in DORMANT state.
    bgmThreadID = CreateThread(&thread);

    if (bgmThreadID >= 0) {
        thread.func = &bgmIoThread;
        thread.stack = bgmIoThreadStack;
        thread.stack_size = sizeof(bgmIoThreadStack);
        thread.gp_reg = &_gp;
        thread.initial_priority = BGM_THREAD_BASE_PRIO + 1;
        thread.attr = 0;
        thread.option = 0;

        // BGM I/O thread will start in DORMANT state.
        bgmIoThreadID = CreateThread(&thread);
        if (bgmIoThreadID >= 0) {
            result = 0;
        } else {
            DeleteSema(inSema);
            DeleteSema(outSema);
            DeleteThread(bgmThreadID);
            result = bgmIoThreadID;
        }
    } else {
        result = bgmThreadID;
        DeleteSema(inSema);
        DeleteSema(outSema);
    }

    return result;
}

static void bgmDeinit(void)
{
    DeleteSema(inSema);
    DeleteSema(outSema);
    DeleteThread(bgmThreadID);
    DeleteThread(bgmIoThreadID);
}

static void bgmShutdownDelayCallback(s32 alarm_id, u16 time, void *common)
{
    iWakeupThread((int)common);
}

void bgmStart(void)
{
    struct audsrv_fmt_t audsrvFmt;

    if (!audio_initialized) {
        LOG("BGM: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    int ret = bgmInit();
    if (ret >= 0) {
        if (bgmReadFormat() != 0) {
            bgmDeinit();
            return;
        }

        audsrvFmt.freq = bgm.freq;
        audsrvFmt.bits = bgm.bits;
        audsrvFmt.channels = bgm.channels;

        audsrv_set_format(&audsrvFmt);

        terminateFlag = 0;
        bgmIsPlaying = 1;

        StartThread(bgmIoThreadID, NULL);
        StartThread(bgmThreadID, NULL);
    }
}

void bgmStop(void)
{
    int threadId;

    if (!audio_initialized) {
        LOG("BGM: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    LOG("bgmStop: terminating threads...\n");

    terminateFlag = 1;
    WakeupThread(bgmThreadID);

    threadId = GetThreadId();
    while (bgmIoThreadRunning) {
        SetAlarm(200 * 16, &bgmShutdownDelayCallback, (void *)threadId);
        SleepThread();
    }
    while (bgmThreadRunning) {
        SetAlarm(200 * 16, &bgmShutdownDelayCallback, (void *)threadId);
        SleepThread();
    }

    bgmDeinit();
    LOG("bgmStop: stopped.\n");
}

int isBgmPlaying(void)
{
    int ret;

    ret = (int)bgmIsPlaying;

    return ret;
}

/*--    General Audio    ------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------*/

void audioInit(void)
{
    if (!audio_initialized) {
        if (audsrv_init() != 0) {
            LOG("AUDIO: Failed to initialize audsrv\n");
            LOG("AUDIO: Audsrv returned error string: %s\n", audsrv_get_error_string());
            return;
        }
        audio_initialized = 1;
    }
}

void audioEnd(void)
{
    if (!audio_initialized) {
        LOG("AUDIO: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    if (gEnableBGM && isBgmPlaying()) {
        bgmStop();
        bgmDeinit();
    }

    audsrv_quit();
    audio_initialized = 0;
}

void audioSetVolume(void)
{
    int i;

    if (!audio_initialized) {
        LOG("AUDIO: %s: ERROR: not initialized!\n", __FUNCTION__);
        return;
    }

    for (i = 1; i < SFX_COUNT; i++)
        audsrv_adpcm_set_volume(i, gSFXVolume);

    audsrv_adpcm_set_volume(0, gBootSndVolume);
    audsrv_set_volume(gBGMVolume);
}
