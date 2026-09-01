# TONEX Pedal Controller

Single-page web controller for the IK Multimedia TONEX Pedal. Manages presets via USB MIDI and reads names/configurations directly from the pedal via the USB CDC serial interface.

Computer:
![Interface PC](captures/tnx1.png)

Android smartphone:
![Interface Android1](captures/android1.png)
![Interface Android2](captures/android2.png)


Computer demo video:
[![Video PC](https://img.youtube.com/vi/ZrpM73ms7fk/0.jpg)](https://www.youtube.com/watch?v=ZrpM73ms7fk)

Android demo vido:
[![Video Android](https://img.youtube.com/vi/XhKJ70A9dGQ/0.jpg)](https://www.youtube.com/watch?v=XhKJ70A9dGQ)

## Features

- **3×3 Grid** of assignable presets with names and AMP/CAB badges
- **Full library** of 150 presets (50 banks × 3 slots A/B/C)
- **USB Sync** — reads all names and AMP/CAB flags directly from the pedal
- **MIDI Control** — sends Bank Select + Program Change to change presets
- **Truthful live status** — distinguishes requested, bridge-accepted, pedal-confirmed, timed-out, and stale preset states
- **Connection recovery** — shows browser/bridge, pedal, and library health and reconnects after network or USB interruptions
- **Drag & drop** — assign a preset to a button, swap between buttons, or delete via trash
- **Editing** — double-click to rename a preset and toggle AMP/CAB
- **Search** — filtering in the library
- **Persistence** — configuration saved in localStorage
- **Responsive** — adaptive text via `container-type: inline-size` + `cqi` units
- **Library toggle** — discreet chevron to minimize/expand the preset library
- **Export/Import JSON** — export preset names to a file, import them on another setup
- **Android support** — works on Android Chrome via WebUSB fallback (Web Serial not available on Android)
- **Wireless Bridge (ESP32-S3)** — standalone Wi-Fi remote control from iOS Safari or any LAN browser with zero computer required

## Prerequisites

| Component | Required version |
|-----------|-----------------|
| Browser | Chrome 89+ or Edge 89+ (Web MIDI + Web Serial API) or iOS Safari (via ESP32 Bridge) |
| OS | Windows 10/11, Android (via WebUSB), iOS / macOS (via ESP32 Bridge) |
| Pedal | IK Multimedia TONEX Pedal (full size) |
| Cable | USB-C connected to the pedal's USB port |

> **Note**: On Android, Web Serial is not available — the app falls back to WebUSB for USB CDC communication. MIDI is not available on Android (no Web MIDI API).

## Installation & Deployment

### Option 1 — Local web server (recommended for desktop)

Copy the `tonexpedal/` folder to your web server root, then access via:
```
https://your-server/tonexpedal/
```

### Option 2 — localhost with a simple server

```bash
# From the tonexpedal/ folder
npx serve -s . -l 3000
# or
python -m http.server 3000
```

Then open `http://localhost:3000`.

### Option 3 — Static file (no server required)

Simply double-click `index.html` or open it via `file:///` in your browser.

### Option 4 — Standalone Wireless Bridge (ESP32-S3)

For remote control from iOS Safari (iPad/iPhone) on your couch or stage with zero computer running:
1. Flash the firmware and LittleFS data in [`firmware/`](firmware/) to an **ESP32-S3-DevKit** board ([Amazon Example](https://www.amazon.com/dp/B0GBT212KM)).
2. On first boot, join the temporary `TONEX-Setup-XXXXXX` network using password `tonexsetup`, then open `http://192.168.4.1`.
3. Enter the existing WLAN name/password and save. The temporary setup network stops after the bridge joins that WLAN.
4. Rejoin the normal WLAN and open `http://tonex.local` (or the custom device name entered during setup).
5. Connect the ESP32 to the ToneX Pedal via USB and power it from 5V (or a 9V tap).
6. 3D print a snap-fit enclosure ([Printables 3D Model](https://www.printables.com/model/1774744-case-for-yd-esp32-s3-n16r8)).

The setup access point is a provisioning/recovery path only; the controller runs in
Wi-Fi station mode during normal use. Captive-portal redirection is not yet
implemented, so open `192.168.4.1` explicitly. For a deployed unit, override
`TONEX_SETUP_AP_PASSWORD` in the build configuration instead of retaining the
development default. Compile-time `wifi_secrets.h` credentials remain supported as
an optional fallback.

To use the bridge at a venue with a different WLAN, power-cycle the ESP32. It tries
the saved network for 15 seconds, then opens the setup network when that connection
fails. Join it, browse to `192.168.4.1`, and save the venue WLAN; the new settings
replace the old ones. V1 has no physical provisioning/reset button, so a power cycle
is currently required to enter setup mode after the bridge has already been running.

See full guide: [ESP32-S3 Wireless Bridge Documentation](docs/ESP32_WIRELESS_BRIDGE.md).

## Usage

### MIDI Connection

1. Connect the TONEX Pedal via USB
2. Open the app in Chrome/Edge
3. Select the MIDI device in the **Device** dropdown
4. Choose the MIDI channel (default: Ch 1)
5. Status changes to **Connected** (green dot)

### USB Sync (reading presets)

1. Click **Sync USB**
2. Select the TONEX Pedal serial port in the dialog
3. Progress shows: Hello → State → Reading 150 presets
4. Names and AMP/CAB badges fill in automatically
5. Button shows **Done! X/150 presets read**

### Export / Import JSON

- Click **⬇ JSON** to download all preset names as `tonex-presets.json`
- Click **⬆ JSON** to import a previously exported file (validates format, replaces names)

JSON format:
```json
{
  "0_A": "Trooper - 80s Pack",
  "0_B": "80s Lead - 80s Pack",
  "1_A": "Final Countdown - 80s Pack"
}
```

### 3×3 Grid

- **Single click** on a button → sends Bank Select + Program Change to the pedal
- **Keys 1–9** → activate the corresponding performance pad without moving focus
- **Drag** a preset from the library → assigns to the button
- **Drag** a button to another → swaps positions
- **Drag** a button to the trash → clears the button
- **Double-click** → opens edit modal (name, AMP, CAB)

### Stage Lock

Use **STAGE LOCK** after preparing the nine performance pads. It collapses the
library, disables editing and drag/drop, enlarges the performance surface on
phones, and requests a screen wake lock where the browser supports it. The stage
status chip reports whether the screen is being held awake. The fullscreen button
enters Stage Lock automatically; **EXIT STAGE** restores editing and the previous
library layout.

### Library

- **Single click** → sends MIDI to audition the preset
- **Double-click** → edits name and AMP/CAB flags
- **Search** → filters by name or bank/slot number
- **Drag** to grid → assigns the preset
- **Chevron toggle** (▶/◀) on the panel border → minimizes/expands the library

## Technical Architecture

### Files

```
tonexpedal/
├── index.html                     # Core SPA (auto-detects Web MIDI / Web Serial vs. WebSocket Bridge)
├── favicon.svg                    # SVG icon
├── package.json                   # Test script & project metadata
├── README.md                      # English documentation
├── README_fr.md                   # French documentation
├── docs/
│   ├── index.html                 # Documentation site (FR/EN)
│   ├── ESP32_WIRELESS_BRIDGE.md   # ESP32 Bridge technical guide (EN)
│   └── ESP32_WIRELESS_BRIDGE_fr.md# ESP32 Bridge technical guide (FR)
├── captures/
│   └── tnx1.png                   # Interface screenshot
├── firmware/                      # Standalone ESP32-S3 PlatformIO firmware
│   ├── platformio.ini             # Build config for ESP32-S3 & native tests
│   ├── partitions_16MB.csv        # LittleFS flash partition map
│   ├── include/                   # C++ headers (HDLC, USB Host, WebSocket bridge)
│   ├── src/                       # C++ source files
│   ├── test/                      # C++ Unity unit tests
│   └── data/                      # LittleFS web assets
└── tests/                         # JavaScript / Protocol unit tests
    ├── protocol.test.js           # CRC-CCITT, byte-stuffing, binary float tests
    ├── midi.test.js               # Bank/PC math verification tests
    └── import_export.test.js      # JSON schema validation tests
```

### Development & Testing

Run the JavaScript protocol and math test suite:
```bash
npm test
```

To exercise the wireless application before ESP32 hardware is available, start the
desktop bridge simulator and open the printed localhost URL:

```bash
npm install
npm run simulator
```

Add `-- --setup` to serve the first-boot Wi-Fi provisioning page and setup API:

```bash
npm run simulator -- --setup
```

The simulator serves the production frontend and models pedal connection changes,
private command acknowledgements, shared preset confirmations, sync ownership, and
missing or failed confirmations. Its deterministic control endpoint accepts:

```bash
curl -X POST http://127.0.0.1:8787/api/simulator \
  -H 'Content-Type: application/json' \
  -d '{"action":"pedal_disconnect"}'

curl -X POST http://127.0.0.1:8787/api/simulator \
  -H 'Content-Type: application/json' \
  -d '{"action":"set_confirmation_mode","mode":"drop"}'
```

Supported actions are `pedal_connect`, `pedal_disconnect`, `set_confirmation_mode`
(`normal`, `drop`, or `error`), `close_clients`, and `reset`.

Run native C++ unit tests (PlatformIO):
```bash
python3 -m pip install -r requirements-dev.txt
cd firmware && pio test -e native
```

The ESP32 target is pinned to PlatformIO Espressif32 7.0.1 with an explicit N16R8 configuration (16 MB flash and 8 MB octal PSRAM).

For direct-USB hardware validation and opt-in diagnostic capture instructions, see the [TONEX Hardware Test Checklist](docs/TONEX_HARDWARE_TEST_CHECKLIST.md).

With a pedal attached directly on macOS or Linux, run the read-only command-line protocol probe:

```bash
npm run hardware:probe
```

It checks Hello, State, and the four preset-request boundaries without exposing preset names. Add `-- --all` to validate all 150 preset responses.

For a repeated full-library soak test that closes and reopens the serial port each cycle:

```bash
npm run hardware:probe -- --all --repeat 10
```

On macOS, once the current preset index is known, validate rapid MIDI changes and their CDC confirmations while restoring that preset afterward:

```bash
npm run hardware:midi-stress -- --restore-index 0
```

The default sends 150 changes at 5 Hz over 30 seconds and fails on missing, excess, reordered, malformed, or CRC-invalid events. It also requires CDC to confirm the final restored index. This command changes the audible preset, so reduce monitor volume before running it.

To capture timestamped CDC fingerprints while testing one physical pedal action, run:

```bash
npm run hardware:control-capture -- --label footswitch_b --seconds 20
```

The recorder prints events live and limits long messages to structural metadata, a hash fingerprint, and a short prefix. It does not expose preset names or parameter data.

To poll the short State response and optionally fingerprint a known active preset while testing bypass:

```bash
npm run hardware:state-watch -- --seconds 30 --preset-index 0
```

The pedal accepts preset bypass control through MIDI CC12 (`0` = bypassed, `127` = active), which can be exercised with a restore-protected probe:

```bash
npm run hardware:midi-cc-probe -- --controller 12 --values 0,127 --restore-value 127
```

### MIDI Protocol

The TONEX Pedal uses 50 banks × 3 slots (A/B/C) = 150 presets.

| Preset # | Bank Select (CC#0) | Program Change |
|----------|-------------------|----------------|
| 0–127    | CC#0 = 0          | PC = preset#   |
| 128–149  | CC#0 = 1          | PC = preset# − 128 |

```
Bank Select:    [0xB0 + channel, 0x00, value]
Program Change: [0xC0 + channel, PC]
```

### USB CDC Serial Protocol (HDLC)

The pedal exposes two USB interfaces:
- **USB-MIDI** — for Bank Select / Program Change
- **USB CDC** — for serial communication (reading presets, parameters)

#### HDLC Frame

```
[0x7E] [payload stuffed] [CRC_lo stuffed] [CRC_hi stuffed] [0x7E]
```

- **Delimiter**: `0x7E`
- **Byte stuffing**: `0x7E` → `0x7D 0x5E`, `0x7D` → `0x7D 0x5D`
- **CRC-CCITT**: polynomial `0x8408`, init `0xFFFF`, inverted result (`~crc & 0xFFFF`)

#### Commands

| Command | Payload | Description |
|---------|---------|-------------|
| Hello | `b9 03 00 82 04 00 80 10 01 b9 02 02 10` | Connection init |
| Request State | `b9 03 00 82 06 00 80 10 03 b9 02 81 01 02 10` | Request current state |
| Request Preset (0–127) | `b9 03 81 00 02 82 06 00 80 10 03 b9 04 10 01 [index] 00` | Request preset (17 bytes) |
| Request Preset (128+) | `b9 03 81 00 02 82 06 00 80 10 03 b9 04 10 01 80 [index] 00` | Request preset (18 bytes, escape `0x80`) |

#### Preset Response — Structure

```
[header] [B9 04 B9 02 BC 21] [name 33 bytes] [parameters...]
                                          ↑ NAME_MARKER
```

The parameters section starts with marker `BA 03 BA 29` (`PARAM_MARKER`), followed by encoded floats `0x88` + 4 bytes (little-endian):

| Parameter index | Byte offset (×5) | Description |
|----------------|-------------------|-------------|
| 17 | 85 | **AMP Enable** — 0.0 = off, >0.5 = on |
| 23 | 115 | **CAB Type** — 0.0 = Tone Model, 1.0 = VIR, 2.0 = disabled |

### Device ID

- **TONEX Pedal (full size)**: `0x10`
- TONEX One: `0x0B` (not supported)

### Transport Abstraction (Android Support)

The app uses a transport abstraction layer to support both **Web Serial** (desktop) and **WebUSB** (Android):

```
transportSend(frame)      → Serial.write() or USB.bulkTransferOut()
transportStartRead()      → Serial reader loop or USB.bulkTransferIn() loop
transportIsOpen()         → serialPort.opened or usbDevice.opened
transportDisconnect()     → serialPort.close() or usbDevice.close()
```

**Connection flow:**
1. Try **Web Serial** first (desktop Chrome/Edge)
2. If unavailable or fails, fallback to **WebUSB** (Android Chrome)
3. WebUSB shows the device picker filtered by VID `0x1963` (IK Multimedia)

**WebUSB CDC setup:**
- Find CDC Communication interface (class `0x02`) → control transfers (SET_LINE_CODING, SET_CONTROL_LINE_STATE)
- Find CDC Data interface (class `0x0A`) → bulk endpoints for HDLC data
- If class `0x0A` not found, fallback to any interface with bulk endpoints

### Persistence

Everything is saved in `localStorage` under the key `tonex-state`:

```json
{
  "buttons": {
    "0": { "bank": 0, "slot": "A" },
    "4": { "bank": 1, "slot": "B" }
  },
  "midi": { "device": "ToneX MIDI Out", "channel": 0 },
  "presets": {
    "0_A": { "name": "Trooper - 80s Pack", "amp": true, "cab": false },
    "0_B": { "name": "80s Lead - 80s Pack", "amp": true, "cab": true }
  }
}
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| No MIDI device | Check USB connection. Chrome → `chrome://midi-devices` |
| Web Serial unavailable | Use Chrome 89+ or Edge 89+. Check HTTPS/localhost |
| USB Sync fails | Close IK Tonex and any app using the serial port |
| Names don't appear | Re-run Sync USB. Check console (F12) for errors |
| AMP/CAB always grey | Check console for correct float32 values (log for first 3 presets) |
| Blank page after load | Reload the page, localStorage may be corrupted |
| Android: Sync doesn't read data | WebUSB fallback should auto-activate. Check console for interface/endpoint logs |

## Credits

- USB CDC protocol: reverse-engineered from [Builty/TonexOneController](https://github.com/Builty/TonexOneController)
- Protocol documentation: [vit3k/tonex_controller](https://github.com/vit3k/tonex_controller)
- Interface: IK Multimedia TONEX Pedal Controller v1.1

## License

Personal project — non-commercial use.
