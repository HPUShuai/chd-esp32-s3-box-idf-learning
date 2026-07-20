# 02 LCD Hello

This ESP-IDF project initializes the 2.8-inch LCD connected to the
CHD ESP32-S3-BOX V2.0 and draws a color background plus `HELLO` and `LCD` text.

## Hardware assumptions

- Controller: ILI9341-compatible SPI LCD, using the CHD vendor initialization
  sequence from the supplied board source code
- Resolution: 240 x 320 (the supplied 2.8-inch display)
- LCD SPI pins from the V2.0 schematic:
  - CS: GPIO5
  - DC: GPIO4
  - MOSI: GPIO6
  - SCLK: GPIO7
  - Backlight enable: GPIO47, active high

GPIO47 also drives the board's D9 red LED. It is used here only as the LCD
backlight-enable signal, so the LED may light while the display is on.

## Important: set the target before building

This project must be configured for `esp32s3`. In the ESP-IDF terminal, run:

```powershell
idf.py set-target esp32s3
```

The compile log must use `xtensa-esp32s3-elf-gcc`. If it uses
`xtensa-esp32-elf-gcc`, VS Code is still building for the older ESP32 target.
