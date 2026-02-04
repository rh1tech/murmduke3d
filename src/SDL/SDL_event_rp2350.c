/*
 * SDL Event implementation for RP2350
 * Uses PS/2 and USB HID keyboard/mouse drivers
 */
#include "SDL.h"
#include "SDL_event.h"
#include "ps2kbd_wrapper.h"
#include "ps2.h"
#include "usbhid_wrapper.h"
#include "pico/stdlib.h"

#define MAX_EVENTS 32
static SDL_Event event_queue[MAX_EVENTS];
static int event_head = 0;
static int event_tail = 0;

// Mouse button tracking
static uint8_t last_mouse_buttons = 0;

// Duke3D scancodes (from keyboard.h)
#define  sc_Escape       0x01
#define  sc_1            0x02
#define  sc_2            0x03
#define  sc_3            0x04
#define  sc_4            0x05
#define  sc_5            0x06
#define  sc_6            0x07
#define  sc_7            0x08
#define  sc_8            0x09
#define  sc_9            0x0a
#define  sc_0            0x0b
#define  sc_Minus        0x0c
#define  sc_Equals       0x0d
#define  sc_BackSpace    0x0e
#define  sc_Tab          0x0f
#define  sc_Q            0x10
#define  sc_W            0x11
#define  sc_E            0x12
#define  sc_R            0x13
#define  sc_T            0x14
#define  sc_Y            0x15
#define  sc_U            0x16
#define  sc_I            0x17
#define  sc_O            0x18
#define  sc_P            0x19
#define  sc_LeftBracket  0x1a
#define  sc_RightBracket 0x1b
#define  sc_Return       0x1c
#define  sc_LeftControl  0x1d
#define  sc_A            0x1e
#define  sc_S            0x1f
#define  sc_D            0x20
#define  sc_F            0x21
#define  sc_G            0x22
#define  sc_H            0x23
#define  sc_J            0x24
#define  sc_K            0x25
#define  sc_L            0x26
#define  sc_SemiColon    0x27
#define  sc_Quote        0x28
#define  sc_BackQuote    0x29
#define  sc_LeftShift    0x2a
#define  sc_BackSlash    0x2b
#define  sc_Z            0x2c
#define  sc_X            0x2d
#define  sc_C            0x2e
#define  sc_V            0x2f
#define  sc_B            0x30
#define  sc_N            0x31
#define  sc_M            0x32
#define  sc_Comma        0x33
#define  sc_Period       0x34
#define  sc_Slash        0x35
#define  sc_RightShift   0x36
#define  sc_Multiply     0x37
#define  sc_LeftAlt      0x38
#define  sc_Space        0x39
#define  sc_CapsLock     0x3a
#define  sc_F1           0x3b
#define  sc_F2           0x3c
#define  sc_F3           0x3d
#define  sc_F4           0x3e
#define  sc_F5           0x3f
#define  sc_F6           0x40
#define  sc_F7           0x41
#define  sc_F8           0x42
#define  sc_F9           0x43
#define  sc_F10          0x44
#define  sc_NumLock      0x45
#define  sc_ScrollLock   0x46
// Keypad (non-extended)
#define  sc_kpad_7       0x47
#define  sc_kpad_8       0x48
#define  sc_kpad_9       0x49
#define  sc_kpad_Minus   0x4a
#define  sc_kpad_4       0x4b
#define  sc_kpad_5       0x4c
#define  sc_kpad_6       0x4d
#define  sc_kpad_Plus    0x4e
#define  sc_kpad_1       0x4f
#define  sc_kpad_2       0x50
#define  sc_kpad_3       0x51
#define  sc_kpad_0       0x52
#define  sc_kpad_Period  0x53
#define  sc_F11          0x57
#define  sc_F12          0x58
#define  sc_Pause        0x59
// Extended scancodes (from keyboard.h)
#define  sc_UpArrow      0x5a
#define  sc_Insert       0x5e
#define  sc_Delete       0x5f
#define  sc_Home         0x61
#define  sc_End          0x62
#define  sc_PgUp         0x63
#define  sc_PgDn         0x64
#define  sc_RightAlt     0x65
#define  sc_RightControl 0x66
#define  sc_kpad_Slash   0x67
#define  sc_kpad_Enter   0x68
#define  sc_DownArrow    0x6a
#define  sc_LeftArrow    0x6b
#define  sc_RightArrow   0x6c

// Convert Duke3D scancode to SDL keycode
static SDLKey duke3d_scancode_to_sdl_key(unsigned char key) {
    switch (key) {
        case sc_Escape:      return SDLK_ESCAPE;
        case sc_1:           return SDLK_1;
        case sc_2:           return SDLK_2;
        case sc_3:           return SDLK_3;
        case sc_4:           return SDLK_4;
        case sc_5:           return SDLK_5;
        case sc_6:           return SDLK_6;
        case sc_7:           return SDLK_7;
        case sc_8:           return SDLK_8;
        case sc_9:           return SDLK_9;
        case sc_0:           return SDLK_0;
        case sc_Minus:       return SDLK_MINUS;
        case sc_Equals:      return SDLK_EQUALS;
        case sc_BackSpace:   return SDLK_BACKSPACE;
        case sc_Tab:         return SDLK_TAB;
        case sc_Q:           return SDLK_q;
        case sc_W:           return SDLK_w;
        case sc_E:           return SDLK_e;
        case sc_R:           return SDLK_r;
        case sc_T:           return SDLK_t;
        case sc_Y:           return SDLK_y;
        case sc_U:           return SDLK_u;
        case sc_I:           return SDLK_i;
        case sc_O:           return SDLK_o;
        case sc_P:           return SDLK_p;
        case sc_LeftBracket: return SDLK_LEFTBRACKET;
        case sc_RightBracket:return SDLK_RIGHTBRACKET;
        case sc_Return:      return SDLK_RETURN;
        case sc_LeftControl: return SDLK_LCTRL;
        case sc_A:           return SDLK_a;
        case sc_S:           return SDLK_s;
        case sc_D:           return SDLK_d;
        case sc_F:           return SDLK_f;
        case sc_G:           return SDLK_g;
        case sc_H:           return SDLK_h;
        case sc_J:           return SDLK_j;
        case sc_K:           return SDLK_k;
        case sc_L:           return SDLK_l;
        case sc_SemiColon:   return SDLK_SEMICOLON;
        case sc_Quote:       return SDLK_QUOTE;
        case sc_BackQuote:   return SDLK_BACKQUOTE;
        case sc_LeftShift:   return SDLK_LSHIFT;
        case sc_BackSlash:   return SDLK_BACKSLASH;
        case sc_Z:           return SDLK_z;
        case sc_X:           return SDLK_x;
        case sc_C:           return SDLK_c;
        case sc_V:           return SDLK_v;
        case sc_B:           return SDLK_b;
        case sc_N:           return SDLK_n;
        case sc_M:           return SDLK_m;
        case sc_Comma:       return SDLK_COMMA;
        case sc_Period:      return SDLK_PERIOD;
        case sc_Slash:       return SDLK_SLASH;
        case sc_RightShift:  return SDLK_RSHIFT;
        case sc_Multiply:    return SDLK_KP_MULTIPLY;
        case sc_LeftAlt:     return SDLK_LALT;
        case sc_Space:       return SDLK_SPACE;
        case sc_CapsLock:    return SDLK_CAPSLOCK;
        case sc_F1:          return SDLK_F1;
        case sc_F2:          return SDLK_F2;
        case sc_F3:          return SDLK_F3;
        case sc_F4:          return SDLK_F4;
        case sc_F5:          return SDLK_F5;
        case sc_F6:          return SDLK_F6;
        case sc_F7:          return SDLK_F7;
        case sc_F8:          return SDLK_F8;
        case sc_F9:          return SDLK_F9;
        case sc_F10:         return SDLK_F10;
        case sc_NumLock:     return SDLK_NUMLOCK;
        case sc_ScrollLock:  return SDLK_SCROLLOCK;
        case sc_F11:         return SDLK_F11;
        case sc_F12:         return SDLK_F12;
        case sc_Pause:       return SDLK_PAUSE;
        // Keypad
        case sc_kpad_7:      return SDLK_KP7;
        case sc_kpad_8:      return SDLK_KP8;
        case sc_kpad_9:      return SDLK_KP9;
        case sc_kpad_Minus:  return SDLK_KP_MINUS;
        case sc_kpad_4:      return SDLK_KP4;
        case sc_kpad_5:      return SDLK_KP5;
        case sc_kpad_6:      return SDLK_KP6;
        case sc_kpad_Plus:   return SDLK_KP_PLUS;
        case sc_kpad_1:      return SDLK_KP1;
        case sc_kpad_2:      return SDLK_KP2;
        case sc_kpad_3:      return SDLK_KP3;
        case sc_kpad_0:      return SDLK_KP0;
        case sc_kpad_Period: return SDLK_KP_PERIOD;
        case sc_kpad_Slash:  return SDLK_KP_DIVIDE;
        case sc_kpad_Enter:  return SDLK_KP_ENTER;
        // Extended keys
        case sc_UpArrow:     return SDLK_UP;
        case sc_DownArrow:   return SDLK_DOWN;
        case sc_LeftArrow:   return SDLK_LEFT;
        case sc_RightArrow:  return SDLK_RIGHT;
        case sc_Home:        return SDLK_HOME;
        case sc_End:         return SDLK_END;
        case sc_PgUp:        return SDLK_PAGEUP;
        case sc_PgDn:        return SDLK_PAGEDOWN;
        case sc_Insert:      return SDLK_INSERT;
        case sc_Delete:      return SDLK_DELETE;
        case sc_RightAlt:    return SDLK_RALT;
        case sc_RightControl:return SDLK_RCTRL;
        default:             return SDLK_UNKNOWN;
    }
}

// USB HID mouse button tracking
static uint8_t last_usb_mouse_buttons = 0;

void SDL_PumpEvents(void) {
    // Poll PS/2 keyboard and add events to queue
    ps2kbd_tick();

    int pressed;
    unsigned char key;
    while (ps2kbd_get_key(&pressed, &key)) {
        int next_head = (event_head + 1) % MAX_EVENTS;
        if (next_head != event_tail) {
            SDL_Event *ev = &event_queue[event_head];
            ev->type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
            ev->key.keysym.sym = duke3d_scancode_to_sdl_key(key);
            ev->key.keysym.scancode = key;
            ev->key.keysym.mod = KMOD_NONE;
            ev->key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
            event_head = next_head;
        }
    }

    // Poll USB HID keyboard (if enabled)
    while (usbhid_wrapper_get_key(&pressed, &key)) {
        int next_head = (event_head + 1) % MAX_EVENTS;
        if (next_head != event_tail) {
            SDL_Event *ev = &event_queue[event_head];
            ev->type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
            ev->key.keysym.sym = duke3d_scancode_to_sdl_key(key);
            ev->key.keysym.scancode = key;
            ev->key.keysym.mod = KMOD_NONE;
            ev->key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
            event_head = next_head;
        }
    }

    // Poll PS/2 mouse
    ps2_mouse_poll();

    int16_t dx, dy;
    int8_t wheel;
    uint8_t buttons;

    // Get PS/2 mouse state
    ps2_mouse_get_state(&dx, &dy, &wheel, &buttons);

    // Remap PS/2 buttons to SDL bit order for motion.state
    // PS/2 standard: bit 0=left, bit 1=right, bit 2=middle
    // SDL standard:  bit 0=left, bit 1=middle, bit 2=right
    uint8_t ps2_buttons_remapped = ((buttons & 0x01)) |        // left (bit 0) -> SDL left (bit 0)
                                   ((buttons & 0x04) >> 1) |   // middle (bit 2) -> SDL middle (bit 1)
                                   ((buttons & 0x02) << 1);    // right (bit 1) -> SDL right (bit 2)

    // PS/2 Motion event
    if (dx != 0 || dy != 0) {
        int next_head = (event_head + 1) % MAX_EVENTS;
        if (next_head != event_tail) {
            SDL_Event *ev = &event_queue[event_head];
            ev->type = SDL_MOUSEMOTION;
            ev->motion.xrel = dx;
            ev->motion.yrel = dy;
            ev->motion.state = ps2_buttons_remapped;
            event_head = next_head;
        }
    }

    // PS/2 Button events
    // PS/2 standard: bit 0=left, bit 1=right, bit 2=middle
    // SDL button order: 1=left, 2=middle, 3=right
    static const uint8_t ps2_to_sdl_button[3] = {1, 3, 2};  // Map PS/2 bits to SDL buttons

    if (buttons != last_mouse_buttons) {
        for (int i = 0; i < 3; i++) {
            uint8_t mask = 1 << i;
            if ((buttons & mask) != (last_mouse_buttons & mask)) {
                int next_head = (event_head + 1) % MAX_EVENTS;
                if (next_head != event_tail) {
                    SDL_Event *ev = &event_queue[event_head];
                    ev->type = (buttons & mask) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    ev->button.button = ps2_to_sdl_button[i];
                    ev->button.state = (buttons & mask) ? SDL_PRESSED : SDL_RELEASED;
                    event_head = next_head;
                }
            }
        }
        last_mouse_buttons = buttons;
    }

    // Poll USB HID mouse (if enabled)
    int16_t usb_dx, usb_dy;
    int8_t usb_wheel;
    uint8_t usb_buttons;
    usbhid_wrapper_get_mouse_state(&usb_dx, &usb_dy, &usb_wheel, &usb_buttons);

    // Remap USB HID buttons to SDL bit order for motion.state
    // USB HID:      bit 0=left, bit 1=right, bit 2=middle
    // SDL standard: bit 0=left, bit 1=middle, bit 2=right
    uint8_t usb_buttons_remapped = ((usb_buttons & 0x01)) |        // USB left (bit 0) -> SDL left (bit 0)
                                   ((usb_buttons & 0x04) >> 1) |   // USB middle (bit 2) -> SDL middle (bit 1)
                                   ((usb_buttons & 0x02) << 1);    // USB right (bit 1) -> SDL right (bit 2)

    // USB HID Motion event
    if (usb_dx != 0 || usb_dy != 0) {
        int next_head = (event_head + 1) % MAX_EVENTS;
        if (next_head != event_tail) {
            SDL_Event *ev = &event_queue[event_head];
            ev->type = SDL_MOUSEMOTION;
            ev->motion.xrel = usb_dx;
            ev->motion.yrel = usb_dy;
            ev->motion.state = usb_buttons_remapped;
            event_head = next_head;
        }
    }

    // USB HID Button events
    // USB HID bit order: 0=left, 1=right, 2=middle (standard USB HID)
    // PS/2 bit order: 0=middle, 1=right, 2=left
    // SDL button order: 1=left, 2=middle, 3=right
    static const uint8_t usb_to_sdl_button[3] = {1, 3, 2};  // Map USB bits to SDL buttons

    if (usb_buttons != last_usb_mouse_buttons) {
        for (int i = 0; i < 3; i++) {
            uint8_t mask = 1 << i;
            if ((usb_buttons & mask) != (last_usb_mouse_buttons & mask)) {
                int next_head = (event_head + 1) % MAX_EVENTS;
                if (next_head != event_tail) {
                    SDL_Event *ev = &event_queue[event_head];
                    ev->type = (usb_buttons & mask) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                    ev->button.button = usb_to_sdl_button[i];
                    ev->button.state = (usb_buttons & mask) ? SDL_PRESSED : SDL_RELEASED;
                    event_head = next_head;
                }
            }
        }
        last_usb_mouse_buttons = usb_buttons;
    }
}

int SDL_PollEvent(SDL_Event *event) {
    SDL_PumpEvents();
    
    if (event_tail != event_head) {
        *event = event_queue[event_tail];
        event_tail = (event_tail + 1) % MAX_EVENTS;
        return 1;
    }
    return 0;
}

int SDL_WaitEvent(SDL_Event *event) {
    while (1) {
        if (SDL_PollEvent(event)) {
            return 1;
        }
        sleep_ms(10);
    }
}

Uint8 *SDL_GetKeyState(int *numkeys) {
    static Uint8 keystate[SDLK_LAST];
    if (numkeys) *numkeys = SDLK_LAST;
    return keystate;
}

char *SDL_GetKeyName(SDLKey key) {
    static char name[32];
    if (key >= SDLK_a && key <= SDLK_z) {
        name[0] = 'A' + (key - SDLK_a);
        name[1] = '\0';
    } else {
        snprintf(name, sizeof(name), "Key%d", key);
    }
    return name;
}

SDL_Keymod SDL_GetModState(void) {
    return KMOD_NONE;
}

void SDL_SetModState(SDL_Keymod modstate) {
    // Not implemented
}

int SDL_EnableKeyRepeat(int delay, int interval) {
    return 0;
}

int SDL_EnableUNICODE(int enable) {
    return 0;
}

/* Joystick stubs */
int SDL_NumJoysticks(void) {
    return 0;
}

SDL_Joystick *SDL_JoystickOpen(int device_index) {
    return NULL;
}

void SDL_JoystickClose(SDL_Joystick *joystick) {
}

const char *SDL_JoystickName(SDL_Joystick *joystick) {
    return "";
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick) {
    return 0;
}

int SDL_JoystickNumButtons(SDL_Joystick *joystick) {
    return 0;
}

int SDL_JoystickNumHats(SDL_Joystick *joystick) {
    return 0;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis) {
    return 0;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button) {
    return 0;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat) {
    return 0;
}

void SDL_JoystickUpdate(void) {
}
