/*
 * Main SDL RP2350 implementation
 * Core SDL functions
 */
#include "SDL.h"
#include "pico/stdlib.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include "board_config.h"
#include "sdcard.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"
#include <stdio.h>

static int sdl_initialized = 0;
static char error_string[256] = "";
extern void stdio_fatfs_init(void);

// Global FatFs object
static FATFS fs;

int SDL_Init(Uint32 flags) {
    if (sdl_initialized) {
        return 0;
    }
    
    // Initialize PSRAM
    uint psram_pin = get_psram_pin();
    psram_init(psram_pin);
    
    // Mount SD Card
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        snprintf(error_string, sizeof(error_string), "Failed to mount SD card: %d", fr);
        return -1;
    }
    
    // Initialize stdio wrapper for FatFS
    stdio_fatfs_init();
    
    // Initialize PS/2 Keyboard and Mouse (unified driver)
    ps2kbd_init();

    sdl_initialized = 1;
    
    return 0;
}

void SDL_Quit(void) {
    sdl_initialized = 0;
}

const char *SDL_GetError(void) {
    return error_string;
}

void SDL_Delay(Uint32 ms) {
    sleep_ms(ms);
}

static SDL_version linked_version = { 1, 2, 15 };

const SDL_version *SDL_Linked_Version(void) {
    return &linked_version;
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color) {
    if (!dst || !dst->pixels) return -1;
    
    int x, y, w, h;
    
    if (dstrect) {
        x = dstrect->x;
        y = dstrect->y;
        w = dstrect->w;
        h = dstrect->h;
    } else {
        x = 0;
        y = 0;
        w = dst->w;
        h = dst->h;
    }
    
    // Clip to surface bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > dst->w) w = dst->w - x;
    if (y + h > dst->h) h = dst->h - y;
    
    if (w <= 0 || h <= 0) return 0;
    
    // Fill with color (assuming 8-bit surface)
    uint8_t *pixels = (uint8_t *)dst->pixels;
    for (int row = y; row < y + h; row++) {
        memset(pixels + row * dst->pitch + x, color & 0xFF, w);
    }
    
    return 0;
}

int SDL_InitSubSystem(Uint32 flags) {
    // All subsystems are already initialized in SDL_Init
    return 0;
}

void SDL_QuitSubSystem(Uint32 flags) {
    // Stub - nothing to do
}

void SDL_ClearError(void) {
    error_string[0] = '\0';
}

// Joystick stubs
int SDL_JoystickNumBalls(SDL_Joystick *joystick) {
    return 0;
}


int SDL_JoystickEventState(int state) {
    return 0;
}

// Window Manager stub
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode) {
    // Always report as grabbing (we're embedded, there's only one app)
    if (mode == SDL_GRAB_QUERY) {
        return SDL_GRAB_ON;
    }
    return mode;
}

// PlayMusic - Load and play MIDI file using OPL emulator
#include "../i_music.h"

int PlayMusic(const char *filename) {
    if (!filename || !filename[0]) {
        return 0;
    }

    // Play the MIDI file with looping enabled
    if (I_Music_PlayMIDI(filename, true)) {
        return 1;  // Success
    }
    
    return 0;
}

