# The Quake engine (../quake) is the MG24Quake one; ESP32 specific code paths are
# selected with ESP_PLATFORM.
set(QUAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/../quake)

file(GLOB QUAKE_SRCS ${QUAKE_DIR}/*.c)

idf_component_register(
    SRCS
        ${QUAKE_SRCS}
        font8x8.c
        esp_main.c
        esp_display.c
        esp_graphics.c
        esp_extmem.c
        esp_audio.c
        esp_input.c
        esp_touch.c
        esp_hid_parser.c
        esp_ble_hid.c
        esp_board_l2.c
        esp_board_tdeck.c
        esp_printf.c
    INCLUDE_DIRS
        port
        .
        ${QUAKE_DIR}
    REQUIRES
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_usb_serial_jtag
        esp_driver_i2s
        esp_driver_sdmmc
        esp_driver_sdspi
        sdmmc
        fatfs
        esp_partition
        esp_psram
        esp_timer
        nvs_flash
        bt
        esp_hid
)

# The engine is written for GCC 12 / C99 era rules and uses many implicit
# conversions (pointer <-> integer for the external memory addresses).
set_source_files_properties(${QUAKE_SRCS} PROPERTIES COMPILE_OPTIONS
    "-O2;-fpermissive;-Wno-error;-w;-fno-strict-aliasing")
# Hot rasterizer loops: the MG24 used hand written ARM assembly here, on the ESP32 the
# portable C code is used; -O3 lets the compiler unswitch/unroll the per-pixel loops.
set_source_files_properties(${QUAKE_DIR}/d_scan.c ${QUAKE_DIR}/d_polyse.c PROPERTIES COMPILE_OPTIONS
    "-O3;-fpermissive;-Wno-error;-w;-fno-strict-aliasing")
# Board selection from the command line: idf.py -DBOARD=BOARD_LILYGO_TDECK ...
if(DEFINED BOARD)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BOARD=${BOARD})
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-variable -Wno-error=unused-function)
