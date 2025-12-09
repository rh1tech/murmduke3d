/*
 * Duke Nukem 3D - RP2350 Port
 * Main entry point
 */
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include <stdio.h>

#include "psram_init.h"
#include "psram_data.h"
#include "psram_sections.h"
#include "board_config.h"

// Forward declaration of Duke3D main
extern int main_duke3d(int argc, char *argv[]);

int main() {
    // Set system clock to 252 MHz for HDMI
    // 640x480@60Hz pixel clock is ~25.2MHz, PIO DVI needs 10x = ~252MHz
    set_sys_clock_khz(252000, true);

    stdio_init_all();
    
    // Wait for USB connection for debugging
    for (int i = 0; i < 3; i++) {
        printf("murmduke3d: Starting in %d...\n", 3 - i);
        sleep_ms(1000);
    }
    
    printf("System Clock: %lu Hz\n", clock_get_hz(clk_sys));
    
    // Initialize PSRAM first (required for game data)
    printf("Initializing PSRAM...\n");
    uint psram_pin = get_psram_pin();
    psram_init(psram_pin);
    printf("PSRAM initialized on GPIO %u\n", psram_pin);
    
    // Initialize PSRAM linker sections (copy .psram_data, zero .psram_bss)
    printf("Initializing PSRAM sections...\n");
    psram_sections_init();
    printf("PSRAM sections: data=%zu bytes, bss=%zu bytes\n", 
           psram_data_size(), psram_bss_size());
    
    // Allocate game data arrays in PSRAM
    psram_data_init();
    
    printf("Starting Duke Nukem 3D...\n");

    // Launch Duke3D with no music (/nm)
    char *argv[] = {"duke3d", "/nm", NULL};
    main_duke3d(2, argv);

    // Should never reach here
    while (1) {
        tight_loop_contents();
    }

    return 0;
}
