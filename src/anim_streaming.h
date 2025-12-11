/*
 * Streaming Animation Player for RP2350
 * 
 * Loads ANM files in chunks from SD card instead of loading entire file to memory.
 * Uses PSRAM for frame buffers.
 */

#ifndef ANIM_STREAMING_H
#define ANIM_STREAMING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize streaming animation from file
// Returns true on success
bool AnimStream_Open(const char *filename);

// Close streaming animation and free resources
void AnimStream_Close(void);

// Get number of frames in animation
int AnimStream_NumFrames(void);

// Get animation palette (768 bytes RGB)
uint8_t *AnimStream_GetPalette(void);

// Draw frame and return pointer to 320x200 8-bit image buffer
// Frame numbers start at 1
uint8_t *AnimStream_DrawFrame(int framenumber);

// Get animation dimensions
int AnimStream_GetWidth(void);
int AnimStream_GetHeight(void);

#ifdef __cplusplus
}
#endif

#endif // ANIM_STREAMING_H
