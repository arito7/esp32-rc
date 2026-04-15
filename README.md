# System
1. ESP32 serves as an access point and server that serves the UI via `http`
  - user can connect via wifi
    `http://192.168.4.1`
    `ESP_WIFI_SSID   ESP32_RC_CAR`
    `ESP_WIFI_PASS  pass1234`
2. When client connects via http it establishes web socket connection
3. ESP32 receives commands and controls motors :)

# Quickstart
Make sure you're in IDF environment (source the activation script for your version of IDF, if installed using EIM CLI on linux it should be in `~/.espressif/tools/activate_[version].sh`)

`idf.py set-target esp32`

`idf.py menuconfig`

`idf.py build flash monitor`

Uses tailwind for styling UI on client, so make sure you build tailwind
`bun run build:css`

## Technical support and feedback

- [ESP32 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html)
- [ESP32-S2 Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)

* For technical queries, go to the [esp32.com](https://esp32.com/) forum
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)
