# ESP32 mp3-player with internet radio
 This project implements a portable audio player based on ESP32, capable of playing MP3/WAV files from an SD card and online MP3 radio streams via WiFi.
It features a color TFT graphical interface, physical button controls, and audio output through an I2S audio expansion module.

Hardware Components
1. MakePython ESP32 Color LCD Wrover
🔗 https://www.makerfabs.com/makepython-esp32-color-lcd.html
2. MakePython Audio Expansion
🔗 https://www.makerfabs.com/makepython-audio.html

Libraries needed for it to work:
-TFT_eSPI
 https://github.com/Bodmer/TFT_eSPI
-ESP32 Audio (ESP32-audioI2S)
 https://github.com/schreibfaul1/ESP32-audioI2S

Project Functionalities
-The player supports MP3 and WAV audio files stored on the SD card.
Audio files are detected at startup, stored in a playlist, and played sequentially. The user can navigate between tracks, pause playback, and adjust the volume.
-The device connects to online MP3 radio stations via WiFi.
Radio stations are defined as MP3 stream URLs and accessed using the connecttohost() function. The stream is decoded in real time and output through the I2S interface.

The application supports two operating modes:
-SD Card Mode
-Radio Mode


Modifications to the TFT_eSPI Library

To ensure correct operation of the TFT display with the MakePython ESP32 Color LCD Wrover, configuration changes were made to the TFT_eSPI library.
Only hardware configuration files were modified; the internal library logic remains unchanged.

 Modifications to User_Setup.h

The display was configured for an ST7789 240×240 SPI TFT and the correct pin mapping:

#define USER_SETUP_ID 24
#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// --- MakePython Color LCD SPI Pins ---
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   22
#define TFT_RST  21

// Backlight (controlled via PWM in code)
#define TFT_BL   5

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY 2500000

📄 Modifications to User_Setup_Select.h

Only the required setup file was enabled; all other setup lines were commented out:

#include <User_Setups/Setup24_ST7789.h>

