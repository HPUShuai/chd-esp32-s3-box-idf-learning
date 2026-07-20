# CHD ESP32-S3-BOX ESP-IDF Learning

This repository records my ESP-IDF learning projects for the CHD ESP32-S3-BOX.

## Board

- Target: `esp32s3`
- Board: CHD ESP32-S3-BOX V2.0
- On-board red LED: D9 (`GPIO47`, active high)

## Learning projects

| Project | Topic | Status |
| --- | --- | --- |
| `01_gpio_led` | Blink the on-board D9 red LED with GPIO47 | In progress |

## Build and flash

Open the desired project directory, then run:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port assigned to the development board.

## Repository rules

- Commit only source code, documentation, and small verification screenshots.
- Do not commit build output, firmware binaries, archives, credentials, or the external `资料` folder.
- Make one commit after each task has been verified on the board.
