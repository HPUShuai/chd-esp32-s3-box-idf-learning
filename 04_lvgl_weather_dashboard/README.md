# 04 LVGL Weather Dashboard

This lesson rebuilds the network weather project as a polished **LVGL** dashboard
for the CHD ESP32-S3-BOX 2.8-inch ILI9341 display. It uses a dark, high-contrast
technology theme and changes pages automatically every eight seconds, so a touch
screen is not required.

## What it displays

The dashboard requests three Seniverse V3 endpoints:

| Page | Endpoint | Displayed fields |
| --- | --- | --- |
| LIVE | `weather/now.json` | Temperature, condition code, humidity, wind speed/direction/scale, pressure, visibility, feels-like temperature, clouds, dew point, source update time |
| FORECAST | `weather/daily.json` | Three days of condition, high/low temperature, humidity, rainfall, precipitation probability, wind data |
| AIR | `air/now.json` | AQI, air-quality class, PM2.5, PM10, SO2, NO2, O3, CO, primary pollutant |

The free Seniverse plan can return only a subset of a paid endpoint's fields.
The UI deliberately renders missing or unauthorized data as `N/A`, instead of
restarting. Three-day daily forecast is the usual free-plan maximum.

The built-in LVGL Montserrat fonts do not contain Chinese glyphs. For a compact
and reliable 2 MB-flash learning project, the dashboard presents weather codes
as English labels such as `SUNNY`, `CLOUDY`, and `RAIN`; the raw Chinese response
is retained in the data model for a later custom Chinese-font exercise.

## Project layout

```text
main/
├─ display/lcd_lvgl.c, lcd_lvgl.h  # CHD ILI9341 SPI panel and LVGL port
├─ network/network.c, network.h    # Wi-Fi STA + SNTP (China Standard Time)
├─ ui/dashboard.c, dashboard.h     # LVGL visual hierarchy and page updates
├─ weather/weather.c, weather.h    # Seniverse HTTPS queries and JSON parsing
├─ main.c                           # Application/task lifecycle
├─ private_config.example.h         # Safe committed credentials template
└─ private_config.h                 # Local secret values, ignored by Git
```

## Before the first build

1. Copy `main/private_config.example.h` to `main/private_config.h`.
2. Fill in 2.4 GHz Wi-Fi credentials and the Seniverse **private key**.
3. Set `WEATHER_LOCATION` to `xiaogan` for Xiaogan, or another supported
   location identifier.
4. Use ESP-IDF 5.5.4 and select the ESP32-S3 target:

   ```powershell
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM12 flash monitor
   ```

The first build downloads the declared managed components: ILI9341 driver,
LVGL 8.3, and Espressif's LVGL port. Do not copy `private_config.h` from this
project into Git.

## Flash layout

LVGL and the selected fonts make the application larger than ESP-IDF's default
1 MiB factory partition. This project therefore configures the board's detected
**16 MiB flash** and supplies `partitions.csv` with a **3 MiB factory app
partition**. This extra capacity is used by LVGL's built-in SimSun Chinese font.
Do not replace this project's `sdkconfig.defaults`
or `partitions.csv` with the default single-app layout.

## Data and refresh behavior

- The digital clock is refreshed by LVGL once per second.
- The pages rotate every 8 seconds: LIVE → FORECAST → AIR.
- Wi-Fi/SNTP setup and HTTPS queries run in a dedicated 10 KB FreeRTOS task.
- Weather is refreshed every 10 minutes; network/API failures leave the UI alive
  and show a clear footer status.

## Key compatibility decisions

- `espressif/esp_lvgl_port ^2.6.2`: supports ESP-IDF 5.1+ and bridges LVGL to
  `esp_lcd`.
- `lvgl/lvgl ^8.3.11`: explicit stable LVGL API selection, avoiding a silent
  upgrade to LVGL 9 while learning.
- The LCD uses the vendor's ILI9341 initialization commands and GPIO 47
  backlight enable, GPIO 5/4/6/7 SPI wiring, and LVGL hardware rotation.
