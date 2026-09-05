# ESP32-P4 WiFi6 Dev Kit: factory-C6 build

Use this build for the Waveshare ESP32-P4-WIFI6-DEV-KIT with its original
ESP32-C6 firmware. It deliberately uses ESP-Hosted 1.4.7, which the factory
C6 speaks, rather than the newer protocol bundled by PIOArduino.

The build script downloads pinned, open-source dependencies on its first run:

- Arduino-ESP32 `3.3.11`, with a small local Hosted compatibility overlay;
- AsyncTCP `3.3.2` and ESPAsyncWebServer `3.6.0`;
- IDF component-manager dependencies locked in `dependencies.lock`.

Install and export ESP-IDF 5.5.x, then build from the repository root:

```sh
. "$IDF_PATH/export.sh"
firmware/p4_legacy/build.sh build
```

Flash through the board's **USB TO UART** connector:

```sh
. "$IDF_PATH/export.sh"
firmware/p4_legacy/build.sh -p /dev/cu.usbmodem... flash
```

The image includes the existing LittleFS web assets. The first boot starts a
`TONEX-Setup-XXXXXX` access point at `http://192.168.4.1` when no Wi-Fi
credentials are stored. The `USB` connector remains the TONEX host port; the
`USB TO UART` connector is for flashing and serial monitoring.

No C6 recovery, external UART adapter, or C6 firmware update is required.

## Setup UI follow-up

- Add an explicit show/hide-password control to the Wi-Fi setup form, so a
  password can be checked before saving it to the bridge.
