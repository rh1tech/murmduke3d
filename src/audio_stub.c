/*
 * Duke3D Audiolib Implementation for RP2350
 * Uses I2S audio via i_picosound driver
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "fx_man.h"
#include "music.h"
#include "i_picosound.h"

// Debug: Set to 1 to disable sound effects (but keep music)
#define DISABLE_SOUND_EFFECTS 0

// Debug: Set to 1 to disable sound callbacks (sounds play but don't notify game)
#define DISABLE_SOUND_CALLBACKS 0

// ============= FX_MAN Implementation =============

int FX_SoundDevice = -1;
int FX_ErrorCode = FX_Ok;
int FX_Installed = 0;

static unsigned int fx_mixrate = 22050;

char *FX_ErrorString(int ErrorNumber) {
    switch (ErrorNumber) {
        case FX_Ok: return "FX ok";
        case FX_Warning: return "FX warning";
        case FX_Error: return "FX error";
        default: return "Unknown FX error";
    }
}

int FX_SetupCard(int SoundCard, fx_device *device) {
    if (device) {
        device->MaxVoices = NUM_SOUND_CHANNELS;
        device->MaxSampleBits = 16;
        device->MaxChannels = 2;
    }
    return FX_Ok;
}

int FX_GetBlasterSettings(fx_blaster_config *blaster) {
    if (blaster) memset(blaster, 0, sizeof(*blaster));
    return FX_Ok;
}

int FX_SetupSoundBlaster(fx_blaster_config blaster, int *MaxVoices, int *MaxSampleBits, int *MaxChannels) {
    if (MaxVoices) *MaxVoices = NUM_SOUND_CHANNELS;
    if (MaxSampleBits) *MaxSampleBits = 16;
    if (MaxChannels) *MaxChannels = 2;
    return FX_Ok;
}

int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits, unsigned mixrate) {
    fx_mixrate = mixrate;
    
    if (I_PicoSound_Init(numvoices, mixrate)) {
        FX_Installed = 1;
        return FX_Ok;
    } else {
        return FX_Error;
    }
}

int FX_Shutdown(void) {
    I_PicoSound_Shutdown();
    FX_Installed = 0;
    return FX_Ok;
}

int FX_SetCallBack(void (*function)(int32_t)) {
#if DISABLE_SOUND_CALLBACKS
    // Don't register callback - sounds will play but game won't be notified
    (void)function;
    return FX_Ok;
#else
    I_PicoSound_SetCallback(function);
    return FX_Ok;
#endif
}

void FX_SetVolume(int volume) {
    I_PicoSound_SetVolume(volume);
}

int FX_GetVolume(void) {
    return I_PicoSound_GetVolume();
}

void FX_SetReverseStereo(int setting) {
    I_PicoSound_SetReverseStereo(setting != 0);
}

int FX_GetReverseStereo(void) {
    return I_PicoSound_GetReverseStereo() ? 1 : 0;
}

void FX_SetReverb(int reverb) {
    // Not implemented
}

void FX_SetFastReverb(int reverb) {
    // Not implemented
}

int FX_GetMaxReverbDelay(void) {
    return 0;
}

int FX_GetReverbDelay(void) {
    return 0;
}

void FX_SetReverbDelay(int delay) {
    // Not implemented
}

int FX_VoiceAvailable(int priority) {
    return I_PicoSound_VoiceAvailable(priority) ? 1 : 0;
}

int FX_EndLooping(int handle) {
    I_PicoSound_EndLooping(handle);
    return FX_Ok;
}

int FX_SetPan(int handle, int vol, int left, int right) {
    I_PicoSound_SetPan(handle, vol, left, right);
    return FX_Ok;
}

int FX_SetPitch(int handle, int pitchoffset) {
    I_PicoSound_SetPitch(handle, pitchoffset);
    return FX_Ok;
}

int FX_SetFrequency(int handle, int frequency) {
    I_PicoSound_SetFrequency(handle, frequency);
    return FX_Ok;
}

// Get VOC file length (needed to estimate data size)
static uint32_t get_voc_data_length(uint8_t *ptr) {
    // Simple heuristic - look for end marker or use a reasonable max
    // VOC files start with "Creative Voice File"
    if (memcmp(ptr, "Creative Voice File\x1a", 20) != 0) {
        // Not a valid VOC, assume raw data
        return 65536;  // Default size
    }
    
    uint16_t header_size = ptr[20] | (ptr[21] << 8);
    uint8_t *block = ptr + header_size;
    uint32_t total_length = 0;
    
    // Parse blocks to find total length
    for (int i = 0; i < 100; i++) {  // Safety limit
        uint8_t block_type = block[0];
        if (block_type == 0) break;  // Terminator
        
        // Invalid block type - stop parsing
        if (block_type > 9) break;
        
        uint32_t block_size = block[1] | (block[2] << 8) | (block[3] << 16);
        
        // Sanity check - block size shouldn't be huge
        if (block_size > 1000000) break;  // 1MB max per block
        
        total_length = (block - ptr) + 4 + block_size;
        block += 4 + block_size;
    }
    
    return total_length > 0 ? total_length : 65536;
}

// Get WAV file length
static uint32_t get_wav_data_length(uint8_t *ptr) {
    if (memcmp(ptr, "RIFF", 4) != 0) return 65536;
    return 8 + (ptr[4] | (ptr[5] << 8) | (ptr[6] << 16) | (ptr[7] << 24));
}

int FX_PlayVOC(uint8_t *ptr, int pitchoffset, int vol, int left, int right,
               int priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    uint32_t length = get_voc_data_length(ptr);
    return I_PicoSound_PlayVOC(ptr, length, 0, pitchoffset, vol, left, right,
                               priority, callbackval, false, 0, 0);
#endif
}

int FX_PlayLoopedVOC(uint8_t *ptr, int32_t loopstart, int32_t loopend,
                     int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, 
                     int32_t priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    uint32_t length = get_voc_data_length(ptr);
    int result = I_PicoSound_PlayVOC(ptr, length, 0, pitchoffset, vol, left, right,
                               priority, callbackval, true, loopstart, loopend);
    return result;
#endif
}

int FX_PlayWAV(uint8_t *ptr, int pitchoffset, int vol, int left, int right,
               int priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    uint32_t length = get_wav_data_length(ptr);
    return I_PicoSound_PlayWAV(ptr, length, pitchoffset, vol, left, right,
                               priority, callbackval, false, 0, 0);
#endif
}

int FX_PlayLoopedWAV(uint8_t *ptr, int32_t loopstart, int32_t loopend,
                     int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, 
                     int32_t priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    uint32_t length = get_wav_data_length(ptr);
    return I_PicoSound_PlayWAV(ptr, length, pitchoffset, vol, left, right,
                               priority, callbackval, true, loopstart, loopend);
#endif
}

int FX_PlayVOC3D(uint8_t *ptr, int32_t pitchoffset, int32_t angle, int32_t distance,
                 int32_t priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    // Duke3D angle is 0-31 (after >>6 from 0-2047)
    // Duke3D distance is 0-255 (after >>6 from 0-16383)
    // angle 0 = front, 8 = right, 16 = back, 24 = left
    
    // Calculate volume based on distance (0=close/loud, larger=far/quiet)
    int vol = 255 - (distance * 2);  // distance is roughly 0-127 for audible
    if (vol < 32) vol = 32;  // Don't go completely silent
    if (vol > 255) vol = 255;
    
    // Stereo panning from angle (0-31 range)
    // 0 = front center, 8 = right, 16 = back center, 24 = left
    int left = 128;
    int right = 128;
    
    // Scale angle to 0-255 range for easier calculation
    int scaled_angle = (angle * 8) & 255;  // 0-31 -> 0-248
    
    if (scaled_angle < 64) {
        // Front-right: pan right
        right = 128 + scaled_angle * 2;
        left = 256 - right;
    } else if (scaled_angle < 128) {
        // Back-right: pan right  
        right = 128 + (128 - scaled_angle) * 2;
        left = 256 - right;
    } else if (scaled_angle < 192) {
        // Back-left: pan left
        left = 128 + (scaled_angle - 128) * 2;
        right = 256 - left;
    } else {
        // Front-left: pan left
        left = 128 + (256 - scaled_angle) * 2;
        right = 256 - left;
    }
    
    // Clamp
    if (left > 255) left = 255;
    if (right > 255) right = 255;
    if (left < 32) left = 32;
    if (right < 32) right = 32;
    
    // Apply volume
    left = (left * vol) / 255;
    right = (right * vol) / 255;
    
    uint32_t length = get_voc_data_length(ptr);
    
    return I_PicoSound_PlayVOC(ptr, length, 0, pitchoffset, vol, left, right,
                               priority, callbackval, false, 0, 0);
#endif
}

int FX_PlayWAV3D(uint8_t *ptr, int pitchoffset, int angle, int distance,
                 int priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    // Same calculation as VOC3D - Duke3D angle is 0-31
    int vol = 255 - (distance * 2);
    if (vol < 32) vol = 32;
    if (vol > 255) vol = 255;
    
    int left = 128;
    int right = 128;
    int scaled_angle = (angle * 8) & 255;
    
    if (scaled_angle < 64) {
        right = 128 + scaled_angle * 2;
        left = 256 - right;
    } else if (scaled_angle < 128) {
        right = 128 + (128 - scaled_angle) * 2;
        left = 256 - right;
    } else if (scaled_angle < 192) {
        left = 128 + (scaled_angle - 128) * 2;
        right = 256 - left;
    } else {
        left = 128 + (256 - scaled_angle) * 2;
        right = 256 - left;
    }
    
    if (left > 255) left = 255;
    if (right > 255) right = 255;
    if (left < 32) left = 32;
    if (right < 32) right = 32;
    
    left = (left * vol) / 255;
    right = (right * vol) / 255;
    
    uint32_t length = get_wav_data_length(ptr);
    return I_PicoSound_PlayWAV(ptr, length, pitchoffset, vol, left, right,
                               priority, callbackval, false, 0, 0);
#endif
}

int FX_PlayRaw(uint8_t *ptr, uint32_t length, uint32_t rate,
               int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, 
               int32_t priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    return I_PicoSound_PlayRaw(ptr, length, rate, pitchoffset, vol, left, right,
                               priority, callbackval, false, NULL, NULL);
#endif
}

int FX_PlayLoopedRaw(uint8_t *ptr, uint32_t length, char *loopstart,
                     char *loopend, uint32_t rate, int32_t pitchoffset, int32_t vol, 
                     int32_t left, int32_t right, int32_t priority, uint32_t callbackval) {
    if (!FX_Installed || !ptr) return 0;
    
#if DISABLE_SOUND_EFFECTS
    return 0;  // Disabled for debugging
#else
    return I_PicoSound_PlayRaw(ptr, length, rate, pitchoffset, vol, left, right,
                               priority, callbackval, true,
                               (const uint8_t *)loopstart, (const uint8_t *)loopend);
#endif
}

int32_t FX_Pan3D(int handle, int angle, int distance) {
    I_PicoSound_Pan3D(handle, angle, distance);
    return FX_Ok;
}

int32_t FX_SoundActive(int32_t handle) {
    return I_PicoSound_VoicePlaying(handle) ? 1 : 0;
}

int32_t FX_SoundsPlaying(void) {
    return I_PicoSound_VoicesPlaying();
}

int32_t FX_StopSound(int handle) {
    I_PicoSound_StopVoice(handle);
    return FX_Ok;
}

int32_t FX_StopAllSounds(void) {
    I_PicoSound_StopAllVoices();
    return FX_Ok;
}

int32_t FX_StartDemandFeedPlayback(void (*function)(char **ptr, uint32_t *length),
                                   int32_t rate, int32_t pitchoffset, int32_t vol, 
                                   int32_t left, int32_t right, int32_t priority, 
                                   uint32_t callbackval) {
    // Not implemented - would need streaming support
    return 0;
}

int FX_StartRecording(int MixRate, void (*function)(char *ptr, int length)) {
    // Not implemented
    return FX_Error;
}

void FX_StopRecord(void) {
    // Not implemented
}

// ============= MUSIC Implementation using OPL emulator =============

#include "i_music.h"

int MUSIC_ErrorCode = MUSIC_Ok;
static int music_loop_flag = 1;

char *MUSIC_ErrorString(int ErrorNumber) {
    switch (ErrorNumber) {
        case MUSIC_Ok: return "MUSIC ok";
        case MUSIC_Warning: return "MUSIC warning";
        case MUSIC_Error: return "MUSIC error";
        default: return "Unknown MUSIC error";
    }
}

int MUSIC_Init(int SoundCard, int Address) {
    if (I_Music_Init()) {
        return MUSIC_Ok;
    }
    return MUSIC_Error;
}

int MUSIC_Shutdown(void) {
    I_Music_Shutdown();
    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel) {
    // Not needed for OPL emulator
}

void MUSIC_SetVolume(int volume) {
    I_Music_SetVolume(volume);
}

void MUSIC_SetMidiChannelVolume(int channel, int volume) {
    // Per-channel volume not implemented
}

void MUSIC_ResetMidiChannelVolumes(void) {
    // Not implemented
}

int MUSIC_GetVolume(void) { 
    return I_Music_GetVolume(); 
}

void MUSIC_SetLoopFlag(int loopflag) {
    music_loop_flag = loopflag;
}

int MUSIC_SongPlaying(void) { 
    return I_Music_IsPlaying() ? 1 : 0; 
}

void MUSIC_Continue(void) {
    I_Music_Resume();
}

void MUSIC_Pause(void) {
    I_Music_Pause();
}

int MUSIC_StopSong(void) { 
    I_Music_Stop();
    return MUSIC_Ok; 
}

int MUSIC_PlaySong(char *song, int loopflag) { 
    // Note: Duke3D calls PlayMusic() which loads from file,
    // this function is for playing from memory which we don't support
    return MUSIC_Ok; 
}

void MUSIC_SetContext(int context) {
    // Not implemented
}

int MUSIC_GetContext(void) { 
    return 0; 
}

void MUSIC_SetSongTick(uint32_t PositionInTicks) {
    // Not implemented
}

void MUSIC_SetSongTime(uint32_t milliseconds) {
    // Not implemented
}

void MUSIC_SetSongPosition(int measure, int beat, int tick) {
    // Not implemented
}

void MUSIC_GetSongPosition(songposition *pos) {
    if (pos) memset(pos, 0, sizeof(*pos));
}

void MUSIC_GetSongLength(songposition *pos) {
    if (pos) memset(pos, 0, sizeof(*pos));
}

int MUSIC_FadeVolume(int tovolume, int milliseconds) { 
    // Instant volume change for now
    I_Music_SetVolume(tovolume);
    return MUSIC_Ok; 
}

int MUSIC_FadeActive(void) { 
    return 0; 
}

void MUSIC_StopFade(void) {
    // Nothing to do
}

void MUSIC_RerouteMidiChannel(int channel, int (*function)(int event, int c1, int c2)) {
    // Not implemented
}

void MUSIC_RegisterTimbreBank(uint8_t *timbres) {
    I_Music_RegisterTimbreBank(timbres);
}

