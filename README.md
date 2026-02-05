# ESP32 HVAC Thermostat

A Wi-Fi thermostat controller on ESP32 (DHT22 + relay + OLED) with a simple API and OTA updates.

## Build & Upload (PlatformIO)
1. Open this folder in VS Code
2. Install PlatformIO IDE
3. Add Wi-Fi credentials (see include/secrets_example.h)
4. Build/Upload

## Quick API test
- GET /api/status
- GET /api/set?t=23.5

## Notes
Default PIN / Wi-Fi details depend on your config.
