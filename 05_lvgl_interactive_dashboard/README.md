# 05 LVGL Interactive Dashboard

This lesson extends the completed LVGL weather dashboard with physical input,
event-driven FreeRTOS tasks, PWM backlight control, and persistent NVS settings.
It uses the board's **BOOT button on GPIO0**. The RST button remains dedicated to
reset and the MUTE switch remains a hardware audio control.

## BOOT button gestures

| Gesture | Result |
| --- | --- |
| Short press | Show the next dashboard page |
| Double press | Toggle automatic rotation and manual paging |
| Long press | Cycle LCD brightness through 25%, 50%, 75%, and 100% |

The dashboard starts in manual mode. The footer page marker uses `M` in manual
mode and `A` in automatic mode. For example, `M 2/3` means manual mode on the
forecast page. A previously saved NVS setting takes precedence over the default.

GPIO0 is also an ESP32-S3 strapping pin. It is safe to use after the firmware has
started, but holding BOOT while resetting or powering on the board enters the ROM
download mode.

## Learning goals

- Configure an active-low GPIO input with its internal pull-up.
- Wake a FreeRTOS task from a GPIO interrupt.
- Debounce a mechanical switch without doing work in the ISR.
- Recognize short, double, and long presses with a state machine.
- Send typed events through a FreeRTOS queue.
- Lock LVGL before updating UI objects from another task.
- Drive GPIO47 with LEDC PWM to control LCD brightness.
- Save interaction mode, brightness, and the last page in NVS.

## New project structure

```text
main/
├─ input/button.c, button.h         # GPIO0 ISR, debounce, gesture events
├─ settings/settings.c, settings.h # NVS load/save wrapper
├─ display/lcd_lvgl.c              # ILI9341 + LEDC backlight PWM
├─ network/network.c               # Wi-Fi and SNTP
├─ weather/weather.c               # Seniverse + Open-Meteo data
├─ ui/dashboard.c                  # LVGL pages and A/M interaction mode
└─ main.c                          # Tasks and event coordination
```

## Before building

Copy the private configuration template and enter local values:

```powershell
Copy-Item main/private_config.example.h main/private_config.h
```

Do not commit `main/private_config.h`. It contains the Wi-Fi password and
Seniverse API key and is already excluded by the repository `.gitignore`.

## Build and flash

Use ESP-IDF 5.5.4 with the ESP32-S3 target:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM12 flash monitor
```

## Acceptance checks

1. A short press advances exactly one page without bouncing.
2. A double press changes the page marker between `A` and `M`.
3. Pages stop rotating in manual mode and resume in automatic mode.
4. A long press visibly changes LCD brightness.
5. Brightness, mode, and the current page survive a reset.
6. Button interaction remains responsive while weather HTTPS requests run.
