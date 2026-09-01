# ESP32-S3 Wireless Bridge for TONEX Pedal

## 1. Project Overview & Objective

The goal of this sub-project is to transform the **TONEX Pedal Controller** into a standalone, wireless, battery/pedal-powered appliance. 

By connecting a small **ESP32-S3** microcontroller board to the TONEX Pedal via USB, the ESP32 acts as a self-contained web server, WebSocket bridge, and USB Host. This enables full remote preset browsing, editing, library sync, and real-time switching from any device on your LAN (e.g. iPad on couch, smartphone on stage, or desktop browser) with **zero latency** and **no host computer required**.

> **Implementation status:** the complete firmware path is implemented: WLAN hosting, bridge discovery, WebSocket messaging, physical USB enumeration, USB-MIDI output, CDC bulk transport, and the 150-preset synchronization state machine. USB descriptor matching, MIDI transfers, and CDC responses still require validation through the physical ESP32 host before the bridge should be considered hardware-ready.

The full-size pedal's MIDI encoding and CDC response/event formats have now been validated directly on macOS. Physical ESP32 descriptor claiming and endpoint behavior remain pending until the board is available.

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
│  • Wi-Fi Station Mode + mDNS (tonex.local)                  │
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

### 4.2 WLAN Configuration

The bridge normally joins an existing WLAN and receives its address through DHCP. It does not create an access point during normal operation.

1. Copy `firmware/include/wifi_secrets.example.h` to `firmware/include/wifi_secrets.h`.
2. Set `TONEX_WIFI_SSID` and `TONEX_WIFI_PASS` in the copied file.
3. Flash the firmware and LittleFS data.
4. Read the assigned IP address from the serial log, or open `http://tonex.local` from another device on the same WLAN.

The credential file is ignored by Git. Access-point fallback and browser-based provisioning are reserved for a later recovery workflow.

### 4.3 Firmware Modules

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

### 4.4 Essential Firmware Libraries
* **`ESPAsyncWebServer`** or **Native `esp_http_server`**: High-performance asynchronous HTTP and WebSocket server.
* **`EspUsbHost` / `TinyUSB Host` / `esp_usb_host`**: Handles USB Host enumeration of the composite TONEX device (MIDI Class + CDC Serial Class).
* **`ArduinoJson`** (v7.x): Fast JSON serialization/deserialization for WebSocket messages.
* **`LittleFS`**: Flash file system to serve static web pages.
* **`ESPmDNS`**: Provides zero-configuration domain resolution (`http://tonex.local`).
* **`Freenove_WS2812_Lib_for_ESP32` / `FastLED`**: Controls onboard `GPIO48` RGB LED.

---

## 5. Communication Protocol (WebSocket Bridge Schema)

The iPad and ESP32 communicate in real-time over a persistent WebSocket connection at `ws://tonex.local/ws`.

Before opening the socket, the shared frontend requests `GET /api/bridge`. A bridge identifies itself with:
```json
{
  "service": "tonex-bridge",
  "protocol_version": 1
}
```
This prevents ordinary HTTP-hosted copies of the application from repeatedly probing for a WebSocket bridge.

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

#### 3. Cancel Preset Sync
Sent when clicking the sync button while a bridge sync is active:
```json
{
  "action": "sync_cancel"
}
```

---

### 5.2 ESP32 -> Client (Events & Updates)

#### 1. Connection and Active-Preset Status
Broadcasted on client connect, USB connect/disconnect, or a confirmed unsolicited CDC preset event:
```json
{
  "event": "status",
  "tonex_connected": true,
  "active_pc": 0,
  "active_bank": 0,
  "active_slot": "A"
}
```

The active-preset fields are omitted until the bridge has observed a preset event. This prevents a new connection from incorrectly assuming preset 0. Changes made from the pedal footswitches, another MIDI controller, or the web UI are then reflected in every connected browser.

Direct pedal testing found that preset forward/backward, A/B/C selection, and a bank selection confirmed with A/B/C all emit this active-preset event. Bypass toggles and unconfirmed bank browsing emit no unsolicited CDC frame.

Bypass was also absent from the known read paths: repeated short State responses and full active-preset responses remained byte-for-byte stable through bypass-off/on/off cycles. The pedal's documented MIDI CC12 command can set preset off/on, but it emits no CDC acknowledgement. Physical bypass produced no USB-MIDI feedback even during a test with `MIDI.THRU` set to `MERGE`. Consequently, the bridge must not publish a confirmed bypass boolean from the currently known protocol. A future UI may expose CC12 as a command, but its result must remain explicitly unconfirmed unless another feedback mechanism is discovered.

During a library sync, unsolicited active-preset events are dispatched immediately without consuming the pending request. Only a solicited response carrying the expected preset index advances the sync state machine; unexpected or mismatched responses fail the sync explicitly.

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

#### 3. Preset Data
Sends each discovered preset to populate the library:
```json
{
  "event": "preset_update",
  "bank": 0,
  "slot": "A",
  "name": "Clean Crunch",
  "amp": true,
  "cab": true
}
```

#### 4. Sync Lifecycle and Errors
The bridge emits `sync_complete` after all presets, `sync_cancelled` after cancellation, or a structured `error` event when a command cannot be completed:
```json
{ "event": "sync_complete", "total": 150 }
{ "event": "sync_cancelled" }
{ "event": "error", "code": "sync_unavailable", "message": "Preset sync is already active or the TONEX is disconnected" }
```

---

## 6. Frontend Integration (`index.html`)

The existing `index.html` uses explicit local-hardware and WebSocket transport adapters. It only activates the bridge transport after the server positively identifies itself:

```javascript
const response = await fetch('/api/bridge');
const identity = await response.json();
if (identity.service === 'tonex-bridge' && identity.protocol_version === 1) {
    connectWebSocketBridge();
}
```

If discovery does not identify a bridge, preset switching and synchronization remain on the original Web MIDI and Web Serial/WebUSB path.

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
