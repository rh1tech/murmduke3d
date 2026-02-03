/*
 * SDL Video header for RP2350
 */
#ifndef SDL_VIDEO_H
#define SDL_VIDEO_H

#include "SDL_stdinc.h"

#define SDL_SaveBMP(surface, file) {}
#define SDL_LoadBMP_RW(src, freesrc) {}

typedef struct {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 unused;
} SDL_Color;

typedef struct {
    int ncolors;
    SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint32 colorkey;
    Uint8 alpha;
} SDL_PixelFormat;

typedef struct {
    Sint16 x, y;
    Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    Uint16 pitch;
    void *pixels;
    SDL_Rect clip_rect;
    int refcount;
} SDL_Surface;

/* Surface flags */
#define SDL_SWSURFACE   0x00000000
#define SDL_HWSURFACE   0x00000001
#define SDL_ASYNCBLIT   0x00000004
#define SDL_ANYFORMAT   0x10000000
#define SDL_HWPALETTE   0x20000000
#define SDL_DOUBLEBUF   0x40000000
#define SDL_FULLSCREEN  0x80000000
#define SDL_OPENGL      0x00000002
#define SDL_OPENGLBLIT  0x0000000A
#define SDL_RESIZABLE   0x00000010
#define SDL_NOFRAME     0x00000020
#define SDL_HWACCEL     0x00000100
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_RLEACCELOK  0x00002000
#define SDL_RLEACCEL    0x00004000
#define SDL_SRCALPHA    0x00010000
#define SDL_PREALLOC    0x01000000

#define SDL_MUSTLOCK(S) (((S)->flags & SDL_RLEACCEL) != 0)

typedef struct {
    Uint32 hw_available:1;
    Uint32 wm_available:1;
    Uint32 blit_hw:1;
    Uint32 blit_hw_CC:1;
    Uint32 blit_hw_A:1;
    Uint32 blit_sw:1;
    Uint32 blit_sw_CC:1;
    Uint32 blit_sw_A:1;
    Uint32 blit_fill;
    Uint32 video_mem;
    SDL_PixelFormat *vfmt;
} SDL_VideoInfo;

/* Video functions */
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Sint32 w, Sint32 h);
SDL_VideoInfo *SDL_GetVideoInfo(void);
char *SDL_VideoDriverName(char *namebuf, int maxlen);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
void SDL_WM_SetCaption(const char *title, const char *icon);
Uint32 SDL_GetTicks(void);
Uint32 SDL_WasInit(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors, int firstcolor, int ncolors);
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors);
int SDL_Flip(SDL_Surface *screen);
void SDL_WarpMouse(Uint16 x, Uint16 y);
Uint8 SDL_GetMouseState(int *x, int *y);
int SDL_ShowCursor(int toggle);

/* Reset video state for returning to welcome screen */
void SDL_ResetVideoState(void);

#define SDL_LOGPAL 0x01
#define SDL_PHYSPAL 0x02

#endif /* SDL_VIDEO_H */
