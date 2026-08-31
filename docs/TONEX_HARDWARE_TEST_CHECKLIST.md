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

## 1. Original Direct-MIDI Path

- Select the TONEX MIDI output and confirm the status turns green.
- Select `00A` and verify the pedal changes to the expected preset.
- Select `42B` and verify PC 127.
- Select `42C` and verify the Bank Select transition plus PC 0 in the next MIDI bank.
- Select `49C` and verify the final preset, PC 149.
- Confirm no `/ws` connection retries appear while hosted on the ordinary local server.

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
- A full sync reads all 150 presets or any failures are understood from the capture.
- Cancellation releases the port and a second sync can start.
- The exported diagnostic can be parsed and summarized.
- The browser does not attempt bridge reconnection on a normal development server.
