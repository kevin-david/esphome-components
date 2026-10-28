# Tripp Lite 09AE:3024

Descriptor-based monitoring and limited controls for the INTERNET350U on
ESP32-S3, validated with ESPHome 2026.10.1 and a LOLIN S3 Mini. Other models
sharing this USB ID may advertise different fields; report IDs are never used
as universal measurement meanings.

Related prior work is Rohan Kapoor's
[Tripp Lite Smart LCD HID protocol (upstream PR #36)](https://github.com/bullshit/esphome-components/pull/36)
([earlier fork PR #1](https://github.com/rwalker777/esphome-components/pull/1)).
This implementation follows the dedicated-protocol approach while resolving
the `3024` device's fields from its descriptor rather than adopting that PR's
fixed SMART1500LCD report map. Scaling and control references are linked below.

## Setup

Keep board, Wi-Fi, API/OTA, captive portal and LED settings in your device YAML.
Import the complete UPS package:

```yaml
substitutions:
  ups_name: INTERNET350U
packages:
  tripplite:
    url: https://github.com/kevin-david/esphome-components
    ref: main
    files: [configs/tripplite.yaml]
    refresh: 0s
```

See the [LOLIN S3 Mini example](../configs/examples/tripplite-s3-mini.yaml).
It uses `wifi_ssid`, `wifi_password` and `api_encryption_key` secrets; OTA
inherits the API encryption key. Refresh sources, rebuild and install using your
existing ESPHome workflow.

The package supplies the external component, `ups_monitor` hub, UART0 logger,
core measurements, diagnostics and controls. Native USB remains available for
host operation. Import the package once and remove duplicate UPS declarations;
each sensor type supports one entity per hub. Entity names use `ups_name`
(default `UPS`); changing it may rename Home Assistant entities.

Package defaults are 5-second polling, INFO logging and vendor dumps disabled.
Override `ups_update_interval` and `ups_diagnostic_dump` in device substitutions.
Device `logger:` and `ups_hid:` settings also override package defaults.
The component's standalone polling default is 30 seconds.

For an existing manually configured hub, import
[tripplite_extras.yaml](../configs/tripplite_extras.yaml) for controls and
read-only diagnostics, or
[tripplite_diagnostics.yaml](../configs/tripplite_diagnostics.yaml) for diagnostics
alone, using the same remote-package syntax. Both expect hub ID `ups_monitor`.
Copying their platform entries into existing YAML sections also works.

## Measurements and status

The complete package includes battery charge/runtime, load percentage, separate
input/output voltage, output watts, measured/nominal battery voltage, AC status,
battery state, and 27 read-only diagnostics:

- Input/output frequency and AC output current.
- Rated input/output voltage, output frequency and apparent power in VA.
- Low/high transfer voltage and battery warning thresholds.
- Shutdown/startup/reboot countdowns and watchdog remaining time.
- Voltage, overload, temperature, converter/internal-failure and shutdown flags;
  buck/boost, awaiting-power, discharging, full/empty and replacement flags.

Output watts come from Output.ActivePower, independently of load percentage and
rated VA. AC output current is not battery current; no DC battery-current field
is declared in the captured descriptor. Unsupported or ambiguous fields remain
unknown. A declared field does not establish the corresponding hardware feature
exists (for example buck/boost).

Percentage capacity requires percentage CapacityMode. Warning and low thresholds
are distinct. Countdown `-1` means a reported idle timer; unavailable means no
usable reading. Unsigned 16-bit countdowns are preserved. Vendor startup delay
uses the descriptor's physical range to convert minutes to seconds. Watchdog
`0` means disabled. These entities never write thresholds, timers or watchdogs.

Online/On Battery follow AC availability. A self-test can report AC present and
Discharging together. Overload uses the device flag. Fault includes summary
internal failure/shutdown imminent and converter failure/overload/overtemperature.
Missing readings clear numeric/binary state; text becomes Unknown. Bool lambda
getters return false when unknown, so check `has_power_status()` first.

The decoder uses descriptor paths, types, lengths, bit offsets, signedness,
units and physical ranges. NUT's known 3024 voltage/frequency/current scaling
corrections apply only to matching unit/exponent signatures without an explicit
physical range; correctly scaled descriptors and nominal values are unaffected.
See [NUT's Tripp Lite implementation](https://github.com/networkupstools/nut/blob/master/drivers/tripplite-hid.c)
and the [HID Power Device specification](https://www.usb.org/sites/default/files/pdcv10.pdf).

`auto` selects this protocol only for `09ae:3024`; `generic` also routes that exact
ID to the descriptor decoder. Forced `tripplite` rejects other products. Other
USB IDs retain their existing protocol selection.

## Controls and confirmed beeper behavior

The extras package supplies a Beeper Mode dropdown (Disabled / Enabled / Muted),
quick-test/cancel buttons and text for mode, test result and raw firmware/protocol
values. The dropdown follows cached device readback every second without extra
USB requests. It sends no command at startup and never publishes a requested
mode optimistically. Unknown readback retains the last dropdown option; the
separate status text shows Unknown. Raw version encoding is unverified.

Commands require an unambiguous, isolated writable byte Feature, successful
pre-read and the same USB connection as its descriptor. Shared, packed and
read-only reports are rejected. A timeout is an unknown command outcome and is
not retried. Successful writes schedule a readback after 250 ms.

Beeper Enable/Disable/Mute and quick self-test have been exercised on hardware.
Test cancellation still needs hardware validation. Deep tests, shutdown/reboot,
delay/watchdog writes and arbitrary vendor writes are unsupported.

The captured beeper field is `UPS.PowerSummary.AudibleAlarmControl`, Feature ID
`0x11`, an unscaled 8-bit value in a two-byte report. Disable, Enable and Mute
submit `11 01`, `11 02` and `11 03`, matching NUT and the HID specification.

**On the tested INTERNET350U, Muted persists across AC restoration and suppresses
the next ordinary battery alarm until explicitly Enabled.** A captured sequence
contains only Disable, Enable, Mute and Enable writes. All 56 polls decode;
mode `3` persists through two sustained battery episodes separated by AC return.
The user confirmed that Mute stopped the first alarm, the second outage stayed
silent, and Enable restored beeping about five seconds later.

This differs from HID's generic temporary-mute expectation. The driver reports
the UPS's native state and does not automatically reset it on AC restoration.
Muted and Disabled are kept distinct: their behavior for critical alarms or a
complete UPS power cycle has not been established. Do not extrapolate this
behavior to other models or revisions.

## Diagnostics and implementation limits

For a focused capture, override the device logger and enable vendor reads:

```yaml
substitutions:
  ups_diagnostic_dump: "true"
logger:
  level: VERY_VERBOSE
  initial_level: DEBUG
  logs:
    ups_hid.tripplite: VERY_VERBOSE
    ups_hid.esp32_usb: VERY_VERBOSE
```

Start logging before USB connection or restart to capture the descriptor and
one-time vendor dump. The dump reads each advertised vendor Feature once per
connection, labeling unknown payloads by usage/ID. It has a two-second total
budget, 500 ms per request, within the poll timeout. USB logs include outgoing
control payloads and fresh responses. Restore normal logging after the capture;
the dump and remaining vendor meanings are not hardware-validated.

USB report transfers own their resources until completion, including after a
caller timeout. One permanently incomplete transfer blocks subsequent requests
until completion or restart. Polling is synchronous and bounded by
`protocol_timeout`. USB reconnection discards the descriptor and any mixed sample.
Parser bounds are 4096 descriptor bytes, 64 report bytes, 256 fields and 16 nesting
levels. The legacy string-descriptor transfer path is unchanged and unused by
this protocol.

## Development checks

Sanitized fixtures retain the captured 885-byte descriptor and self-test
reports without network identifiers. Native tests cover parser bounds,
malformed input, changed report IDs, scaling, unavailable states, control
validation and late USB completion. Firmware compilation covers the ESP32-S3
integration; it does not replace physical validation of a new control or field.

From the repository root with ESPHome 2026.10.1 installed:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  tests/ups_hid/test_descriptor.cpp -o /tmp/ups-test
/tmp/ups-test
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itests/ups_hid/stubs tests/ups_hid/test_control_transfer.cpp -o /tmp/ups-control-test
/tmp/ups-control-test
python tests/ups_hid/test_sensor_config.py
esphome compile tests/ups_hid/firmware.yaml
```
