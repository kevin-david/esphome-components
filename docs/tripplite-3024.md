# Tripp Lite 09AE:3024: protocol repair and validation

## Scope and evidence

This adds descriptor-based, read-only monitoring for `09ae:3024` through
`protocol: generic` (also selected by `auto`). The target is an INTERNET350U
connected to a WEMOS LOLIN S3 Mini, with ESPHome 2026.10.1, external regulated
5V and the logger on UART0. No physical UPS was available during development.
The INTERNET350U's report descriptor remains uncaptured. The tests use a
**synthetic descriptor**, not a hardware dump.

The starting archive was `esphome-tripplite-3024-patch-series.zip`, SHA-256
`03773915b9c05a935ecd07678c1d0fc44595aa23ee63f8bbca822b855cda6724`.
The code base is rwalker777/esphome-components at `4012cca`.

Independent references:

- [USB HID specification](https://www.usb.org/hid): report IDs identify reports, while usages and collections identify fields. Offsets, types, widths, ranges, physical conversion, units and exponents belong to the descriptor.
- [AVR900U NUT diagnostic capture](https://alioth-lists.debian.net/pipermail/nut-upsuser/2021-May/012403.html): a different enclosure with `09ae:3024`, exposing the paths below. This supports a hypothesis about the target; it does not identify its descriptor.
- [NUT voltage handling](https://github.com/networkupstools/nut/blob/1c4ab78c615a7b2b8231bdb023d553abd64b541d/drivers/tripplite-hid.c): `smart1500lcdt_scale` applies a factor of 100000 to live I/O voltage for `3024`.
- [NUT HID unit and usage definitions](https://github.com/networkupstools/nut/blob/1c4ab78c615a7b2b8231bdb023d553abd64b541d/drivers/libhid.c): HID voltage has base exponent 7; time uses seconds.

The AVR900U capture assigns these meanings:

| Report | Declared field |
| --- | --- |
| `01` | `UPS.Flow.ConfigVoltage` |
| `06` / `09` | Output low/high voltage transfer settings |
| `16` | Vendor startup-delay field, not power status |
| `30` / `31` | PowerSummary input nominal/live voltage |
| `32` | PowerSummary status flags |
| `33` / `34` / `35` | Capacity mode / remaining capacity / runtime |
| `38` | Warning capacity limit |
| `1B` | PowerConverter output voltage |
| `1E` | OutletSystem outlet load |

The supplied target captures (`01 78`, `06 5F 00`, `16 FF FF`, `30 78`,
`31 B7 04`) contain no independently established battery charge or power-state
flags. With the related descriptor, `5F 00` is a 95V threshold and `B7 04`
is raw 1207. The NUT live-voltage correction yields 120.7V **input**, rather
than an output measurement. The code requires the connected descriptor's
usages and unit signature before making those assignments.

## Target logs supplied after the initial review

`ups-s3-mini-logs.txt` and `ups-s3-mini-logs(1).txt` capture the original driver
on ESPHome 2026.10.1. They overlap; the longer file continues the same session
through 23:41:05. They do not show the repaired firmware running.

- At 23:39:35.432, `06 5F 00` immediately produces the logged battery alarms and 0% charge. This establishes the existing parser's producing mechanism directly.
- Discovery returns five IDs as both Input and Feature reports, counts ten successful responses, and logs `Found 10 reports, skipping extended search` at 23:39:35.321. Ten responses do not establish ten independent measurements. The repaired device path does not use this discovery shortcut.
- `0x32`, `0x34` and `0x35` are never requested. Their absence from these logs is not evidence that the UPS lacks them.
- No HID report descriptor is present. The logs establish report bytes and the old driver's interpretation, not the fields' physical meanings.
- `0x31` repeatedly returns `31 B7 04` and the old parser labels it output voltage. Its actual direction still requires descriptor evidence.
- The initial operation logs a 534 ms duration warning with VERY_VERBOSE logging active. This single observation does not establish USB timeouts, instability or the new driver's performance.
- `ups-s3-mini-install.txt` confirms a successful original firmware build/upload on ESP-IDF 5.5.6 and the integer format/unsigned-comparison warnings addressed by the compatibility commit.

A sanitized fixture, `tests/ups_hid/fixtures/internet350u-mains-reports.txt`,
contains the ten successful `(type, ID, bytes)` responses. A replay through the
production decoder, paired explicitly with a **synthetic** related-layout
descriptor, keeps power state, battery condition, charge, runtime and output
voltage unknown. It does not claim target descriptor or hardware validation.
Raw logs and network/device connection details are not included in the repository.

## Review findings and repairs

1. **Critical — report IDs still treated as semantics.** The proposed decoder
   skips descriptor discovery solely on VID:PID and assigns fields from another
   enclosure. The repair retrieves the claimed interface's descriptor and selects
   by UPS collection path and usage. No descriptor means no guessed readings.
2. **Critical — warning capacity substituted for low capacity.** The proposed
   `0x38` assignment can mark a healthy battery low. Warning and remaining-capacity
   limits are separate. Low-battery state prefers the declared status flag.
   Numeric comparison requires a real low threshold and percentage capacity mode.
3. **Critical — inconsistent or stale power state.** The proposed publication
   change leaves lambda getters based on voltage and retains old entities after
   missing reports. Binary sensors and getters now share state resolution.
   Missing numeric readings publish NaN, missing binaries invalidate their state,
   and missing text publishes `Unknown` for this descriptor path.
4. **Important — guessed writes remain reachable.** Generic battery tests and
   delay commands write guessed report IDs. They now return unsupported for
   `09ae:3024`; the new path only performs descriptor and report reads.
5. **Important — USB timeout ownership.** Existing GET_REPORT frees a submitted
   transfer and stack context when its wait expires. A late completion can access
   freed memory. Read transfers now retain their buffers, semaphore and context
   through completion. One pending read bounds retained resources. ESP-IDF IN
   buffers round to EP0 packet size while `wLength` remains the requested length.
6. **Important — build and callback failures.** ESPHome 2026.10 removed the sensor
   unit setter. Optional platform files also unconditionally include absent
   platform headers. Metadata now enters schema/code generation and platform
   sources use feature guards. Publication copies a data snapshot before invoking
   callbacks, so a lambda can read the component without taking its held mutex.
7. **Important — reconnect identity.** A device can reconnect between polls with
   the same VID:PID. Connection generations discard cached descriptors and mixed
   samples, forcing descriptor discovery for the replacement connection.

The archive installer itself rejects the current base on its whitespace-sensitive
warning-replacement anchor. Its fixture tests are not evidence of a firmware build.
These changes replace its installation approach with ordinary repository commits.

## Behavior and limits

- Scope is the generic protocol's `09ae:3024` path. Other generic devices retain
  their existing report-ID heuristics. Shared binary publication now honors a
  known protocol status before its legacy voltage fallback.
- Only descriptor-declared absolute Data/Variable fields are measurements.
  Feature reports take priority, with fallback to declared Input fields. Each
  `(type, ID)` is read at most once per polling cycle. Wire length and ID must match.
- Input and output voltage use their collection paths. The known Tripp Lite
  100000 correction applies only to live voltage with the voltage unit,
  exponent 1 and no physical-range conversion. Other voltages use descriptor
  scaling. No input reading is copied into output voltage.
- Capacity requires `CapacityMode=2` (percent). Zero load/charge are valid.
  Runtime uses seconds converted to minutes; `0xffff` runtime is withheld.
- AC-present and discharging flags determine power state. Contradictory flags,
  or AC absent without discharge evidence, leave the combined state unknown.
  Bool lambda getters return false when unknown; check `has_power_status()` first.
- The parser bounds descriptors to 4096 bytes, reports to 64 bytes, stored fields
  to 256 and nesting to 16 levels. Unsupported long items, delimiters, malformed
  descriptors and ambiguous duplicate paths fail closed or leave the field unknown.
- Unsupported metrics, including identity strings, frequency and battery voltage,
  remain unknown. This is not a complete HID Power Device driver.
- A poll's USB waits share the configured `protocol_timeout` budget. The loop
  remains synchronous. If a timed-out transfer never completes, one transfer is
  retained and further reads fail until completion or restart. This prevents
  memory corruption without pretending ESP-IDF provides transfer cancellation.
- Legacy SET_REPORT and string-descriptor implementations are unchanged. They
  are not called by this device's monitoring path; other protocols still use them.

## Reproduce local checks

From the repository root:

```sh
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python esphome==2026.10.1
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  tests/ups_hid/test_descriptor.cpp -o /tmp/ups-hid-test
/tmp/ups-hid-test
.venv/bin/python tests/ups_hid/test_sensor_config.py
.venv/bin/esphome compile tests/ups_hid/firmware.yaml
```

The C++ checks exercise the production parser, decoder, poll cache and shared
state functions: changed IDs, shifted Input offsets, Feature/Input fallback,
wrong IDs, truncation, signed fields, physical ranges, push/pop, units, zero load,
capacity-mode rejection, warning/low distinction, battery transitions, lost data,
ambiguous flags and 20,000 deterministic malformed byte sequences under ASan/UBSan.
The Python checks exercise ESPHome metadata validation and user overrides.
The firmware fixture compiles the ESP32-S3 hardware transport, all three sensor
platforms, Wi-Fi, UART0 logging and the Home Assistant API. Its access point is
for compile testing; it is not a deployment configuration.

No local check substitutes for electrical, USB timing, late callback, disconnect,
Wi-Fi or Home Assistant runtime tests on hardware. The existing broad pytest
suite references missing standalone YAML files, so it is not a delivery gate for
this repair. Dedicated CI runs the checks above.

## Use the published branch

Merge this into your existing configuration, retaining Wi-Fi, API and sensor
names. Keep native USB free for host operation and log through UART0.

```yaml
external_components:
  - source: github://kevin-david/esphome-components@fix/tripplite-3024-descriptor
    components: [ups_hid]

logger:
  hardware_uart: UART0

ups_hid:
  id: ups_monitor
  protocol: generic
  update_interval: 30s
  fallback_nominal_voltage: 120V
```

The fallback nominal setting does not fill missing measurements or power state
on the descriptor path. Keep existing sensor declarations, using `input_voltage`
for input and `output_voltage` for output. A missing output field stays unknown.

## Hardware acceptance checklist (pending)

1. Flash only after preserving the working configuration. Capture boot and at
   least two complete polling cycles through UART0 with `logger.level:
   VERY_VERBOSE`. Save every numbered `HID descriptor [offset]` line, decoded
   field line and `GET_REPORT` raw line. No USB shutdown/test commands are needed.
2. Compare the exact INTERNET350U descriptor to the declared paths, types,
   offsets, widths, ranges and units. Record firmware/device revision. If parsing
   rejects it, preserve the full descriptor instead of adding ID guesses.
3. While on mains, compare input/output voltage, charge, runtime, load and flags
   to a known-working NUT `usbhid-ups` session on this exact UPS. Report fields
   absent from the descriptor as unknown. Never run two USB hosts simultaneously.
4. Under an appropriate controlled load, observe mains → battery → mains return
   and charging. Confirm text, binary entities and lambda getters agree within
   the polling interval. A valid voltage alone must not determine status.
5. Disconnect USB after a valid reading. Confirm numeric entities become unknown,
   binaries lose state, and text shows `Unknown` at the next poll. Reconnect and
   confirm fresh descriptor discovery, including a rapid reconnect between polls.
6. Exercise failed/short reports and USB stalls in a controlled fixture if
   available. Confirm missing fields clear and late callbacks do not corrupt
   memory. Monitor free heap and task watchdog behavior over an extended run.
7. Confirm Home Assistant displays units and availability correctly, then pin
   `external_components` to the tested commit. Automatic shutdown use remains
   pending these hardware checks.

## Local verification record (2026-10-28)

- Clang C++17 with `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer: descriptor/decoder checks completed, including 20,000 deterministic malformed inputs.
- ESPHome metadata schema: both unittest methods completed, covering four default sensor types and explicit metadata overrides.
- ESPHome 2026.10.1 / ESP-IDF 5.5.6: LOLIN S3 Mini hardware transport with numeric, binary and text sensors, Wi-Fi and the Home Assistant API compiled without compiler warnings. Firmware uses about 1.12 MB flash.
- Additional local compile variants: a simulation data provider with all sensor platforms absent, and a hardware configuration with button and number platforms enabled, both compiled without compiler warnings.
- Hardware execution, late USB callbacks, actual disconnect/reconnect behavior and Home Assistant runtime remain pending. The review was performed in this session without a separate reviewer.
