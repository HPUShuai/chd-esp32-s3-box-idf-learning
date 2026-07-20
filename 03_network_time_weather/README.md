# 03 Network Time and Weather

This project connects the CHD ESP32-S3-BOX to Wi-Fi, synchronizes Beijing time
with SNTP, then retrieves current weather from Seniverse and prints both to the
serial monitor.

The HTTPS request runs in its own 8 KB FreeRTOS task so the default ESP-IDF
`main` task does not overflow while processing the JSON response.

## Project layout

```text
main/
├─ main.c                         # Application flow
├─ lcd/lcd.c, lcd.h               # 2.8-inch LCD driver and network data view
├─ network/network.c, network.h   # Wi-Fi and SNTP time synchronization
├─ weather/weather.c, weather.h   # Seniverse HTTPS request and JSON parsing
├─ private_config.example.h        # Safe template committed to Git
└─ private_config.h                # Local credentials; ignored by Git
```

## Before building

1. Copy `main/private_config.example.h` as `main/private_config.h`.
2. Fill in your 2.4 GHz Wi-Fi SSID/password and Seniverse **private key**.
3. Keep `WEATHER_LOCATION` as an ASCII city name such as `hefei` or `beijing`.
4. Make sure the ESP-IDF target is `esp32s3`:

   ```powershell
   idf.py set-target esp32s3
   ```

The first successful build downloads no third-party display component; all
networking uses ESP-IDF built-in components and the certificate bundle.

## Expected serial output

```text
Connected to Wi-Fi
Beijing time: 2026-07-20 09:30:00
Weather: Hefei, 晴, 31 C
```

The LCD shows the synchronized `TIME`, fetched `TEMP`, and Seniverse weather
`CODE`. The API's Chinese weather text continues to be printed in the serial
monitor; displaying it requires a Chinese font, which is intentionally left for
a later UI task.

The Seniverse key is intentionally excluded from Git. Do not commit
`private_config.h`.
