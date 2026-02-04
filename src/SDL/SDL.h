/*
 * SDL compatibility layer for RP2350
 * This provides a minimal SDL-like interface for Duke3D on RP2350
 */
#ifndef SDL_h_
#define SDL_h_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL_stdinc.h"
#include "SDL_endian.h"
#include "SDL_scancode.h"
#include "SDL_input.h"
#include "SDL_video.h"
#include "SDL_event.h"
#include "SDL_audio.h"

#include "pico/stdlib.h"

typedef int SDLMod;
typedef SDL_Keycode SDLKey;  /* Compatibility alias */

#define SDLK_FIRST 0
#define SDLK_LAST 1024

/* Keypad aliases - use SDL2 KP values to avoid conflicts */
#define SDLK_KP0 SDLK_KP_0
#define SDLK_KP1 SDLK_KP_1
#define SDLK_KP2 SDLK_KP_2
#define SDLK_KP3 SDLK_KP_3
#define SDLK_KP4 SDLK_KP_4
#define SDLK_KP5 SDLK_KP_5
#define SDLK_KP6 SDLK_KP_6
#define SDLK_KP7 SDLK_KP_7
#define SDLK_KP8 SDLK_KP_8
#define SDLK_KP9 SDLK_KP_9
#define SDLK_PRINT 0

#define AUDIO_S16SYS 16
#define AUDIO_S8 8

#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDLK_NUMLOCK 999
#define SDLK_SCROLLOCK 1000

#define SDL_strlcpy strlcpy

#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_JOYSTICK       0x00000200u
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_SENSOR         0x00008000u
#define SDL_INIT_NOPARACHUTE    0x00100000u
#define SDL_INIT_EVERYTHING ( \
                SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | \
                SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR \
            )

/* SDL Query states */
#define SDL_QUERY   -1
#define SDL_IGNORE   0
#define SDL_DISABLE  0
#define SDL_ENABLE   1

/* SDL version info */
typedef struct SDL_version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} SDL_version;

#define SDL_VERSION(x) do { \
    (x)->major = 1; \
    (x)->minor = 2; \
    (x)->patch = 15; \
} while (0)

/* Core SDL functions */
int SDL_Init(uint32_t flags);
void SDL_Quit(void);
void SDL_Delay(uint32_t ms);
const SDL_version *SDL_Linked_Version(void);
const char *SDL_GetError(void);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, uint32_t color);

// RP2350-specific functions
void SDL_LockDisplay(void);
void SDL_UnlockDisplay(void);

// Memory functions mapped to standard C
#define SDL_malloc malloc
#define SDL_calloc calloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_memmove memmove
#define SDL_memcmp memcmp
#define SDL_strlen strlen
#define SDL_strlcat strlcat
#define SDL_strdup strdup
#define SDL_strchr strchr
#define SDL_strrchr strrchr
#define SDL_strstr strstr
#define SDL_strcmp strcmp
#define SDL_strncmp strncmp
#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
#define SDL_sscanf sscanf
#define SDL_vsscanf vsscanf
#define SDL_snprintf snprintf
#define SDL_vsnprintf vsnprintf
#define SDL_atoi atoi
#define SDL_itoa itoa
#define SDL_isdigit isdigit
#define SDL_isspace isspace

#endif /* SDL_h_ */
