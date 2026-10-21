# reTerminal E1001/E1002 hardware interfaces

Base: official/main `f60347aeb851a1fe9438cfc388721776aa24f275`.
References: `202004307_reTerminal_E1001_V1_2_SCH_251120.pdf` and
`202004321_reTerminal_E1002_V1_2_SCH_251120.pdf`, both revision V1.2.
Power, ESP32, button and peripheral schematic sheets govern the connections
below. The E1001 has a UC8179 black-and-white panel; the E1002 has a Spectra 6
colour panel. Both profiles enable `CONFIG_HOMEHUB_RETERMINAL_E100X_HARDWARE`.
The C source names retain their original `e1002_` prefix but are compiled for
either selected reTerminal profile.

| Hardware | Schematic connection | Command |
| --- | --- | --- |
| KEY0 / KEY1 / KEY2 | GPIO3 / 4 / 5, active low | `buttons.read`, `events.read`, `events.wait` |
| SD | SPI2 SCK7, MISO8, MOSI9, CS14; DET15 active low, EN16 active high | `sd.status`, `sd.list`, `sd.read_text`, `sd.write_text`, `sd.read`, `sd.write` |
| PCF8563 U11 | I2C SDA19/SCL20, address 0x51 | `rtc.read`, `rtc.set` |
| Buzzer BUZ1 / Q7 | GPIO45, LEDC low-speed timer0/channel0 | `buzzer.beep` |
| Battery U3 / R19 / R21 | ADC1 GPIO1, divider enable GPIO21; 10k/10k divider | `battery.read`, `device.health` |
| PDM microphone U18 | EN38, CLK42, DATA41; L/R grounded | `microphone.record` |
| SHT40 U15 | I2C SDA19/SCL20, address 0x44 | Official `sensors.read` |

The official UC8179 and Spectra 6 rendering, image sizing and temperature
calibration remain the base implementation. Only the sensor bus acquisition
is shared with RTC. No second SHT40 driver or extra temperature correction is
added. Official temperature offset defaults to -3.3 C; users may adjust the
existing `HOMEHUB_RETERMINAL_SHT4X_TEMP_OFFSET` setting after measurement.

## Invocation and results

Commands are advertised in `link.register`. Data always lives inside
`{"ok":true,"payload":{...}}`. File operations, RTC, buzzer setup and audio
capture run on one serialized hardware worker. Concurrent hardware operations
return `busy`; button/battery reads and event waits remain independent.
Async replies use the request's session generation, so stale results do not
answer requests on a different cloud session.

- `buttons.read`: reports `key0_down`, `key1_down`, `key2_down` and the three
  corresponding `*_presses` counts since boot. GPIO4/5 have no assigned actions.
  KEY0 retains the official pairing and long-press reset gestures.
- `events.read/wait`: see [event protocol](e1002-events.md). These are command
  replies, not an assumed server push protocol. A cloud listener must continue
  issuing waits to respond automatically.
- `battery.read`: cached `battery_mv` and `battery_pct`, sampled every 30 seconds.
  A reading older than 60 seconds or a failed sample is null. ADC collection
  follows Seeed's battery gauge: 10 samples spaced 25 ms, rejecting averages
  below 500 mV or a range above 400 mV at the ADC. Voltage is doubled to undo
  the divider, then mapped through Seeed's 101-point E1001/E1002 curve and
  rounded to a whole percent. The adapted table retains its MIT notice in
  `reterminal_e100x.c` alongside the E1003 table. It is not a fuel gauge or
  an assertion of charging. The same values are in `device.health`.
- `buzzer.beep`: `frequency_hz` 100..5000, `duration_ms` 20..1000. Automatically
  stops; a subsequent tone replaces the earlier tone.
- `rtc.read`: calendar fields and `valid`. `rtc.set` requires integer `year`,
  `month`, `day`, `hour`, `minute`, `second` and validates the date (2000..2099).
  No timezone is stored. Reads check the oscillator's STOP bit and resume a stopped clock, preserving
  calendar values and voltage-low flags. A failed restart returns an error.
  RTC INT only reaches a test point on these schematics; no alarm, display schedule
  or wakeup feature is claimed.
- `sd.status`: card detect, FAT mount status and capacity. The screen owns SPI2;
  SD adds its own chip select to that bus. There is no separate SPI reinitialization.
  Card removal invalidates the mounted handle before a subsequent command.
  A card absent at boot can be inserted later. Failed mounts never format.
- `sd.list`: optional relative directory `path`, at most 32 names.
- `sd.read_text` / `sd.write_text`: relative `path`; text up to 2048 bytes.
  Writing overwrites the file. Directories must already exist.
- `sd.read`: relative `path`, optional nonnegative 32-bit `offset` (default 0).
  Returns up to 1024 binary bytes as `data_base64`, plus `bytes`, `next_offset`,
  `eof`. Continue using `next_offset` until `eof` is true.
- `sd.write`: relative `path`, base64 `data_base64` decoding to 1..1024 bytes,
  optional `offset`. Offset 0 truncates/creates; positive offsets write to an
  existing file. Returns `bytes`, `next_offset`. No image interpretation occurs.
- `microphone.record`: required `duration_ms` 100..500. Returns `audio_base64`
  containing signed little-endian PCM16 at 16000 Hz, one channel, with format,
  rate, duration and byte count. Capture is bounded to keep control replies
  within memory limits. PDM startup samples are discarded; power and the I2S0
  channel are released on success or failure. U18's L/R is tied low; this
  interface captures one channel and does not claim a stereo configuration.
  Calls occur only on user request; no background recording, speech recognition
  or integration with the full Muse voice UI is installed.

No page lists, photo cache, display-on-button rules, timers, sample apps, Sticky
UI/profile changes, touch support, charger register changes or microphone auto-start are added.
These primitives can be combined by a cloud program; offline business execution
requires additional software and is not implied by event support.

## Validation boundary

Host tests cover event history and wire payloads, command validation/ownership,
async results and registration size. IDF API definitions were checked against
the local ESP-IDF v6.0.1 headers. These checks do not replace builds or
hardware validation of pin levels, SD cards, audio quality, battery calibration
or clock retention on either board. Build and flash are user-owned in this change.

The shared implementation is now named `reterminal_e100x`: simple board
peripherals and wiring are centralized, while SD, RTC, events and microphone
retain independent modules. Existing command names and payloads are unchanged.
See [the shared structure and regression checks](e1003-hardware.md#e100x-structure-and-regression-checks).
