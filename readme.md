# Quake for ESP32-S3

A port of [MG24Quake](https://github.com/next-hack/MG24Quake), Nicola Wrachien's Quake port for
Silicon Labs EFR32MG24 microcontrollers (276 kB RAM), to the ESP32-S3. It runs the **full retail
Quake** on the [Seeed Wio Tracker L2 / L2 Pro](https://wiki.seeedstudio.com/meshtastic_wio_tracker_l2_intro/)
with its touch screen, speaker and microSD card, and supports Bluetooth LE keyboards, mice and
gamepads. Other ESP32-S3 boards with PSRAM, a 320x240 SPI display and an SD card can be
configured in [`board_config.h`](QuakeESP32S3/main/board_config.h).

Based on WinQuake/SDLQuake by id Software, MG24 port by Nicola Wrachien (next-hack). The
technical background of the engine is described in the next-hack articles:

- [Quake port to Sparkfun and Arduino Nano Matter boards using only 276 kB RAM](https://next-hack.com/index.php/2024/09/22/quake-port-to-sparkfun-and-arduino-nano-matter-boards-using-only-276-kb-ram/)
- [Now even the full retail Quake runs on the Arduino Nano Matter board](https://next-hack.com/index.php/2024/11/02/now-even-the-full-retail-quake-version-runs-on-the-arduino-nano-matter-board-and-it-is-faster-as-well/)

## Features

- Full Quake engine (MG24Quake 2.0):
  - static and dynamic lighting with bilinear interpolation, Gouraud shading on enemies;
  - animated sky, turbulent surfaces (water, teleports…), underwater warp effect;
  - particles for smoke, blood, etc.
- 320x200 rendering (Quake's default resolution), stretched to the full 320x240 screen (the
  4:3 aspect ratio of the original monitors).
- Full game logic: monster AI, teleports, triggers, doors…
- Shareware and **full retail** episodes, including the skill/episode selection map.
- Sound: 8 bit stereo, 11025 Hz; static, ambient and dynamic sounds.
- Console with cheats, options with key remapping, savegames with full game state.
- Touch screen controls with multi-touch; Bluetooth LE keyboards, mice and gamepads.
- About 18 fps on the Wio Tracker L2 (ESP32-S3 at 240 MHz).

## Limitations

- No CD audio / music.
- No multiplayer.
- Fixed screen size: the status bar can't be reduced or removed.
- Bluetooth LE only (the ESP32-S3 has no Bluetooth Classic), one Bluetooth device at a time.
- The 2021 remaster `pak0.pak` is not supported: use the original (1.06) PAK files.

## Quick start (Wio Tracker L2)

You need the firmware image (`QuakeESP32S3-wio-tracker-l2.bin`, from the GitHub releases or
built as described below) and your own copy of Quake: the shareware `pak0.pak`, or the retail
`pak0.pak` + `pak1.pak`.

1. Convert the PAK files (see [PAK file](#pak-file)) and copy the result to the root of a
   FAT32-formatted microSD card as `PAK0.PAK`. Insert the card in the L2.
2. Flash the firmware. The L2 appears as a USB serial port; if flashing fails, hold BOOT while
   pressing RESET to enter download mode.

   ```sh
   esptool.py --chip esp32s3 -p <port> -b 921600 write_flash 0x0 QuakeESP32S3-wio-tracker-l2.bin
   ```

   This replaces the Meshtastic firmware. Back it up first if you want to go back
   (`esptool.py -p <port> read_flash 0 0x1000000 backup.bin`).
3. Press RESET. Boot messages appear on the screen and on the USB serial console.

## Hardware

| Item | Wio Tracker L2 | Generic board |
|---|---|---|
| MCU | ESP32-S3, 16 MB flash, 8 MB octal PSRAM | ESP32-S3 with PSRAM (8 MB recommended, e.g. N8R8 / N16R8), flash ≥ 4 MB |
| Display | 3.2" 320x240 NV3031B, QSPI | 320x240 SPI LCD, ST7789 or ILI9341 |
| Storage | microSD, SDMMC 1-bit | SD card (FAT), SPI or SDMMC (1/4 bit) |
| Audio | ES8311 codec + speaker amplifier | optional I2S DAC/amplifier (MAX98357A, PCM5102…) or PDM on one pin |
| Input | GT911 touch screen, BOOT and WAKE buttons, Bluetooth LE | Bluetooth LE, optional GPIO buttons |

On the L2, power rails, display reset and the touch controller are driven through the PCA9555
GPIO expander, and the backlight through the LP5814 LED driver, all on the I2C bus (SDA 47,
SCL 48). See [`esp_board_l2.c`](QuakeESP32S3/main/esp_board_l2.c).

Bluetooth: BLE keyboards and mice, Xbox Wireless controllers (firmware 5.x+), 8BitDo pads in
BLE/Android mode and generic BLE gamepads work. Bluetooth Classic-only devices (DualShock 4,
DualSense, most Switch Pro controllers, many older keyboards and mice) can't be used.

## Controls

### Touch screen

Once the screen has been touched, the buttons are drawn over the 3D view. They are hidden again
when a Bluetooth controller connects.

| Area | In game | In menus / console |
|---|---|---|
| Left half | virtual stick, centered where the thumb lands: move / strafe | swipe = arrow keys |
| Right half | drag to turn / look | swipe = arrow keys, tap = select |
| Big circle, bottom right | fire (drag from it to aim while firing) | |
| Middle circle, right | jump / swim up | |
| Small circle, top right | next weapon | |
| ☰ box, top left | menu | back |

Move and look at the same time with two thumbs. Layout, stick radius and look speed are at the
top of [`esp_touch.c`](QuakeESP32S3/main/esp_touch.c) and [`esp_input.c`](QuakeESP32S3/main/esp_input.c).

### Board buttons (L2)

BOOT opens the menu (escape); WAKE jumps in game and selects in menus.

### Bluetooth LE keyboard, mouse and gamepad

A keyboard behaves like a PC keyboard (default Quake bindings, console with `` ` ``). Mouse
buttons are MOUSE1..3, the wheel is MWHEELUP/DOWN, and movement turns / looks.

Gamepad (standard "Android" button layout):

| Button | In game | In menus |
|---|---|---|
| Left stick | move / strafe | navigate |
| Right stick | turn / look | |
| D-pad | forward/back/turn (arrows) | navigate |
| RT | fire | select |
| LT | run | |
| A | jump / swim up | select |
| B | swim down | back |
| X | next weapon | |
| Y | scores | |
| LB / RB | strafe left / right | |
| Start / Home | menu | back |
| Select | console | console |

Button assignments are in the tables at the top of [`esp_input.c`](QuakeESP32S3/main/esp_input.c).

**Pairing:** put the device in pairing mode, and disconnect it from any phone or computer it is
paired with (or it will reconnect there). The ESP32-S3 keeps scanning for BLE HID devices, even
during the game, and connects to the strongest one (bonded devices are preferred), reconnecting
automatically. If a device is not found, set `BLE_HID_DEBUG` to 1 and check which advertisers
the serial log shows. To forget bonds, erase the NVS partition (`idf.py erase-flash` also
erases savegames).

## PAK file

The game data is read directly from the SD card, converted to the MG24Quake format (which
changed in MG24Quake 2.0: PAK files converted for older releases must be converted again).

1. Build the converter (or use the Windows executable in
   `QuakeESP32S3/tools/MCUPackConverter/bin/Debug/`):

   ```sh
   cc -O2 -o MCUPackConverter QuakeESP32S3/tools/MCUPackConverter/*.c -lm
   ```

2. Run it in a directory containing `pak0.pak` (and the retail `pak1.pak`, if you have it). It
   prints a lot of data and asks for a key press at the end. The result is a single
   `PAK0Conv.PAK`: `pak0` and `pak1` are joined.
3. Copy `PAK0Conv.PAK` to the root of the SD card as `PAK0.PAK`.

## Building

With ESP-IDF 5.5 installed and exported:

```sh
cd QuakeESP32S3
idf.py set-target esp32s3
idf.py build flash monitor
```

To create the merged image for a release:

```sh
cd QuakeESP32S3/build
esptool.py --chip esp32s3 merge_bin -o QuakeESP32S3-wio-tracker-l2.bin @flash_args
```

`platformio.ini` is provided for PlatformIO users (pioarduino platform, ESP-IDF 5.5) but has
not been tested.

### Configuration

Select the board with `BOARD` in [`board_config.h`](QuakeESP32S3/main/board_config.h)
(`BOARD_WIO_TRACKER_L2` by default, or `BOARD_GENERIC`). For a generic board set the display
SPI pins, controller type, `DISPLAY_MADCTL` (orientation) and color inversion, the SD card mode
and pins (the card can share the display SPI bus, at the cost of some speed), the audio output
and the optional GPIO buttons.

The default `sdkconfig.defaults` is for **octal PSRAM** modules (N8R8, N16R8). For quad PSRAM
modules (e.g. N8R2) replace `CONFIG_SPIRAM_MODE_OCT=y` with `CONFIG_SPIRAM_MODE_QUAD=y`.

| Option | Default | |
|---|---|---|
| `DISPLAY_STRETCH_TO_FULL_HEIGHT` | 1 | stretch 320x200 to 320x240; 0 = 1:1 pixels with black bars |
| `TOUCH_CONTROLS_ENABLED` | 1 | on-screen controls on boards with a touch controller |
| `TOUCH_SWAP_XY`, `TOUCH_INVERT_X/Y` | L2 values | touch to screen coordinate mapping |
| `TOUCH_DEBUG` | 0 | log raw touch coordinates |
| `BLE_HID_ENABLED` | 1 | Bluetooth LE HID host |
| `BLE_HID_DEBUG` | 0 | log every nearby BLE advertiser while scanning |

The L2 speaker volume is set by ES8311 register `0x32` in `boardAudioCodecInit()`
([`esp_board_l2.c`](QuakeESP32S3/main/esp_board_l2.c), 0.5 dB steps, `0xBF` = 0 dB; the
default is -14 dB).

## Repository layout

| Path | Contents |
|---|---|
| `QuakeESP32S3/main` | ESP32-S3 platform code: display, SD/PAK access, audio, input, touch, Bluetooth, board support |
| `QuakeESP32S3/main/port` | headers replacing the MG24 platform headers used by the engine |
| `QuakeESP32S3/quake` | the MG24Quake engine; ESP32 code paths are selected with `ESP_PLATFORM` |
| `QuakeESP32S3/tools/MCUPackConverter` | PAK converter |
| `QuakeESP32S3/tools/QuakeCConverter`, `FunctionArrayCreator`, `QTablesGenerator` | MG24Quake tools that generate engine sources (QuakeC progs, field accessors, lookup tables); only needed to change the game code |

## How it works

| MG24 | ESP32-S3 |
|---|---|
| PAK in 2 interleaved SPI flash chips, read with DMA | PAK read from the SD card in 8 kB blocks, cached in PSRAM (all free PSRAM, CLOCK replacement). Asynchronous reads complete immediately. |
| Savegames/settings at the end of the SPI flash | `quakenvm` flash partition, mapped at the same virtual address |
| Internal flash used as model/skin cache | 1 MB PSRAM buffer (`INTERNAL_FLASH_CACHE_SIZE`) |
| 20 kB auxiliary radio RAM (`AUX_SECTION`) | PSRAM |
| ARM inline assembly in the rasterizer | the portable C code paths (the ones used by the MG24 Windows build) |
| Display LDMA with progressive refresh | display task on core 0 converts the 8-bit frame through the palette (stretching it to 240 lines and drawing the touch overlay) and sends it by SPI/QSPI DMA, updating the same "lines sent" counter the renderer waits on |
| Audio DAC + LDMA ring buffer | audio task playing the same ring buffer through I2S |

The game runs on core 1 and never yields; display, audio, touch and Bluetooth run on core 0.

## License

GPL v2, see [LICENSE](LICENSE). The tiny printf implementation
([`printf.c`](QuakeESP32S3/main/printf.c), Marco Paland) is MIT licensed.

Quake game data (PAK files) is not included and is not covered by this license: you need your
own copy of the game (the shareware `pak0.pak` is freely distributable).
