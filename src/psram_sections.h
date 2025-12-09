/*
 * PSRAM Section Macros for RP2350
 * 
 * Use these macros to place large arrays in PSRAM instead of SRAM.
 * Requires the custom memmap_psram.ld linker script.
 */

#ifndef PSRAM_SECTIONS_H
#define PSRAM_SECTIONS_H

#include <stdint.h>
#include <string.h>

/* Linker symbols for PSRAM sections */
extern uint8_t __psram_data_start__[];
extern uint8_t __psram_data_end__[];
extern uint8_t __psram_data_load__[];
extern uint8_t __psram_bss_start__[];
extern uint8_t __psram_bss_end__[];
extern uint8_t __psram_heap_start__[];

/* Place variable in PSRAM BSS section (zero-initialized at startup) */
#define __psram_bss(name) __attribute__((section(".psram_bss." name)))

/* Place variable in PSRAM data section (initialized from flash) */
#define __psram_data(name) __attribute__((section(".psram_data." name)))

/* Initialize PSRAM sections - call this early in main() after PSRAM init */
static inline void psram_sections_init(void) {
    /* Copy .psram_data from flash to PSRAM */
    size_t psram_data_size = __psram_data_end__ - __psram_data_start__;
    if (psram_data_size > 0) {
        memcpy(__psram_data_start__, __psram_data_load__, psram_data_size);
    }
    
    /* Zero .psram_bss in PSRAM */
    size_t psram_bss_size = __psram_bss_end__ - __psram_bss_start__;
    if (psram_bss_size > 0) {
        memset(__psram_bss_start__, 0, psram_bss_size);
    }
}

/* Get PSRAM heap start for dynamic allocation */
static inline void* psram_heap_start(void) {
    return __psram_heap_start__;
}

/* Get sizes for debug output */
static inline size_t psram_data_size(void) {
    return __psram_data_end__ - __psram_data_start__;
}

static inline size_t psram_bss_size(void) {
    return __psram_bss_end__ - __psram_bss_start__;
}

#endif /* PSRAM_SECTIONS_H */
