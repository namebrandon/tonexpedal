# TONEX Direct-USB Hardware Test Checklist

Use this checklist to establish a known-good computer-to-TONEX baseline before testing the ESP32-S3 bridge. Diagnostic exports contain raw pedal responses and preset names; keep them local unless they are intentionally sanitized for the test suite.

## Preparation

- Power the TONEX normally and connect its USB port directly to the development computer.
- Close TONEX Librarian and other software that may claim its MIDI or serial interfaces.
- Reduce amplifier or monitor volume because MIDI validation changes presets.
- Use current Chrome or Edge and open the application through a local server:

  ```bash
  python3 -m http.server 3000
  ```

- Navigate to `http://localhost:3000/?debug=1`. Confirm the **🧪 Diagnostics** button is visible.
- Open browser developer tools and preserve the Console log.

For a non-interactive, read-only protocol check on macOS or Linux, run:

```bash
npm run hardware:probe
```

The default probe validates Hello, State, and preset boundaries 0/127/128/149 without printing preset names. Use `npm run hardware:probe -- --all` to validate all 150 preset responses, or pass `--port` when more than one USB serial device is attached.

Use `npm run hardware:probe -- --all --repeat 10` for a reconnect soak test. Every cycle opens the port, validates the full library without exposing names, closes the port, and reports timing and response-size distributions.

On macOS, capture a privacy-safe interface inventory with:

```bash
npm run hardware:inventory
```

The inventory deliberately omits the pedal serial number, location, session identifier, and raw descriptor signature. IORegistry exposes endpoint counts but not endpoint addresses or transfer types, so those remain pending until the ESP32 host descriptor log is available.

To observe the pedal's MIDI output without transmitting anything, run:

```bash
npm run hardware:midi-monitor -- --seconds 10
```

This is safe to leave running while operating the pedal controls. A received Program Change can establish the currently selected preset; no MIDI switching test should run until that preset can be identified and restored.

To monitor unsolicited CDC events after a read-only handshake, run:

```bash
npm run hardware:probe -- --listen-seconds 15
```

Short events are shown in full; longer events are limited to a 12-byte structural prefix so preset content is not exposed. Full preset events are decoded to a `preset_index` without printing their names or parameter data.

For a live, timestamped capture of one physical-control phase, run:

```bash
npm run hardware:control-capture -- --label bypass_on_off --seconds 20
```

The recorder announces `capture_ready=true`, prints each event as it arrives, and reports a privacy-safe payload fingerprint. Use separate labeled runs for preset forward/backward, A/B/C, bypass, unconfirmed bank navigation, and bank selection confirmed with A/B/C.

When a new message family needs decoding, an explicit `--raw-output` option can
write complete unsolicited frames to a local file under `diagnostics/`:

```bash
npm run hardware:control-capture -- --label global_input_delta --seconds 45 --raw-output diagnostics/global-input.json
```

This is intentionally opt-in because it can contain pedal data. Files in
`diagnostics/` are ignored by Git; inspect and sanitize any findings before retaining
them as fixtures.

To test whether a physical state appears in the known read commands, poll State and optionally fingerprint the known active preset response:

```bash
npm run hardware:state-watch -- --seconds 40 --interval 0.5 --preset-index 0
```

The [official TONEX Pedal MIDI specification](https://g1.ikmultimedia.com/html/Manuals/TONEXPedal/TONEX_Pedal_User_Manual_English.pdf) assigns preset off/on to CC12. On macOS, exercise it with an explicit restore value:

```bash
npm run hardware:midi-cc-probe -- --controller 12 --values 0,127 --restore-value 127
```

## 1. Original Direct-MIDI Path

When the current preset index is known, the macOS boundary probe can exercise all MIDI encoding boundaries and restore that preset even if a send fails:

```bash
npm run hardware:midi-probe -- --restore-index 0
```

- Select the TONEX MIDI output and confirm the status turns green.
- Select `00A` and verify the pedal changes to the expected preset.
- Select `42B` and verify PC 127.
- Select `42C` and verify the Bank Select transition plus PC 0 in the next MIDI bank.
- Select `49C` and verify the final preset, PC 149.
- Confirm no `/ws` connection retries appear while hosted on the ordinary local server.

### Rapid-change event integrity

After confirming the pedal's current index, run the macOS stress harness. The default test sends 150 deterministic, non-adjacent preset changes at 5 Hz for 30 seconds, restores the supplied index, and continuously drains CDC while MIDI is active:

```bash
npm run hardware:midi-stress -- --restore-index 0
```

The command changes the pedal preset. Keep the audio path muted or at low volume and supply the actual starting index to `--restore-index`. A passing result requires:

- Every transmitted change and the final restore to have one CDC event in exact order.
- No missing, excess, malformed, or CRC-invalid frames.
- No unrelated CDC frames during the controlled run.
- The last CDC event to confirm the requested restore index.

For a short boundary characterization near the observed CDC throughput limit, use 10 Hz with a longer drain period:

```bash
npm run hardware:midi-stress -- --restore-index 0 --rate 10 --duration 10 --drain-seconds 4
```

Treat 10 Hz as a stress condition, not as the recommended application command rate. The tool accepts 0.5–10 changes per second and always makes a best-effort restore if its MIDI sender is interrupted or fails.

## 2. Initial CDC Synchronization

- Click **Sync USB** and select the TONEX serial device.
- Confirm the Hello and State phases both receive responses.
- Allow the full synchronization to finish.
- Record the reported success count; the target is `150/150`.
- Spot-check names and AMP/CAB badges at the beginning, around presets 127/128, and at preset 149.
- Export the preset JSON as a separate functional backup.
- Click **🧪 Diagnostics** and retain the downloaded diagnostic JSON.

## 3. Cancellation and Recovery

- Start another synchronization and cancel it after several presets.
- Confirm the serial port closes and the UI returns to its idle state.
- Start synchronization again without reloading the page.
- Confirm it can reopen the port and make progress.
- Disconnect the TONEX during a later sync and record the displayed error and diagnostic capture.

## 4. Capture Review

Summarize each downloaded capture without printing its raw preset data:

```bash
npm run diagnostics:summary -- /path/to/tonex-diagnostic-....json
```

Review these fields:

- `reconstructed_hdlc_frames` versus `recorded_hdlc_frames`
- `crc_errors`
- `read_timeouts`
- `event_counts.sync_preset_result`
- Detected transport and USB VID/PID

Do not add raw captures to Git. Files named `tonex-diagnostic-*.json` and JSON files under `diagnostics/` are ignored by default. Sanitized fixtures should use explicit descriptive names under `tests/fixtures/`.

## Pass Criteria Before ESP32 Testing

- Direct MIDI switches all four boundary presets correctly.
- The 5 Hz rapid-change test reports every CDC event in exact order and confirms the restored index.
- A full sync reads all 150 presets or any failures are understood from the capture.
- Cancellation releases the port and a second sync can start.
- The exported diagnostic can be parsed and summarized.
- The browser does not attempt bridge reconnection on a normal development server.

## Known Direct-USB Baseline

On 2026-08-31, a full-size TONEX Pedal (`VID 0x1963`, `PID 0x0068`) completed 10 consecutive read-only command-line probe cycles on macOS:

- `1,500/1,500` preset responses decoded successfully across 10 serial open/close cycles.
- Every Hello, State, and preset frame passed HDLC CRC validation.
- Full-library cycle times ranged from `8.918` to `9.035` seconds.
- Presets 0–127 returned 1,189-byte payloads; presets 128–149 returned 1,190-byte payloads because of the extended request/response index encoding.
- No preset names or raw response payloads were retained.

The same pedal also completed a restore-protected CoreMIDI boundary probe from index 0 (UI label `00A`). The pedal display began with `0.AA`, where `0.` was the bank and `AA` was the beginning of the factory preset name—not a slot code:

- MIDI indices `0`, `127`, `128`, and `149` were accepted, followed by restoration to index `0`.
- The bank transition used Bank Select 0 / Program 127 for index 127 and Bank Select 1 / Program 0 for index 128.
- The pedal emitted no CoreMIDI feedback messages.
- Every MIDI change emitted a full unsolicited CDC preset event with decoded indices `0`, `127`, `128`, `149`, and restored `0`.
- The same events were emitted immediately after opening CDC without a Hello/State handshake.
- Manual pedal navigation emitted indices `1` and `0` when advancing one preset and returning to the first preset.
- Solicited and unsolicited preset payloads encode indices 0–127 at byte 12. Indices 128–149 use an `0x80` extension at byte 12 and the raw index at byte 13.
- A full `150/150` preset read completed while MIDI injected indices `0`, `127`, `128`, `149`, and restored `0`; the CDC demultiplexer dispatched all five active events without misassigning them to pending preset requests.
- A burst run repeated that boundary sequence three times during one full read; all `150/150` responses and all 15 active events arrived in order with no drops.
- The deterministic rapid-change harness delivered all `151/151` CDC events in exact order during 150 changes at 5 Hz, including the final restore to index 0. It reported no missing, excess, malformed, unrelated, or CRC-invalid frames.
- A near-throughput run delivered all `101/101` CDC events in exact order during 100 changes at 10 Hz, again with no protocol errors and with the final restore to index 0 confirmed after a four-second drain.

A Chrome direct-USB operator smoke test on the same Mac and pedal established the browser UX baseline:

- A fresh browser profile showed `Mode: direct USB`, `Pedal: connected`, and `Library: empty` without claiming an active preset.
- Direct WebMIDI selection was explicitly labeled unconfirmed instead of being presented as the active preset.
- Browser selections of UI labels `01A` and `00A` produced privacy-safe CDC active-preset events for indices 3 and 0, respectively, with zero CRC errors.
- A browser-driven sync loaded names for all `150/150` presets and advanced library health from empty through syncing to current.
- Reloading retained all names and correctly changed library health to cached because the new page session could not prove the pedal library was unchanged.
- USB removal changed the pedal status to disconnected after the macOS/Chrome device event arrived, observed at approximately one to two seconds. Reconnection was automatic, did not select an unrelated MIDI output, and retained the cached library.

A live composite-interface test then exercised CoreAudio input, USB-MIDI output, and CDC input while a guitarist played through the pedal:

- The TONEX stereo CoreAudio input streamed continuously at 44.1 kHz for 40 seconds and was discarded without being saved.
- During that stream, MIDI selected index 3 for two seconds and restored index 0.
- CDC reported both 1,189-byte active-preset events in order at approximately 6.35 and 8.38 seconds, with zero CRC errors.
- CoreAudio, MIDI, and CDC all completed without transport errors, and the guitarist reported no abnormal dropout, click, freeze, or delayed response beyond the expected preset change.
- This validates simultaneous CoreAudio input plus both control interfaces on macOS. Opening the TONEX CoreAudio output concurrently remains a separate test if USB playback or re-amping becomes an application requirement.

Physical-control captures on the same pedal established the following unsolicited CDC behavior:

- A five-second idle baseline produced no frames.
- Preset forward from index 0 emitted one active-preset event for index 1; preset backward emitted one for index 0.
- Footswitches B, C, and A emitted one active-preset event each for indices 1, 2, and 0.
- Engaging and disengaging bypass produced no unsolicited CDC frame.
- Bank up/down without choosing a slot produced no unsolicited CDC frame.
- Bank up followed by footswitch A emitted index 3; bank down followed by A emitted index 0. The observed `1.AA` and `0.AA` displays were the bank number followed by a preset name beginning with `AA`; they must not be interpreted as bank/slot notation. The bank-navigation gestures themselves emitted nothing.
- All observed preset-selection messages used the existing `04 02 b9 03 00` active-preset body prefix. No additional unsolicited control-message type was found, and every capture completed with zero CRC errors.

Focused bypass-state testing established a stricter limitation:

- Across one 40-second bypass-off/on/off cycle, all 79 State samples were the identical 10-byte payload `b9 03 81 01 02 03 10 b9 01 00` (`state_code=0`).
- Across a second cycle, all 77 State samples and all 77 full responses for active preset index 0 retained one fingerprint each. Bypass therefore does not modify the known State or preset-read responses.
- Sending the documented CC12 values 0 and 127, followed by a restore value of 127, produced no unsolicited CDC acknowledgement.
- With the pedal temporarily configured for `MIDI.THRU=MERGE`, a physical bypass-off/on cycle produced no USB-MIDI messages; the setting was then restored to `OFF`.
- The bridge can command bypass through CC12 in a future feature, but cannot confirm or track physical bypass from any currently observed USB response. Do not expose a definitive bypass status until a feedback mechanism is discovered.

Focused cab-simulation testing established a separate limitation:

- A physical CAB enabled → disabled → enabled cycle emitted no unsolicited CDC frames during a 120-second, read-only capture.
- During a second physical CAB enabled → disabled → enabled cycle, 146 State responses and 146 read-only preset-0 responses retained one fingerprint each; neither known response carries the CAB state.
- The community-proposed `CC117` Cab Sim Bypass candidate was sent directly through CoreMIDI as `127` followed by `0`, with a final restore to `0`. macOS reported successful delivery, but the full-size TONEX Pedal showed no CAB-state change during either value.
- Therefore, Cab Simulation remote disable is **not validated** for this pedal. Keep the browser CAB control unavailable and do not treat `CC117` as a supported Pedal command. Discovering an alternative requires a trace of TONEX Editor traffic while it changes the same setting, or an IK Multimedia Pedal-specific protocol reference.

Focused global-setting capture established a separate live-update message family:

- Changing and restoring the Global Settings **Input Trim** value emitted a 25-frame burst of 3,552-byte unsolicited CDC payloads, each with zero CRC errors. This is distinct from the 1,189-byte active-preset event family.
- The 25 frames had no preset-name or known preset-parameter markers. Across the ordered burst, adjacent frames differed at exactly byte offset 58.
- The observed byte at that offset stepped through `8, 16, 24, …, 112` as Input Trim changed and returned to `8` when it was restored. The mapping from this encoded value to the pedal’s displayed units remains to be decoded.
- Changing and restoring Global Settings **Main Volume** emitted eight 3,552-byte CDC frames with zero CRC errors. The isolated field at byte offsets 61–64 is little-endian IEEE-754 and followed the observed physical adjustment sequence `+1.0, +2.0, +3.0, +2.0, +1.0, 0.0, -1.0, 0.0`; the final physical setting was confirmed at `0.0`.
- The community-proposed `CC122` Global Volume candidate was then delivered through CoreMIDI as `127`, `0`, and final restore `0` while a 20-second CDC capture was active. It emitted **zero** CDC frames and did not update the identified Main Volume field. Do not expose a browser Master Volume control or treat `CC122` as a supported full-size TONEX Pedal command.
- This proves the pedal does report at least some live global-setting changes over CDC. A future bridge feature may decode this message family, but must not treat its fields as understood until more isolated setting captures establish a stable schema.

## 5. ESP32 WLAN and USB-MIDI Vertical Slice

- Configure `firmware/include/wifi_secrets.h`, then upload both firmware and LittleFS data.
- Confirm the ESP32 logs a DHCP address and that another device on the same WLAN can open it.
- Connect the TONEX to the native USB host port and retain the logged device, interface, and endpoint descriptors.
- Confirm the bridge reports the pedal disconnected before attachment and connected after enumeration.
- Look for `MIDI ready` with the claimed interface and bulk OUT endpoint.
- Look for `CDC claimed`, followed by `CDC transport ready`, and retain any control-transfer error.
- From the remotely hosted application, select `00A`, `42B`, `42C`, and `49C` and verify each change on the pedal.
- Start a bridge preset sync and confirm Hello, State, and all 150 preset responses complete without `sync_failed`.
- Spot-check names and AMP/CAB flags around presets 0, 127, 128, and 149.
- Cancel a second sync, confirm the LED and UI return to connected state, then start it again.
- Unplug and reconnect the pedal, then repeat one preset change without rebooting the ESP32.

If MIDI does not become ready, do not guess endpoint numbers in code. Preserve the descriptor log and update the interface matcher from that evidence.
