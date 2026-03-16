# Induction Cooker PID Controller (GIC3500)

Professional automated mashing controller using an ESP32-C6 for the GIC3500 induction cooker.
It automates the mashing phases according to a user-provided temperature and time schedule, regulating power using a CD74HC4067 multiplexer to switch cooker stages 0-11.

## Features
- **Wi-Fi & MQTT**: Reads the current temperature from a remote sensor over MQTT (`brew/sensor/temp`). Publishes status at `brew/cooker/status`.
- **PID Control Loop**: Uses a standard PID control algorithm, mapping continuous 0-100% output precisely to 11 discrete induction cooker power levels. Avoids overshoot by limiting higher stages during temperature holds.
- **Web UI & REST API**: Hosts a built-in HTTP server to easily submit multi-step mashing schedules (e.g., 65°C for 60 min, 75°C for 10 min) from any browser (PC or phone). No app required.
- **State Machine**: Automatically transitions through Heat up, Hold phase, and Completion, entirely hands-free.

## Build and Flashing
This project leverages the native **ESP-IDF** framework (instead of Arduino) for robust multitasking and networking stability.

1. Ensure you have PlatformIO installed with support for ESP-IDF.
2. The `platformio.ini` uses the `espressif32` platform configured for ESP-IDF.
3. Build the project:
   ```bash
   pio run
   ```
4. Flash to the ESP32-C6 device:
   ```bash
   pio run -t upload
   ```

*Note: If you run into `fatfs` Python package collision issues on macOS, it means your global Python environment has an incompatible `fatfs` PyPI package that conflicts with PlatformIO's build script. You may need to run `pip uninstall fatfs` to let PlatformIO use its own tool.*

## Web Interface
After flashing, connect the ESP32-C6 to your network (defaults to SSID `optical` and Pass `Passw0rd`).
Find its IP address from your router or via the Serial Monitor.
Navigate to the IP in any browser:
```
http://<ESP32_IP_ADDRESS>
```
You will be presented with the Brew Mashing Controller dashboard where you can define the heating schedule and start the induction cooker automatically.

## Hardware Multiplexer
- `PIN_S0 = 23`
- `PIN_S1 = 1`
- `PIN_S2 = 2`
- `PIN_S3 = 21`
- `PIN_EN = 22`

See the source code in `components/power_control/power_control.c` for pulse and hold timing configurations.

## Changelog
- **v1.0.0**: Initial ESP-IDF version with MQTT, PID, WebServer, and power control multiplexing capabilities.
