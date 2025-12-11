add_library(ps2kbd STATIC)

target_sources(ps2kbd PRIVATE
${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.cpp
${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.h
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.cpp
    ${CMAKE_CURRENT_LIST_DIR}/ps2kbd_wrapper.h
)

target_link_libraries(ps2kbd PRIVATE hardware_pio hardware_clocks)

target_include_directories(ps2kbd PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

# PS2 pin configuration passed from parent CMakeLists.txt
if(DEFINED PS2_DATA_PIN)
    target_compile_definitions(ps2kbd PRIVATE PS2_PIN_DATA=${PS2_DATA_PIN})
endif()
if(DEFINED PS2_CLK_PIN)
    target_compile_definitions(ps2kbd PRIVATE KBD_CLOCK_PIN=${PS2_CLK_PIN})
endif()

pico_generate_pio_header(ps2kbd 
${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr.pio
)

pico_generate_pio_header(ps2kbd 
${CMAKE_CURRENT_LIST_DIR}/ps2kbd_mrmltr2.pio
)
