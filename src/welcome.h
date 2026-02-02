/*
 * Welcome Screen for Duke Nukem 3D - RP2350 Port
 * Displays GRP file selection menu before game starts
 */
#ifndef WELCOME_H
#define WELCOME_H

#include <stdint.h>
#include <stdbool.h>

// Screen dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// GRP file entry
typedef struct {
    const char *filename;
    const char *label;
} grp_entry_t;

// Initialize welcome screen (sets up graphics, palette)
void welcome_init(void);

// Show welcome screen and wait for GRP selection
// Returns selected GRP filename, or NULL if none found
const char *welcome_show(void);

// Signal that game should return to welcome screen
void welcome_request_return(void);

// Check if we should return to welcome screen
bool welcome_should_return(void);

// Clear return request
void welcome_clear_return(void);

#endif // WELCOME_H
