/*
 * Duke3D Sound System for RP2350
 * Based on murmdoom's i_picosound implementation
 * Uses I2S audio via PIO
 *
 * Copyright (C) 2024
 * Portions from murmdoom (C) 2021-2022 Graham Sanderson
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "i_picosound.h"
#include "board_config.h"

#define none pico_audio_enum_none
#include "pico/audio_i2s.h"
#undef none
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#ifndef INT16_MAX
#define INT16_MAX 32767
#endif
#ifndef INT16_MIN
#define INT16_MIN (-32768)
#endif

#ifndef PICO_AUDIO_I2S_DMA_CHANNEL
#define PICO_AUDIO_I2S_DMA_CHANNEL 10
#endif

#ifndef PICO_AUDIO_I2S_STATE_MACHINE
// NOTE: Use SM1 on PIO0 - PS/2 keyboard uses SM0
#define PICO_AUDIO_I2S_STATE_MACHINE 1
#endif

//=============================================================================
// Data Types
//=============================================================================

// Small decompressed buffer size - matches murmdoom's approach
// Buffer is refilled during mixing when exhausted
#define VOICE_BUFFER_SAMPLES 256

typedef struct voice_s {
    const uint8_t *data;           // Current position in source data (PSRAM)
    const uint8_t *data_end;       // End of sample data
    const uint8_t *loop_start;     // Loop start point (NULL if not looping)
    const uint8_t *loop_end;       // Loop end point
    
    // Local buffer for mixing (decompressed/converted samples)
    int8_t buffer[VOICE_BUFFER_SAMPLES];
    uint16_t buffer_size;          // Number of valid samples in buffer
    
    uint32_t offset;               // Current position in buffer (16.16 fixed point)
    uint32_t step;                 // Fixed-point step per output sample (16.16)
    
    uint8_t left_vol;              // Left channel volume (0-255)
    uint8_t right_vol;             // Right channel volume (0-255)
    uint8_t priority;              // Voice priority for allocation
    
    bool active;                   // Is this voice playing?
    bool looping;                  // Is this voice looping?
    bool is_16bit;                 // Is sample 16-bit? (false = 8-bit)
    bool is_signed;                // Is sample signed?
    bool is_adpcm;                 // Is sample ADPCM compressed?
    
    // Creative ADPCM decoder state
    uint8_t adpcm_pred;            // ADPCM predictor (0-255, unsigned)
    int adpcm_step;                // ADPCM step (0-3)
    
    uint32_t callback_val;         // Value to pass to callback
    
#if SOUND_LOW_PASS
    uint8_t alpha256;              // Low-pass filter coefficient
#endif
} voice_t;

//=============================================================================
// Static Variables
//=============================================================================

static struct audio_buffer_pool *producer_pool = NULL;
static bool sound_initialized = false;
static voice_t voices[NUM_SOUND_CHANNELS];
static int next_handle = 1;

static int master_volume = 255;
static bool reverse_stereo = false;
static void (*sound_callback)(int32_t) = NULL;
static void (*music_generator)(audio_buffer_t *buffer) = NULL;

// Debug: track mix iterations
static volatile uint32_t mix_iteration_count = 0;
static volatile uint32_t last_reported_mix = 0;

// Deferred callback queue to avoid calling game code from mixer
#define MAX_PENDING_CALLBACKS 32
static volatile uint32_t pending_callbacks[MAX_PENDING_CALLBACKS];
static volatile int pending_callback_head = 0;
static volatile int pending_callback_tail = 0;
static volatile bool processing_callbacks = false;

//=============================================================================
// Creative ADPCM Decoder (VOC codec 4 = Creative 4-bit ADPCM)
// Using DOSBox's table-based algorithm which is known to work correctly
//=============================================================================

// DOSBox's ScaleMap for 4-bit ADPCM (64 entries = 4 steps x 16 nibble values)
static const int8_t adpcm4_scale_map[64] = {
    0,  1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
    1,  3,  5,  7,  9, 11, 13, 15, -1,  -3,  -5,  -7,  -9, -11, -13, -15,
    2,  6, 10, 14, 18, 22, 26, 30, -2,  -6, -10, -14, -18, -22, -26, -30,
    4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
};

// DOSBox's AdjustMap for 4-bit ADPCM - use signed for proper math
// Values are: 0 (no change), +16 (step up), -16 (step down)
static const int8_t adpcm4_adjust_map[64] = {
      0,  0,  0,  0,  0, 16, 16, 16,
      0,  0,  0,  0,  0, 16, 16, 16,
    -16,  0,  0,  0,  0, 16, 16, 16,
    -16,  0,  0,  0,  0, 16, 16, 16,
    -16,  0,  0,  0,  0, 16, 16, 16,
    -16,  0,  0,  0,  0, 16, 16, 16,
    -16,  0,  0,  0,  0,  0,  0,  0,
    -16,  0,  0,  0,  0,  0,  0,  0
};

// Debug counter for ADPCM decoding
static int adpcm_debug_count = 0;

// Decode one nibble using DOSBox's table-based algorithm
// reference is 0-255 (unsigned 8-bit sample), stepsize is 0, 16, 32, or 48
static uint8_t decode_creative_adpcm_nibble(int nibble, uint8_t *reference, int *stepsize) {
    // Index into 64-entry table: nibble (0-15) + stepsize (0, 16, 32, 48)
    int i = nibble + *stepsize;
    if (i < 0) i = 0;
    if (i > 63) i = 63;
    
    int old_step = *stepsize;
    int old_ref = *reference;
    
    // Update stepsize using adjust map (signed, so -16 works correctly)
    *stepsize = *stepsize + adpcm4_adjust_map[i];
    
    // Clamp stepsize to valid range (0, 16, 32, 48)
    if (*stepsize < 0) *stepsize = 0;
    if (*stepsize > 48) *stepsize = 48;
    
    // Update reference sample
    int new_ref = (int)*reference + adpcm4_scale_map[i];
    if (new_ref < 0) new_ref = 0;
    if (new_ref > 255) new_ref = 255;
    *reference = (uint8_t)new_ref;
    
    // Debug first 20 samples
    if (adpcm_debug_count < 20) {
        printf("ADPCM[%d]: nib=%d i=%d ref:%d->%d step:%d->%d scale=%d adj=%d\n",
               adpcm_debug_count, nibble, i, old_ref, *reference, old_step, *stepsize,
               adpcm4_scale_map[i], adpcm4_adjust_map[i]);
        adpcm_debug_count++;
    }
    
    return *reference;
}

static struct audio_format audio_format = {
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,
    .sample_freq = PICO_SOUND_SAMPLE_FREQ,
    .channel_count = 2,
};

static struct audio_buffer_format producer_format = {
    .format = &audio_format,
    .sample_stride = 4  // 2 channels * 2 bytes per sample
};

//=============================================================================
// Utility Functions
//=============================================================================

static inline int16_t clamp_s16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return (int16_t)v;
}

static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// Find a free voice slot or steal one based on priority
static int find_voice_slot(int priority) {
    // First, look for an inactive voice
    for (int i = 0; i < NUM_SOUND_CHANNELS; i++) {
        if (!voices[i].active) {
            return i;
        }
    }
    
    // No free slots, try to steal a lower priority voice
    int lowest_priority = priority;
    int lowest_slot = -1;
    
    for (int i = 0; i < NUM_SOUND_CHANNELS; i++) {
        if (voices[i].priority < lowest_priority) {
            lowest_priority = voices[i].priority;
            lowest_slot = i;
        }
    }
    
    return lowest_slot;
}

// Convert handle to voice index
static int handle_to_voice(int handle) {
    if (handle <= 0) return -1;
    // Handle encodes voice index in lower bits
    int voice_idx = (handle - 1) % NUM_SOUND_CHANNELS;
    if (!voices[voice_idx].active) return -1;
    return voice_idx;
}

// Queue a callback to be called later (from game thread, not mixer)
static void queue_callback(uint32_t callback_val) {
    int next_tail = (pending_callback_tail + 1) % MAX_PENDING_CALLBACKS;
    if (next_tail != pending_callback_head) {
        pending_callbacks[pending_callback_tail] = callback_val;
        pending_callback_tail = next_tail;
    }
}

// Process any pending callbacks (called from I_PicoSound_Update)
static void process_pending_callbacks(void) {
    // Prevent re-entrancy - callback might trigger another sound that finishes
    if (processing_callbacks) {
        static int reentrant_warns = 0;
        if (reentrant_warns++ < 3) {
            printf("CALLBACK: Re-entrancy blocked!\n");
        }
        return;
    }
    processing_callbacks = true;
    
    // Process up to a limited number to prevent infinite loops
    int processed = 0;
    while (pending_callback_head != pending_callback_tail && processed < 8) {
        uint32_t cb_val = pending_callbacks[pending_callback_head];
        pending_callback_head = (pending_callback_head + 1) % MAX_PENDING_CALLBACKS;
        processed++;
        
        if (sound_callback) {
            sound_callback(cb_val);
        }
    }
    
    processing_callbacks = false;
}

// Decompress/copy next block of samples into voice buffer
// Called when buffer is exhausted during mixing (murmdoom pattern)
static void decompress_buffer(voice_t *v) {
    // Validate pointers
    if (!v || !v->data || !v->data_end || v->data_end < v->data) {
        printf("DECOMPRESS: invalid ptrs data=%p end=%p\n", 
               v ? (void*)v->data : NULL, v ? (void*)v->data_end : NULL);
        if (v) v->buffer_size = 0;
        return;
    }
    
    if (v->data >= v->data_end) {
        // Removed verbose logging - Check for loop
        if (v->looping && v->loop_start) {
            v->data = v->loop_start;
            if (v->is_adpcm) {
                v->adpcm_pred = 128;  // Placeholder (first byte will replace)
                v->adpcm_step = -1;   // -1 = needs to read first byte
            }
        } else {
            v->buffer_size = 0;
            return;
        }
    }
    
    int samples_decoded = 0;
    
    if (v->is_adpcm) {
        // Creative 4-bit ADPCM: first byte is raw sample (initialization)
        // Check if we need to read initial sample (-1 means uninitialized)
        if (v->adpcm_step < 0 && v->data < v->data_end) {
            // First byte is the initial raw sample (reference)
            v->adpcm_pred = *v->data++;
            // stepsize starts at 0 (DOSBox uses 0, 16, 32, 48 for steps)
            v->adpcm_step = 0;
            // Reset debug counter for new sound
            adpcm_debug_count = 0;
            printf("ADPCM START: initial ref=%d\n", v->adpcm_pred);
            // Print first 16 bytes of data for analysis
            printf("ADPCM DATA:");
            for (int db = 0; db < 16 && v->data + db < v->data_end; db++) {
                printf(" %02X", v->data[db]);
            }
            printf("\n");
        }
        
        // Decode ADPCM: each byte contains 2 nibbles (2 samples)
        // HIGH nibble first, then LOW nibble (Creative/DOSBox order)
        while (samples_decoded < VOICE_BUFFER_SAMPLES && v->data < v->data_end) {
            uint8_t byte = *v->data++;
            
            // HIGH nibble first (bits 4-7)
            int nibble = (byte >> 4) & 0x0F;
            uint8_t sample = decode_creative_adpcm_nibble(nibble, &v->adpcm_pred, &v->adpcm_step);
            // Convert unsigned 0-255 to signed -128 to 127
            v->buffer[samples_decoded++] = (int8_t)(sample - 128);
            
            if (samples_decoded >= VOICE_BUFFER_SAMPLES) break;
            
            // LOW nibble (bits 0-3)
            nibble = byte & 0x0F;
            sample = decode_creative_adpcm_nibble(nibble, &v->adpcm_pred, &v->adpcm_step);
            v->buffer[samples_decoded++] = (int8_t)(sample - 128);
        }
    } else {
        // PCM: copy up to VOICE_BUFFER_SAMPLES
        int available;
        if (v->is_16bit) {
            available = (v->data_end - v->data) / 2;
        } else {
            available = v->data_end - v->data;
        }
        
        int to_copy = available;
        if (to_copy > VOICE_BUFFER_SAMPLES) {
            to_copy = VOICE_BUFFER_SAMPLES;
        }
        
        // Removed verbose decompress logging
        
        if (to_copy <= 0) {
            v->buffer_size = 0;
            return;
        }
        
        if (v->is_16bit) {
            const int16_t *src = (const int16_t *)v->data;
            for (int i = 0; i < to_copy; i++) {
                v->buffer[i] = src[i] >> 8;
            }
            v->data += to_copy * 2;
        } else if (v->is_signed) {
            const int8_t *src = (const int8_t *)v->data;
            for (int i = 0; i < to_copy; i++) {
                v->buffer[i] = src[i];
            }
            v->data += to_copy;
        } else {
            // 8-bit unsigned to signed
            const uint8_t *src = v->data;
            for (int i = 0; i < to_copy; i++) {
                v->buffer[i] = (int8_t)(src[i] - 128);
            }
            v->data += to_copy;
        }
        
        samples_decoded = to_copy;
    }
    
    v->buffer_size = samples_decoded;
}

// Stop a voice and optionally queue callback
static void stop_voice(int voice_idx, bool do_callback) {
    if (voice_idx < 0 || voice_idx >= NUM_SOUND_CHANNELS) return;
    
    voice_t *v = &voices[voice_idx];
    uint32_t cb_val = v->callback_val;
    bool was_active = v->active;
    
    // Mark inactive
    v->active = false;
    
    // Queue callback if sound was playing and callback requested
    if (was_active && do_callback && cb_val != 0) {
        queue_callback(cb_val);
    }
}

//=============================================================================
// VOC/WAV Parsing
//=============================================================================

// Parse VOC file header and return sample data info
// VOC format: "Creative Voice File" header, then blocks
// Codec: 0=PCM, 4=ADPCM (4-bit)
static bool parse_voc(const uint8_t *data, uint32_t length,
                      const uint8_t **sample_data, uint32_t *sample_length,
                      uint32_t *sample_rate, bool *is_16bit, uint8_t *out_codec) {
    // Check for "Creative Voice File" header
    if (length < 26) return false;
    if (memcmp(data, "Creative Voice File\x1a", 20) != 0) return false;
    
    uint16_t header_size = read_le16(data + 20);
    if (header_size > length) return false;
    
    const uint8_t *block = data + header_size;
    const uint8_t *end = data + length;
    
    *is_16bit = false;
    *out_codec = 0;
    
    // Parse blocks to find sound data
    while (block < end) {
        uint8_t block_type = block[0];
        
        if (block_type == 0) {
            // Terminator
            break;
        }
        
        if (block + 4 > end) break;
        
        uint32_t block_size = block[1] | (block[2] << 8) | (block[3] << 16);
        const uint8_t *block_data = block + 4;
        
        if (block_data + block_size > end) break;
        
        switch (block_type) {
            case 1: // Sound data
                if (block_size < 2) break;
                {
                    uint8_t freq_div = block_data[0];
                    uint8_t codec = block_data[1];
                    
                    printf("VOC block 1: freq_div=%d codec=%d size=%u\n", freq_div, codec, (unsigned)block_size);
                    
                    // Support codec 0 (PCM) and codec 4 (4-bit ADPCM)
                    if (codec != 0 && codec != 4) {
                        printf("VOC: Unsupported codec %d\n", codec);
                        break;
                    }
                    
                    *out_codec = codec;
                    *sample_rate = 1000000 / (256 - freq_div);
                    *sample_data = block_data + 2;
                    *sample_length = block_size - 2;
                    return true;
                }
                
            case 9: // Sound data (new format)
                if (block_size < 12) break;
                {
                    *sample_rate = read_le32(block_data);
                    uint8_t bits = block_data[4];
                    uint8_t channels = block_data[5];
                    uint16_t codec = read_le16(block_data + 6);
                    
                    printf("VOC block 9: rate=%u bits=%d ch=%d codec=%d size=%u\n", 
                           (unsigned)*sample_rate, bits, channels, codec, (unsigned)block_size);
                    
                    // Block type 9 codecs:
                    // 0 = 8-bit unsigned PCM
                    // 4 = 16-bit signed PCM (NOT ADPCM!)
                    // ADPCM is only valid for block type 1
                    if (codec != 0 && codec != 4) {
                        printf("VOC: Unsupported codec %d\n", codec);
                        break;
                    }
                    if (channels != 1) {
                        printf("VOC: Multi-channel not supported\n");
                        break;
                    }
                    
                    // For block type 9, codec 4 = 16-bit signed PCM, NOT ADPCM
                    *out_codec = 0;  // Always PCM for block type 9
                    *is_16bit = (bits == 16) || (codec == 4);
                    *sample_data = block_data + 12;
                    *sample_length = block_size - 12;
                    return true;
                }
        }
        
        block = block_data + block_size;
    }
    
    return false;
}

// Parse WAV file header
static bool parse_wav(const uint8_t *data, uint32_t length,
                      const uint8_t **sample_data, uint32_t *sample_length,
                      uint32_t *sample_rate, bool *is_16bit, bool *is_signed) {
    if (length < 44) return false;
    
    // Check RIFF header
    if (memcmp(data, "RIFF", 4) != 0) return false;
    if (memcmp(data + 8, "WAVE", 4) != 0) return false;
    
    const uint8_t *ptr = data + 12;
    const uint8_t *end = data + length;
    
    uint32_t fmt_sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint16_t audio_format = 0;
    bool found_fmt = false;
    
    // Parse chunks
    while (ptr + 8 <= end) {
        uint32_t chunk_id = read_le32(ptr);
        uint32_t chunk_size = read_le32(ptr + 4);
        const uint8_t *chunk_data = ptr + 8;
        
        if (chunk_data + chunk_size > end) break;
        
        if (chunk_id == 0x20746D66) {  // "fmt "
            if (chunk_size < 16) return false;
            
            audio_format = read_le16(chunk_data);
            uint16_t channels = read_le16(chunk_data + 2);
            fmt_sample_rate = read_le32(chunk_data + 4);
            bits_per_sample = read_le16(chunk_data + 14);
            
            if (audio_format != 1) {
                printf("WAV: Only PCM format supported\n");
                return false;
            }
            if (channels != 1) {
                printf("WAV: Only mono supported, got %d channels\n", channels);
                // Continue anyway, we'll just use left channel
            }
            
            found_fmt = true;
        }
        else if (chunk_id == 0x61746164) {  // "data"
            if (!found_fmt) return false;
            
            *sample_data = chunk_data;
            *sample_length = chunk_size;
            *sample_rate = fmt_sample_rate;
            *is_16bit = (bits_per_sample == 16);
            *is_signed = (bits_per_sample == 16);  // 16-bit WAV is signed, 8-bit is unsigned
            return true;
        }
        
        ptr = chunk_data + chunk_size;
        // Pad to word boundary
        if (chunk_size & 1) ptr++;
    }
    
    return false;
}

//=============================================================================
// Audio Mixing
//=============================================================================

static void mix_audio_buffer(audio_buffer_t *buffer) {
    mix_iteration_count++;
    
    int16_t *samples = (int16_t *)buffer->buffer->bytes;
    int sample_count = buffer->max_sample_count;
    
    // Start with silence or music
    if (music_generator) {
        music_generator(buffer);
    } else {
        memset(samples, 0, sample_count * 4);  // 2 channels * 2 bytes
    }
    
    // Mix in all active voices (murmdoom pattern: decompress inline)
    int active_channels = 0;
    for (int ch = 0; ch < NUM_SOUND_CHANNELS; ch++) {
        voice_t *v = &voices[ch];
        if (!v->active) continue;
        if (v->buffer_size == 0) continue;
        active_channels++;
        
        int voll = v->left_vol / 2;  // Match murmdoom's volume scaling
        int volr = v->right_vol / 2;
        
        if (reverse_stereo) {
            int tmp = voll;
            voll = volr;
            volr = tmp;
        }
        
        uint32_t offset_end = v->buffer_size * 65536;
        int16_t *out = samples;
        
        // Safety check - offset should never exceed buffer
        if ((v->offset >> 16) >= VOICE_BUFFER_SAMPLES) {
            printf("MIX OVERFLOW: ch=%d offset=%u buf_size=%u\n", ch, v->offset >> 16, v->buffer_size);
            v->offset = 0;
        }
        
#if SOUND_LOW_PASS
        int alpha256 = v->alpha256;
        int beta256 = 256 - alpha256;
        int sample = v->buffer[v->offset >> 16];
#endif
        
        int decompress_calls = 0;  // Track decompress calls per voice per buffer
        
        for (int s = 0; s < sample_count; s++) {
            // Bounds check buffer access
            uint32_t buf_idx = v->offset >> 16;
            if (buf_idx >= VOICE_BUFFER_SAMPLES) {
                printf("MIX IDX OVERFLOW: ch=%d idx=%u\n", ch, buf_idx);
                v->active = false;
                break;
            }
            
#if !SOUND_LOW_PASS
            int sample = v->buffer[buf_idx];
#else
            sample = (beta256 * sample + alpha256 * v->buffer[buf_idx]) / 256;
#endif
            
            // Mix into output - both channels should get audio
            // Write to BOTH out[0] and out[1]
            int32_t mixed_0 = out[0];
            int32_t mixed_1 = out[1];
            
            // Apply same sample to both channels with their respective volumes
            mixed_0 += sample * voll;
            mixed_1 += sample * volr;
            
            out[0] = clamp_s16(mixed_0);
            out[1] = clamp_s16(mixed_1);
            
            out += 2;
            v->offset += v->step;
            
            // Buffer exhausted - decompress next block
            if (v->offset >= offset_end) {
                v->offset -= offset_end;
                
                // Safety: limit decompress calls per voice to prevent infinite loop
                decompress_calls++;
                if (decompress_calls > 20) {
                    printf("MIX: too many decompress ch=%d, stopping\n", ch);
                    v->active = false;
                    break;
                }
                
                decompress_buffer(v);  // Read from PSRAM here
                
                offset_end = v->buffer_size * 65536;
                if (offset_end == 0) {
                    // Sound finished or buffer empty - queue callback
                    if (v->callback_val != 0) {
                        queue_callback(v->callback_val);
                    }
                    v->active = false;
                    break;
                }
                // Clamp offset to new buffer size
                if (v->offset >= offset_end) {
                    v->offset = 0;
                }
            }
        }
    }
    
    buffer->sample_count = sample_count;
    give_audio_buffer(producer_pool, buffer);
}

//=============================================================================
// Public Interface
//=============================================================================

bool I_PicoSound_Init(int numvoices, int mixrate) {
    if (sound_initialized) return true;
    
    // Create audio buffer pool (4 buffers for smooth playback)
    producer_pool = audio_new_producer_pool(&producer_format, 4, PICO_SOUND_BUFFER_SAMPLES);
    if (!producer_pool) {
        return false;
    }
    
    // Configure I2S
    struct audio_i2s_config config = {
        .data_pin = I2S_DATA_PIN,
        .clock_pin_base = I2S_CLOCK_PIN_BASE,
        .dma_channel = PICO_AUDIO_I2S_DMA_CHANNEL,
        .pio_sm = PICO_AUDIO_I2S_STATE_MACHINE,
    };
    
    const struct audio_format *output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format) {
        return false;
    }
    
#if INCREASE_I2S_DRIVE_STRENGTH
    // Increase drive strength for cleaner signal
    gpio_set_drive_strength(I2S_DATA_PIN, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(I2S_CLOCK_PIN_BASE, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(I2S_CLOCK_PIN_BASE + 1, GPIO_DRIVE_STRENGTH_12MA);
#endif
    
    // Connect producer to I2S consumer
    bool ok = audio_i2s_connect_extra(producer_pool, false, 0, 0, NULL);
    if (!ok) {
        return false;
    }
    
    // Enable I2S output
    audio_i2s_set_enabled(true);
    
    // Initialize voices
    memset(voices, 0, sizeof(voices));
    
    sound_initialized = true;
    return true;
}

void I_PicoSound_Shutdown(void) {
    if (!sound_initialized) return;
    
    audio_i2s_set_enabled(false);
    sound_initialized = false;
}

// Forward declaration
extern void psram_print_stats(void);

void I_PicoSound_Update(void) {
    if (!sound_initialized) return;
    
#if 0  // Disable periodic status debug output
    // Periodic status report to detect hangs and memory issues
    static uint32_t update_count = 0;
    update_count++;
    if ((update_count % 500) == 0) {
        printf("SND: update=%lu mix=%lu\n", (unsigned long)update_count, (unsigned long)mix_iteration_count);
        psram_print_stats();
    }
#endif
    
    // Process audio buffers - decompress_buffer is called inline during mixing
    // This is the murmdoom pattern: PSRAM access happens inside the mix loop
    audio_buffer_t *buffer;
    int buffers_processed = 0;
    while ((buffer = take_audio_buffer(producer_pool, false)) != NULL) {
        mix_audio_buffer(buffer);
        buffers_processed++;
        if (buffers_processed > 10) {
            break;
        }
    }
    
    // Process any pending callbacks from finished sounds
    process_pending_callbacks();
}

bool I_PicoSound_IsInitialized(void) {
    return sound_initialized;
}

int I_PicoSound_PlayVOC(const uint8_t *data, uint32_t length,
                        int samplerate, int pitchoffset,
                        int vol, int left, int right,
                        int priority, uint32_t callbackval,
                        bool looping, uint32_t loopstart, uint32_t loopend) {
    if (!sound_initialized) return 0;
    
    const uint8_t *sample_data;
    uint32_t sample_length, sample_rate;
    bool is_16bit;
    uint8_t codec = 0;
    
    // Parse VOC header
    if (!parse_voc(data, length, &sample_data, &sample_length, &sample_rate, &is_16bit, &codec)) {
        // Debug: show what data we got if VOC parsing fails
        if (callbackval == 109) {  // SHOTGUN_FIRE
            printf("VOC PARSE FAIL: first20='%.20s'\n", (const char*)data);
        }
        // Fallback: treat entire data as raw 8-bit unsigned samples
        sample_data = data;
        sample_length = length;
        sample_rate = samplerate > 0 ? samplerate : 11025;
        is_16bit = false;
        codec = 0;
    } else {
        // Debug: show parsed VOC info
        if (callbackval == 109) {  // SHOTGUN_FIRE
            printf("VOC OK: rate=%u len=%u codec=%d\n", 
                   (unsigned)sample_rate, (unsigned)sample_length, codec);
        }
    }
    
    // For ADPCM, we need to handle it specially
    bool is_adpcm = (codec == 4);
    
    // Find a voice slot
    int slot = find_voice_slot(priority);
    if (slot < 0) return 0;
    
    voice_t *v = &voices[slot];
    stop_voice(slot, true);
    
    v->data = sample_data;
    v->data_end = sample_data + sample_length;
    
    // NOTE: Duke3D's loopstart/loopend are calculated incorrectly (file offsets + file size)
    // For looping sounds, we simply loop the entire parsed sample data
    v->loop_start = looping ? sample_data : NULL;
    v->loop_end = looping ? sample_data + sample_length : NULL;
    v->looping = looping;
    
    v->is_16bit = is_16bit;
    v->is_signed = false;  // VOC 8-bit is unsigned
    v->is_adpcm = is_adpcm;
    
    // Initialize Creative ADPCM state
    if (is_adpcm) {
        v->adpcm_pred = 128;  // Placeholder (first byte will replace)
        v->adpcm_step = -1;   // -1 = needs to read first byte
    }
    
    // Decompress first buffer block
    decompress_buffer(v);
    v->offset = 0;
    
    // Calculate step: input_rate / output_rate in 16.16 fixed point
    int32_t rate = (int32_t)sample_rate;
    if (pitchoffset != 0) {
        rate = rate + (rate * pitchoffset / 2048);
        if (rate < 1000) rate = 1000;
        if (rate > 48000) rate = 48000;
    }
    v->step = ((uint64_t)rate << 16) / PICO_SOUND_SAMPLE_FREQ;
    
    // If left/right are both 0 or very low but vol is set, use vol for both
    if (left <= 0 && right <= 0 && vol > 0) {
        left = vol;
        right = vol;
    }
    // Amplify volumes - Duke3D uses very low values
    left = left * 4;
    right = right * 4;
    v->left_vol = left > 255 ? 255 : (left < 0 ? 0 : left);
    v->right_vol = right > 255 ? 255 : (right < 0 ? 0 : right);
    v->priority = priority;
    v->callback_val = callbackval;

#if SOUND_LOW_PASS
    v->alpha256 = (256 * 201 * sample_rate) / (201 * sample_rate + 64 * PICO_SOUND_SAMPLE_FREQ);
#endif
    
    v->active = true;
    
    int handle = (next_handle++ % 10000) * NUM_SOUND_CHANNELS + slot + 1;
    return handle;
}

int I_PicoSound_PlayWAV(const uint8_t *data, uint32_t length,
                        int pitchoffset,
                        int vol, int left, int right,
                        int priority, uint32_t callbackval,
                        bool looping, uint32_t loopstart, uint32_t loopend) {
    if (!sound_initialized) return 0;
    
    const uint8_t *sample_data;
    uint32_t sample_length, sample_rate;
    bool is_16bit, is_signed;
    
    if (!parse_wav(data, length, &sample_data, &sample_length, &sample_rate, &is_16bit, &is_signed)) {
        printf("I_PicoSound_PlayWAV: Failed to parse WAV\n");
        return 0;
    }
    
    // Find a voice slot
    int slot = find_voice_slot(priority);
    if (slot < 0) return 0;
    
    voice_t *v = &voices[slot];
    stop_voice(slot, true);  // Stop any previous sound
    
    v->data = sample_data;
    v->data_end = sample_data + sample_length;
    
    // NOTE: Duke3D's loopstart/loopend are calculated incorrectly (file offsets + file size)
    // For looping sounds, we simply loop the entire parsed sample data
    v->loop_start = looping ? sample_data : NULL;
    v->loop_end = looping ? sample_data + sample_length : NULL;
    v->looping = looping;
    
    v->is_16bit = is_16bit;
    v->is_signed = is_signed;
    v->is_adpcm = false;  // WAV files are not ADPCM
    
    // Decompress first buffer block
    decompress_buffer(v);
    v->offset = 0;
    
    // Calculate step: input_rate / output_rate in 16.16 fixed point
    // Apply pitch offset (signed operation to handle negative pitch)
    int32_t rate = (int32_t)sample_rate;
    if (pitchoffset != 0) {
        // Duke3D pitch is in the range of about -2048 to 2048
        rate = rate + (rate * pitchoffset / 2048);
        if (rate < 1000) rate = 1000;  // Clamp to reasonable minimum
        if (rate > 48000) rate = 48000; // Clamp to reasonable maximum
    }
    v->step = ((uint64_t)rate << 16) / PICO_SOUND_SAMPLE_FREQ;
    
    // If left/right are both 0 or very low but vol is set, use vol for both
    if (left <= 0 && right <= 0 && vol > 0) {
        left = vol;
        right = vol;
    }
    // Amplify volumes - Duke3D uses very low values
    left = left * 4;
    right = right * 4;
    v->left_vol = left > 255 ? 255 : (left < 0 ? 0 : left);
    v->right_vol = right > 255 ? 255 : (right < 0 ? 0 : right);
    v->priority = priority;
    v->callback_val = callbackval;

#if SOUND_LOW_PASS
    v->alpha256 = (256 * 201 * sample_rate) / (201 * sample_rate + 64 * PICO_SOUND_SAMPLE_FREQ);
#endif
    
    v->active = true;
    
    int handle = (next_handle++ % 10000) * NUM_SOUND_CHANNELS + slot + 1;
    return handle;
}

int I_PicoSound_PlayRaw(const uint8_t *data, uint32_t length,
                        uint32_t samplerate, int pitchoffset,
                        int vol, int left, int right,
                        int priority, uint32_t callbackval,
                        bool looping, const uint8_t *loopstart, const uint8_t *loopend) {
    if (!sound_initialized) return 0;
    if (!data || length == 0) return 0;
    
    // Find a voice slot
    int slot = find_voice_slot(priority);
    if (slot < 0) {
        return 0;
    }
    
    voice_t *v = &voices[slot];
    stop_voice(slot, true);  // Stop any previous sound
    
    v->data = data;
    v->data_end = data + length;
    v->loop_start = loopstart;
    v->loop_end = loopend;
    v->looping = looping;
    
    v->is_16bit = false;  // Raw data assumed to be 8-bit unsigned
    v->is_signed = false;
    v->is_adpcm = false;  // Raw data is not ADPCM
    
    // Decompress first buffer block
    decompress_buffer(v);
    v->offset = 0;
    
    // Calculate step: input_rate / output_rate in 16.16 fixed point
    int32_t rate = (int32_t)samplerate;
    if (pitchoffset != 0) {
        rate = rate + (rate * pitchoffset / 2048);
        if (rate < 1000) rate = 1000;
        if (rate > 48000) rate = 48000;
    }
    v->step = ((uint64_t)rate << 16) / PICO_SOUND_SAMPLE_FREQ;
    
    // If left/right are both 0 or very low but vol is set, use vol for both
    if (left <= 0 && right <= 0 && vol > 0) {
        left = vol;
        right = vol;
    }
    // Amplify volumes - Duke3D uses very low values
    left = left * 4;
    right = right * 4;
    v->left_vol = left > 255 ? 255 : (left < 0 ? 0 : left);
    v->right_vol = right > 255 ? 255 : (right < 0 ? 0 : right);
    v->priority = priority;
    v->callback_val = callbackval;
    
#if SOUND_LOW_PASS
    v->alpha256 = (256 * 201 * samplerate) / (201 * samplerate + 64 * PICO_SOUND_SAMPLE_FREQ);
#endif
    
    v->active = true;
    
    int handle = (next_handle++ % 10000) * NUM_SOUND_CHANNELS + slot + 1;
    
    return handle;
}

int I_PicoSound_StopVoice(int handle) {
    int slot = handle_to_voice(handle);
    if (slot >= 0) {
        stop_voice(slot, false);  // Don't call callback when explicitly stopped
        return 1;
    }
    return 0;
}

void I_PicoSound_StopAllVoices(void) {
    for (int i = 0; i < NUM_SOUND_CHANNELS; i++) {
        stop_voice(i, false);
    }
}

bool I_PicoSound_VoicePlaying(int handle) {
    int slot = handle_to_voice(handle);
    return slot >= 0 && voices[slot].active;
}

int I_PicoSound_VoicesPlaying(void) {
    int count = 0;
    for (int i = 0; i < NUM_SOUND_CHANNELS; i++) {
        if (voices[i].active) count++;
    }
    return count;
}

bool I_PicoSound_VoiceAvailable(int priority) {
    return find_voice_slot(priority) >= 0;
}

void I_PicoSound_SetPan(int handle, int vol, int left, int right) {
    int slot = handle_to_voice(handle);
    if (slot < 0) return;
    
    voices[slot].left_vol = left > 255 ? 255 : (left < 0 ? 0 : left);
    voices[slot].right_vol = right > 255 ? 255 : (right < 0 ? 0 : right);
}

void I_PicoSound_SetPitch(int handle, int pitchoffset) {
    // Not implemented - would need to recalculate step
}

void I_PicoSound_SetFrequency(int handle, int frequency) {
    int slot = handle_to_voice(handle);
    if (slot < 0) return;
    
    voices[slot].step = ((uint32_t)frequency << 16) / PICO_SOUND_SAMPLE_FREQ;
}

void I_PicoSound_EndLooping(int handle) {
    int slot = handle_to_voice(handle);
    if (slot < 0) return;
    
    voices[slot].looping = false;
    voices[slot].loop_start = NULL;
}

void I_PicoSound_Pan3D(int handle, int angle, int distance) {
    int slot = handle_to_voice(handle);
    if (slot < 0) return;
    
    // Convert angle (0-255) and distance (0-255) to left/right volumes
    // angle 0 = front, 64 = right, 128 = back, 192 = left
    
    // Simple implementation: angle affects pan, distance affects volume
    int vol = 255 - distance;
    if (vol < 0) vol = 0;
    
    // Calculate stereo pan from angle
    int pan;
    if (angle < 128) {
        pan = angle * 2;  // 0-255 from left to right
    } else {
        pan = (256 - angle) * 2;
    }
    
    voices[slot].left_vol = (vol * (255 - pan)) >> 8;
    voices[slot].right_vol = (vol * pan) >> 8;
}

void I_PicoSound_SetVolume(int volume) {
    master_volume = volume > 255 ? 255 : (volume < 0 ? 0 : volume);
}

int I_PicoSound_GetVolume(void) {
    return master_volume;
}

void I_PicoSound_SetReverseStereo(bool reverse) {
    reverse_stereo = reverse;
}

bool I_PicoSound_GetReverseStereo(void) {
    return reverse_stereo;
}

void I_PicoSound_SetCallback(void (*callback)(int32_t)) {
    sound_callback = callback;
}

void I_PicoSound_SetMusicGenerator(void (*generator)(audio_buffer_t *buffer)) {
    music_generator = generator;
}
