# ESP32-S3 Wireless Bridge for TONEX Pedal

## 1. Project Overview & Objective

The goal of this sub-project is to transform the **TONEX Pedal Controller** into a standalone, wireless, battery/pedal-powered appliance. 

By connecting a small **ESP32-S3** microcontroller board to the TONEX Pedal via USB, the ESP32 acts as a self-contained web server, WebSocket bridge, and USB Host. This enables full remote preset browsing, editing, library sync, and real-time switching from any device on your LAN (e.g. iPad on couch, smartphone on stage, or desktop browser) with **zero latency** and **no host computer required**.

```
┌────────────────────────────────┐
│   iPad / iPhone / PC           │
│   (Safari / Chrome Browser)    │
│   http://tonex.local           │
└───────────────┬────────────────┘
                │ Wi-Fi (HTTP + WebSocket JSON / Binary)
                ▼
┌─────────────────────────────────────────────────────────────┐
│   ESP32-S3-DevKit (N16R8) Controller                        │
│                                                             │
│  • Onboard Web Server (serves index.html from LittleFS)     │
│  • WebSocket Server (handles remote commands in ~1-2ms)     │
│  • USB Host Controller (talks directly to ToneX hardware)   │
│  • Wi-Fi AP + Station Mode + mDNS (tonex.local)             │
│  • Status RGB LED (GPIO48)                                  │
└───────────────────────────────┬─────────────────────────────┘
                                │ USB Cable (Type-C to Type-B/C)
                                │ Native USB Host (GPIO 19/20)
                                ▼
┌─────────────────────────────────────────────────────────────┐
│   IK Multimedia TONEX Pedal                                 │
│   • USB-MIDI (Bank Select CC#0 + Program Change)            │
│   • USB-CDC Serial (Binary HDLC protocol for preset dump)   │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Hardware Specifications

### 2.1 Target Board: ESP32-S3-DevKit N16R8 (Dual Type-C)
* **Reference Hardware**: [ESP32-S3-DevKit N16R8 Dual Type-C Development Board (Amazon)](https://www.amazon.com/dp/B0GBT212KM) (also known as YD-ESP32-S3).
* **Microcontroller**: Espressif ESP32-S3 (Dual-core 32-bit Xtensa LX7 @ 240 MHz).
* **Flash Memory**: **16 MB (N16)** Quad-SPI Flash (stores firmware + LittleFS web assets).
* **PSRAM**: **8 MB (R8)** Octal-SPI PSRAM (high-speed buffering for WebSockets & preset library).
* **Wireless**: 2.4 GHz Wi-Fi (802.11 b/g/n) with integrated on-board PCB antenna (15–30m indoor range) + Bluetooth 5 (LE).
* **Power Draw**: ~0.4W – 0.8W (5V @ 80–150 mA).

### 2.2 Port & Pin Mapping

| Interface | Physical Connector / Pins | Function |
| :--- | :--- | :--- |
| **Power / Debug** | **Left Type-C Port** (USB-to-UART) | 5V Power input from USB charger / pedal power supply + Serial flashing/logging. |
| **ToneX USB Host** | **Right Type-C Port** (Native USB OTG) | Hardware USB Host connected to ToneX (`GPIO19 = D-`, `GPIO20 = D+`). |
| **Status RGB LED** | `GPIO48` | WS2812 NeoPixel for connection & sync status. |
| **Auxiliary Power** | `5V` & `GND` header pins | Alternative DC input for direct 9V→5V buck converter integration. |

---

## 3. Power Architecture

The TONEX Pedal is a USB peripheral/device and **does not supply 5V power over its USB port**. The ESP32-S3 acts as the USB Host and must be supplied with 5V DC.

### Recommended Power Options:
1. **9V DC Tap from ToneX Power Supply (Zero Wall-Wart Solution)**:
   * The included ToneX power adapter provides 9V DC @ 3.2A (3200mA). The pedal only consumes ~350mA.
   * A standard 2.1mm DC barrel Y-splitter connected to a compact 9V→5V step-down buck converter (or USB regulator) powers both the ToneX and ESP32 from a single wall plug.
2. **Pedalboard Power Supply**:
   * Connect to any isolated 5V USB output on pedalboard supplies (e.g. Cioks Crux/Grip, Strymon Zuma/Ojai with 5V adapter).
3. **Standard USB-C Phone Charger / Power Bank**:
   * Plug into the UART/Power Type-C port.

---

## 4. Software & Firmware Architecture

### 4.1 Development Environment
* **Platform**: PlatformIO (recommended) or Arduino IDE 2.x with ESP32 board support package (v3.x / ESP-IDF 5.x).
* **Filesystem**: `LittleFS` partition allocated on the 16MB flash (e.g. 8MB firmware / 8MB LittleFS data).

### 4.2 Firmware Modules

```
firmware/
├── include/
│   ├── config.h             # Wi-Fi credentials, mDNS hostname, pin definitions
│   ├── tonex_usb_host.h     # USB Host driver for MIDI & CDC endpoints
│   ├── tonex_hdlc.h         # HDLC framing, byte stuffing, CRC-CCITT for ESP32
│   ├── ws_bridge.h          # WebSocket server and JSON/Binary dispatcher
│   └── led_status.h         # RGB WS2812 status indicator controller
├── src/
│   ├── main.cpp             # Setup, Wi-Fi init, mDNS, task loops
│   ├── tonex_usb_host.cpp   # USB Host enumeration, MIDI Out, CDC read/write
│   ├── tonex_hdlc.cpp       # Ported CRC-CCITT & packet parser
│   ├── ws_bridge.cpp        # WebSocket client handler
│   └── led_status.cpp       # Visual feedback handler
├── data/                    # Web assets uploaded to LittleFS
│   ├── index.html           # Single-page web app with WS transport shim
│   └── favicon.svg          # App icon
└── platformio.ini           # Build flags, partition table, dependencies
```

### 4.3 Essential Firmware Libraries
* **`ESPAsyncWebServer`** or **Native `esp_http_server`**: High-performance asynchronous HTTP and WebSocket server.
* **`EspUsbHost` / `TinyUSB Host` / `esp_usb_host`**: Handles USB Host enumeration of the composite TONEX device (MIDI Class + CDC Serial Class).
* **`ArduinoJson`** (v7.x): Fast JSON serialization/deserialization for WebSocket messages.
* **`LittleFS`**: Flash file system to serve static web pages.
* **`ESPmDNS`**: Provides zero-configuration domain resolution (`http://tonex.local`).
* **`Freenove_WS2812_Lib_for_ESP32` / `FastLED`**: Controls onboard `GPIO48` RGB LED.

---

## 5. Communication Protocol (WebSocket Bridge Schema)

The iPad and ESP32 communicate in real-time over a persistent WebSocket connection at `ws://tonex.local/ws`.

### 5.1 Client -> ESP32 (Commands)

#### 1. MIDI Bank Select & Program Change
Sent when tapping a preset on the iPad:
```json
{
  "action": "midi_send",
  "bank": 0,
  "slot": "A",
  "pc": 0,
  "channel": 0
}
```

#### 2. Start Full USB Preset Sync
Sent when clicking "Sync USB" on the iPad:
```json
{
  "action": "sync_start"
}
```

#### 3. Save Preset Edit
Sent when editing a preset name or AMP/CAB flag on iPad:
```json
{
  "action": "save_preset",
  "bank": 0,
  "slot": "A",
  "name": "Custom Lead",
  "amp": true,
  "cab": true
}
```

---

### 5.2 ESP32 -> Client (Events & Updates)

#### 1. Connection Status
Broadcasted on client connect or USB connect/disconnect:
```json
{
  "event": "status",
  "tonex_connected": true,
  "active_pc": 0,
  "active_bank": 0,
  "active_slot": "A"
}
```

#### 2. Sync Progress
Broadcasted during preset dump (150 presets):
```json
{
  "event": "sync_progress",
  "loaded": 45,
  "total": 150,
  "percent": 30
}
```

#### 3. Sync Batch / Preset Data
Sends discovered presets to populate the library:
```json
{
  "event": "preset_data",
  "presets": {
    "0_A": { "name": "Clean Crunch", "amp": true, "cab": true },
    "0_B": { "name": "Heavy Lead", "amp": true, "cab": false }
  }
}
```

---

## 6. Frontend Integration (`index.html`)

The existing [`index.html`](file:///Users/brandon/Documents/repos/tonexpedal/index.html) is adapted using a **Transport Shim** that automatically detects if it is running on the ESP32 network bridge vs. standalone local browser:

```javascript
// Auto-detect environment
const isNetworkBridge = window.location.protocol.startsWith('http') && window.location.port !== '';

if (isNetworkBridge) {
    // Route MIDI and Sync over WebSocket to ESP32
    initWebSocketBridge();
} else {
    // Fall back to local browser Web MIDI & Web Serial (desktop USB)
    initLocalHardware();
}
```

This guarantees 100% backward compatibility with desktop Chrome/Edge direct USB connections while unlocking seamless Wi-Fi control on iOS Safari.

---

## 7. Implementation Roadmap

| Phase | Milestone | Deliverables |
| :--- | :--- | :--- |
| **Phase 1** | **Repository Structure & Docs** | `docs/ESP32_WIRELESS_BRIDGE.md`, `firmware/` scaffold. |
| **Phase 2** | **Firmware Core & USB Host** | ESP32-S3 USB Host implementation for TONEX MIDI & CDC HDLC reader. |
| **Phase 3** | **Web & WebSocket Server** | AsyncWebServer serving `index.html` via LittleFS, mDNS `tonex.local`. |
| **Phase 4** | **Frontend Bridge Adapter** | WebSocket transport hooks in `index.html` for zero-latency switching & sync. |
| **Phase 5** | **Hardware Enclosure & Field Test** | 3D-printed snap-fit enclosure, power integration testing. |

---

## 8. 3D Printable Enclosure Guidelines

* **Ready-to-Print 3D Model**: [Case for YD-ESP32-S3 N16R8 on Printables](https://www.printables.com/model/1774744-case-for-yd-esp32-s3-n16r8) (tested snap-fit case with dual Type-C cutouts and button access).
* **Dimensions**: ~65 mm (L) × 32 mm (W) × 14 mm (H).
* **Port Openings**:
  * Dual Type-C cutouts on the bottom face.
  * Light pipe or 2mm diffusion hole over the `GPIO48` RGB LED.
  * Ventilation micro-slots on the top/bottom lids.
* **Mounting**: Snap-fit lid with optional dual-lock Velcro recess for mounting under or behind the TONEX pedal.
