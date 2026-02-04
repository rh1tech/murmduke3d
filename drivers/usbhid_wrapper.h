/*
 * USB HID Wrapper for Duke3D - Header
 * Provides interface to USB keyboard/mouse for Duke3D
 *
 * When USB_HID_ENABLED is not defined, provides empty stub functions
 * so the code compiles but USB HID is disabled.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef USBHID_WRAPPER_H
#define USBHID_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef USB_HID_ENABLED

/**
 * Initialize USB HID wrapper
 * Call during system initialization after other USB init
 */
void usbhid_wrapper_init(void);

/**
 * Process USB HID events (call periodically)
 */
void usbhid_wrapper_task(void);

/**
 * Get mouse state (dx, dy, wheel, buttons)
 * Clears accumulated deltas after reading
 */
void usbhid_wrapper_get_mouse_state(int16_t *dx, int16_t *dy, int8_t *wheel, uint8_t *buttons);

/**
 * Check if USB keyboard is connected
 * @return Non-zero if a USB keyboard is connected
 */
int usbhid_wrapper_keyboard_connected(void);

/**
 * Check if USB mouse is connected
 * @return Non-zero if a USB mouse is connected
 */
int usbhid_wrapper_mouse_connected(void);

/**
 * Get the next key event from the USB keyboard queue
 * Works like ps2kbd_get_key() - returns Duke3D scancodes
 * @param pressed Output: 1 if key pressed, 0 if released
 * @param key Output: Duke3D scancode
 * @return Non-zero if a key event was available
 */
int usbhid_wrapper_get_key(int *pressed, unsigned char *key);

#else // !USB_HID_ENABLED

// Stub functions when USB HID is disabled
static inline void usbhid_wrapper_init(void) {}
static inline void usbhid_wrapper_task(void) {}
static inline void usbhid_wrapper_get_mouse_state(int16_t *dx, int16_t *dy, int8_t *wheel, uint8_t *buttons) {
    *dx = 0; *dy = 0; *wheel = 0; *buttons = 0;
}
static inline int usbhid_wrapper_keyboard_connected(void) { return 0; }
static inline int usbhid_wrapper_mouse_connected(void) { return 0; }
static inline int usbhid_wrapper_get_key(int *pressed, unsigned char *key) { (void)pressed; (void)key; return 0; }

#endif // USB_HID_ENABLED

#ifdef __cplusplus
}
#endif

#endif /* USBHID_WRAPPER_H */
