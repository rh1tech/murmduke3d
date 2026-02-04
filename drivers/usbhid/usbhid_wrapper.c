/*
 * USB HID Wrapper for Duke3D
 * Maps USB HID keyboard/mouse events to Duke3D events
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "usbhid.h"
#include "../usbhid_wrapper.h"
#include <stdint.h>
#include <stdio.h>

// Mouse sensitivity multiplier (increase for faster response)
#define MOUSE_SENSITIVITY_MULT 2

// Maximum delta per tick to prevent abrupt jumps
#define MOUSE_MAX_DELTA 40

// Track previous button state to detect changes
static uint8_t prev_usb_buttons = 0;

// Clamp value to range
static inline int16_t clamp_delta(int16_t val, int16_t max_val) {
    if (val > max_val) return max_val;
    if (val < -max_val) return -max_val;
    return val;
}

//--------------------------------------------------------------------
// Duke3D Scancodes (from keyboard.h)
//--------------------------------------------------------------------

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

//--------------------------------------------------------------------
// HID Keycode to Duke3D Scancode Mapping
//--------------------------------------------------------------------

static unsigned char hid_to_duke3d_scancode(uint8_t hid_keycode) {
    // Modifier pseudo-keycodes (from hid_app.c)
    if (hid_keycode == 0xE0) return sc_LeftControl;  // Ctrl
    if (hid_keycode == 0xE1) return sc_LeftShift;    // Shift
    if (hid_keycode == 0xE2) return sc_LeftAlt;      // Alt

    // Letters A-Z (HID 0x04-0x1D)
    if (hid_keycode >= 0x04 && hid_keycode <= 0x1D) {
        // Map HID A-Z to Duke3D scancodes
        static const unsigned char letter_map[] = {
            sc_A, sc_B, sc_C, sc_D, sc_E, sc_F, sc_G, sc_H, sc_I, sc_J,
            sc_K, sc_L, sc_M, sc_N, sc_O, sc_P, sc_Q, sc_R, sc_S, sc_T,
            sc_U, sc_V, sc_W, sc_X, sc_Y, sc_Z
        };
        return letter_map[hid_keycode - 0x04];
    }

    // Numbers 1-9, 0 (HID 0x1E-0x27)
    if (hid_keycode >= 0x1E && hid_keycode <= 0x26) {
        return sc_1 + (hid_keycode - 0x1E);
    }
    if (hid_keycode == 0x27) return sc_0;

    // Function keys F1-F12 (HID 0x3A-0x45)
    if (hid_keycode >= 0x3A && hid_keycode <= 0x43) {
        return sc_F1 + (hid_keycode - 0x3A);
    }
    if (hid_keycode == 0x44) return sc_F11;
    if (hid_keycode == 0x45) return sc_F12;

    // Special keys
    switch (hid_keycode) {
        case 0x28: return sc_Return;       // Enter
        case 0x29: return sc_Escape;       // Escape
        case 0x2A: return sc_BackSpace;    // Backspace
        case 0x2B: return sc_Tab;          // Tab
        case 0x2C: return sc_Space;        // Space
        case 0x2D: return sc_Minus;        // Minus
        case 0x2E: return sc_Equals;       // Equals
        case 0x2F: return sc_LeftBracket;  // Left bracket
        case 0x30: return sc_RightBracket; // Right bracket
        case 0x31: return sc_BackSlash;    // Backslash
        case 0x33: return sc_SemiColon;    // Semicolon
        case 0x34: return sc_Quote;        // Quote
        case 0x35: return sc_BackQuote;    // Grave/tilde
        case 0x36: return sc_Comma;        // Comma
        case 0x37: return sc_Period;       // Period
        case 0x38: return sc_Slash;        // Forward slash
        case 0x39: return sc_CapsLock;     // Caps Lock

        // Arrow keys
        case 0x4F: return sc_RightArrow;
        case 0x50: return sc_LeftArrow;
        case 0x51: return sc_DownArrow;
        case 0x52: return sc_UpArrow;

        // Navigation keys
        case 0x49: return sc_Insert;
        case 0x4A: return sc_Home;
        case 0x4B: return sc_PgUp;
        case 0x4C: return sc_Delete;
        case 0x4D: return sc_End;
        case 0x4E: return sc_PgDn;

        // Pause
        case 0x48: return sc_Pause;

        // Keypad
        case 0x53: return sc_NumLock;
        case 0x54: return sc_kpad_Slash;
        case 0x55: return sc_Multiply;
        case 0x56: return sc_kpad_Minus;
        case 0x57: return sc_kpad_Plus;
        case 0x58: return sc_kpad_Enter;
        case 0x59: return sc_kpad_1;
        case 0x5A: return sc_kpad_2;
        case 0x5B: return sc_kpad_3;
        case 0x5C: return sc_kpad_4;
        case 0x5D: return sc_kpad_5;
        case 0x5E: return sc_kpad_6;
        case 0x5F: return sc_kpad_7;
        case 0x60: return sc_kpad_8;
        case 0x61: return sc_kpad_9;
        case 0x62: return sc_kpad_0;
        case 0x63: return sc_kpad_Period;

        default: return 0; // Unknown key
    }
}

//--------------------------------------------------------------------
// Initialization
//--------------------------------------------------------------------

static int usb_hid_initialized = 0;

void usbhid_wrapper_init(void) {
#ifdef USB_HID_ENABLED
    usbhid_init();
    usb_hid_initialized = 1;
    prev_usb_buttons = 0;
#endif
}

//--------------------------------------------------------------------
// Mouse State Access
//--------------------------------------------------------------------

void usbhid_wrapper_get_mouse_state(int16_t *dx, int16_t *dy, int8_t *wheel, uint8_t *buttons) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) {
        *dx = 0;
        *dy = 0;
        *wheel = 0;
        *buttons = 0;
        return;
    }

    // Process USB events
    usbhid_task();

    // Get mouse state
    usbhid_mouse_state_t mouse;
    usbhid_get_mouse_state(&mouse);

    // Clamp deltas
    *dx = clamp_delta(mouse.dx, MOUSE_MAX_DELTA) * MOUSE_SENSITIVITY_MULT;
    *dy = clamp_delta(mouse.dy, MOUSE_MAX_DELTA) * MOUSE_SENSITIVITY_MULT;
    *wheel = mouse.wheel;
    *buttons = mouse.buttons & 0x07;
#else
    *dx = 0;
    *dy = 0;
    *wheel = 0;
    *buttons = 0;
#endif
}

//--------------------------------------------------------------------
// Connection Status
//--------------------------------------------------------------------

int usbhid_wrapper_keyboard_connected(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;
    return usbhid_keyboard_connected();
#else
    return 0;
#endif
}

int usbhid_wrapper_mouse_connected(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;
    return usbhid_mouse_connected();
#else
    return 0;
#endif
}

//--------------------------------------------------------------------
// Keyboard Event Access
//--------------------------------------------------------------------

int usbhid_wrapper_get_key(int *pressed, unsigned char *key) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return 0;

    // Process USB events
    usbhid_task();

    // Get next key action from the queue
    uint8_t hid_keycode;
    int down;
    if (usbhid_get_key_action(&hid_keycode, &down)) {
        unsigned char duke_key = hid_to_duke3d_scancode(hid_keycode);
        if (duke_key != 0) {
            *pressed = down;
            *key = duke_key;
            return 1;
        }
    }
#else
    (void)pressed;
    (void)key;
#endif
    return 0;
}

//--------------------------------------------------------------------
// USB Task (call periodically)
//--------------------------------------------------------------------

void usbhid_wrapper_task(void) {
#ifdef USB_HID_ENABLED
    if (!usb_hid_initialized) return;
    usbhid_task();
#endif
}
