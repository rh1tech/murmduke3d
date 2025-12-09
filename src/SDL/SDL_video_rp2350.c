/*
 * SDL Video implementation for RP2350
 * Uses HDMI driver for display output
 * Copy-based double buffering with DMA acceleration
 */
#include "SDL.h"
#include "SDL_video.h"
#include "HDMI.h"
#include "psram_allocator.h"
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FRAME_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT)

static SDL_Surface *primary_surface = NULL;
static SDL_VideoInfo video_info;
static SDL_PixelFormat primary_format;
static SDL_Palette primary_palette;
static SDL_Color palette_colors[256];

/* Double buffering: game always renders to back_buffer, HDMI reads from front_buffer */
static uint8_t *front_buffer = NULL;  /* HDMI displays this */
static uint8_t *back_buffer = NULL;   /* Game renders to this */

/* DMA channel for fast buffer copy */
static int dma_chan = -1;

int SDL_LockSurface(SDL_Surface *surface) {
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface) {
}

void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h) {
    SDL_Flip(screen);
}

SDL_VideoInfo *SDL_GetVideoInfo(void) {
    return &video_info;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen) {
    strncpy(namebuf, "RP2350 HDMI Driver", maxlen);
    return namebuf;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags) {
    static SDL_Rect mode = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    static SDL_Rect *modes[] = {&mode, NULL};
    return modes;
}

void SDL_WM_SetCaption(const char *title, const char *icon) {
    // No window caption on embedded system
}

Uint32 SDL_GetTicks(void) {
    return to_ms_since_boot(get_absolute_time());
}

Uint32 SDL_WasInit(Uint32 flags) {
    return (primary_surface != NULL) ? flags : 0;
}

int SDL_InitSubSystem(Uint32 flags) {
    if (flags & SDL_INIT_VIDEO) {
        // Video initialization done in SDL_SetVideoMode
    }
    if (flags & SDL_INIT_AUDIO) {
        // Audio disabled for now
    }
    return 0;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, 
                                   Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
    SDL_Surface *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!surface) return NULL;
    
    SDL_PixelFormat *pf = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
    if (!pf) {
        free(surface);
        return NULL;
    }
    
    pf->BitsPerPixel = depth;
    pf->BytesPerPixel = (depth + 7) / 8;
    pf->Rmask = Rmask;
    pf->Gmask = Gmask;
    pf->Bmask = Bmask;
    pf->Amask = Amask;
    
    if (depth == 8) {
        pf->palette = &primary_palette;
    }
    
    surface->flags = flags;
    surface->format = pf;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * pf->BytesPerPixel;
    surface->pixels = psram_malloc(width * height * pf->BytesPerPixel);
    surface->clip_rect.x = 0;
    surface->clip_rect.y = 0;
    surface->clip_rect.w = width;
    surface->clip_rect.h = height;
    surface->refcount = 1;
    
    if (surface->pixels) {
        memset(surface->pixels, 0, width * height * pf->BytesPerPixel);
    }
    
    return surface;
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags) {
    if (primary_surface) {
        // Already initialized
        return primary_surface;
    }
    
    // Initialize HDMI
    graphics_init(g_out_HDMI);
    graphics_set_res(width, height);
    
    // Allocate front buffer (HDMI reads from this)
    front_buffer = (uint8_t *)psram_malloc(width * height);
    if (!front_buffer) {
        printf("SDL_SetVideoMode: Failed to allocate front buffer\n");
        return NULL;
    }
    memset(front_buffer, 0, width * height);
    
    // Allocate back buffer (game renders to this)
    back_buffer = (uint8_t *)psram_malloc(width * height);
    if (!back_buffer) {
        printf("SDL_SetVideoMode: Failed to allocate back buffer\n");
        return NULL;
    }
    memset(back_buffer, 0, width * height);
    
    // HDMI displays from front buffer
    graphics_set_buffer(front_buffer);
    
    // DMA will be set up later if needed - for now use memcpy
    dma_chan = -1;  // Disable DMA for now - use memcpy
    
    // Initialize palette
    primary_palette.ncolors = 256;
    primary_palette.colors = palette_colors;
    memset(palette_colors, 0, sizeof(palette_colors));
    
    // Create the primary surface
    primary_surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
    if (!primary_surface) {
        return NULL;
    }
    
    primary_format.BitsPerPixel = 8;
    primary_format.BytesPerPixel = 1;
    primary_format.palette = &primary_palette;
    
    primary_surface->flags = flags | SDL_DOUBLEBUF;
    primary_surface->format = &primary_format;
    primary_surface->w = width;
    primary_surface->h = height;
    primary_surface->pitch = width;
    primary_surface->pixels = back_buffer;  /* Game always renders to back buffer */
    primary_surface->clip_rect.x = 0;
    primary_surface->clip_rect.y = 0;
    primary_surface->clip_rect.w = width;
    primary_surface->clip_rect.h = height;
    primary_surface->refcount = 1;
    
    printf("SDL_SetVideoMode: %dx%d @ %dbpp (DMA double-buffered)\n", width, height, bpp);
    
    return primary_surface;
}

void SDL_FreeSurface(SDL_Surface *surface) {
    if (surface && surface != primary_surface) {
        if (surface->pixels && surface->pixels != back_buffer && surface->pixels != front_buffer) {
            psram_free(surface->pixels);
        }
        if (surface->format && surface->format != &primary_format) {
            free(surface->format);
        }
        free(surface);
    }
}

int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors, int firstcolor, int ncolors) {
    if (!surface || !surface->format || !surface->format->palette) {
        return 0;
    }
    
    for (int i = 0; i < ncolors && (firstcolor + i) < 256; i++) {
        palette_colors[firstcolor + i] = colors[i];
        uint32_t color888 = (colors[i].r << 16) | (colors[i].g << 8) | colors[i].b;
        graphics_set_palette(firstcolor + i, color888);
    }
    
    return 1;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors) {
    return SDL_SetPalette(surface, SDL_LOGPAL | SDL_PHYSPAL, colors, firstcolor, ncolors);
}

int SDL_Flip(SDL_Surface *screen) {
    if (!screen || !back_buffer || !front_buffer) return -1;
    
    /* Copy back buffer to front buffer using DMA for speed */
    if (dma_chan >= 0) {
        /* Use DMA for fast copy - 32-bit transfers, so divide byte count by 4 */
        dma_channel_set_read_addr(dma_chan, back_buffer, false);
        dma_channel_set_write_addr(dma_chan, front_buffer, false);
        dma_channel_set_trans_count(dma_chan, FRAME_SIZE / 4, true);  /* Start transfer */
        dma_channel_wait_for_finish_blocking(dma_chan);
    } else {
        /* Fallback to memcpy if DMA not available */
        memcpy(front_buffer, back_buffer, FRAME_SIZE);
    }
    
    return 0;
}

void SDL_WarpMouse(Uint16 x, Uint16 y) {
    // No mouse warping on embedded system
}

Uint8 SDL_GetMouseState(int *x, int *y) {
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

int SDL_ShowCursor(int toggle) {
    return 0;
}

void SDL_LockDisplay(void) {
    // Could add mutex here if needed
}

void SDL_UnlockDisplay(void) {
    // Could add mutex here if needed
}
