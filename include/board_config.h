// Pin map for the Guition JC4827W543.
// These match the commonly published pinout for this board, but batches vary.
// CHECK THEM against the seller's schematic or the silkscreen before first flash.
#pragma once

// Display: NV3041A panel over QSPI, 480 x 272
#define LCD_WIDTH   480
#define LCD_HEIGHT  272
#define LCD_CS      45
#define LCD_SCK     47
#define LCD_D0      21
#define LCD_D1      48
#define LCD_D2      40
#define LCD_D3      39
#define LCD_BL      1     // backlight, PWM

// Capacitive touch (the "C" version): GT911 over I2C
#define TOUCH_SDA   8
#define TOUCH_SCL   4
#define TOUCH_INT   3
#define TOUCH_RST   38

// Set to 0 for the resistive ("R") or no-touch ("N") versions until we add their drivers
#define HAS_GT911_TOUCH 1
