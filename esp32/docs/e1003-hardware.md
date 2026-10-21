# reTerminal E1003 Muse hardware support

The `reterminal-e1003` profile uses the existing Muse Link pairing, Wi-Fi,
Noise session, status screen, and `display.draw_url` command. On first boot it
advertises over BLE as `MuseGadget-Disp-XXXXXX`. In the Muse app, select that
device and press the green KEY0 button to confirm pairing. Holding KEY0 for
five seconds resets setup. The status screen wording is shared with E1001/E1002.
The Seeed factory firmware's Wi-Fi hotspot and SenseCraft pairing-code flow
are separate from this Muse profile.

The V1.0 schematic `202004522_reTerminal_E1003_V1.0_SCH_251231.pdf` is the
source for board wiring. The display is an ED103TC2 panel with an IT8951 TCON:
SPI2 SCK7, MISO8, MOSI9, CS10, reset12, ready13, drive power11, and TCON
power21. There is no D/C signal. The microSD slot shares SPI2; its CS14 is
held high while its active-high power GPIO39 is enabled, matching the factory
firmware and Seeed_GFX2; the deselected card cannot drive MISO during TCON
reads. Battery enable is GPIO40, not E1001's GPIO21.

The display driver uses the TCON's native 1872x1404 landscape order and a
full-screen one-bit black/white refresh. Its command protocol and bitmap
ordering follow Seeed_GFX2's MIT-licensed IT8951/ED103TC2 implementation.
Before each refresh the driver sets the IT8951 TCON temperature, following
Seeed_GFX2's `0x0040, 1, uint8_t(temp)` command. It uses the latest valid
SHT40 reading (truncated to whole degrees C), or the previously tested 16 C
value before the first measurement, after 30 seconds without a reading, or
outside -40..85 C. The boot screen can therefore use 16 C before the sensor
task starts; later refreshes use the sensor. Startup goes directly to the
ordinary Muse status screen; the temporary black/white test pattern has been
removed.
The Muse image command accepts baseline JPEG and raw RGB565 as on E1001;
JPEGs are downscaled by powers of two if necessary. Uncovered space starts
white on each new image. The E1003 extensions below cover SD, microphone,
16-level grayscale and partial updates. Online power management uses DFS,
Wi-Fi MIN_MODEM and automatic Light Sleep; Deep Sleep is not exposed.
`display.draw_file` uses the same full-screen drawing mode as
`display.draw_url`, reading a VM file over the active Noise session.
E1001/E1002 keep their existing backends and hardware interfaces.

## Input and sensors

The E1003 V1.0 schematic and Seeed's E1003 board profile place KEY0/1/2 on
GPIO3/4/5, SHT40 on I2C GPIO19/20 (address 0x44), and GT911 on that same
bus (address 0x5D, INT GPIO2, RESET GPIO48). This profile enables the shared
SHT4x reader and reports temperature/humidity through `sensors.read`.
Its initial calibration offset is 0 C, pending a reading on the actual E1003.
The display refresh uses a recent SHT40 value when available and falls back
to 16 C if reading fails. A sensor failure cannot stop the screen refreshing.

`buttons.read` gives the three current button states and press counts.
`events.read`/`events.wait` retain debounced `button.changed` events, as on
E1001/E1002. KEY0 still confirms pairing and resets setup on a long press;
KEY1/2 have no assigned display action.

`touch.read` reports GT911 availability and the latest single-contact state,
mapped to the 1872x1404 panel coordinates. `touch.changed` events contain
`pressed`, `x`, `y`, `gpio:2`, `sequence`, and `uptime_ms` on the first contact
and its release. It does not emit drag samples or redraw the e-paper. Contact
coordinates use the official firmware's default GT911 orientation. The four
corners were checked on the E1003: origin at top left, x right and y down.
Multi-touch is ignored. Events use the existing Link command/result
transport; the cloud must issue `events.wait` repeatedly to react to them.

## Battery, charger, RTC, buzzer and LED

The E1003 V1.0 schematic assigns the battery divider to ADC GPIO1 with
VBAT_EN GPIO40; this is different from E1001/E1002's GPIO21. Seeed's E1003
factory profile keeps GPIO40 high and has its own 101-point battery voltage
curve, which this profile uses for `battery_pct`. Capacity in mAh is not
estimated or assumed to equal E1001/E1002. The voltage is sampled every 30 s
and returned by `battery.read` and `device.health`; percentage is a
voltage-based estimate, not a fuel gauge. A failed or stale sample is null.

`pmic.read` reads the SY6974B at I2C address 0x6B. It reports input source,
charge phase, input present, charging/precharge or fast-charge, charge done,
and the raw status/fault/bus registers. REG09 is read twice: the first value
`fault_latched_reg` is the prior latched fault and the second `fault_reg` is
the current fault; `battery_overvoltage` decodes bit 3 of the latter. It does
not write charging current,
watchdog, input current limit, or voltage threshold; USB/charger presence is
separate from actual charging. The PCF8563 at 0x51 uses the existing
`rtc.read`/`rtc.set` implementation on the shared GPIO19/20 bus.

`buzzer.beep` drives GPIO45 for a bounded 20..1000 ms tone. `led.read` and
`led.set` expose the active-low GPIO16 user LED; LED state is caller-defined.
Neither control is tied to button/touch events or the screen. On the E1003,
`pmic.read`, `battery.read`, `rtc.set`/`rtc.read`, the green LED and the buzzer
were verified on hardware. The RTC kept valid time after a reboot and rebind.
`device.health` also reads the PMIC's current charging and input-present state
without reading (and clearing) its latched fault register; either field stays
null if the PMIC read fails. `battery_pct` remains a voltage estimate.

With ESP-IDF v6.0.1 activated, the board build command is:

```sh
cd esp32
tools/board.sh reterminal-e1003 build
```

The build needs a local Muse SDK token in its generated sdkconfig for pairing;
never commit or print it. On 2026-10-20 the E1003 paired, joined Wi-Fi,
displayed the Muse status screen and received full 1872x1404 black/white
images. Those are device observations, not just build results.

## SD, microphone and advanced display

The E1003 microSD card uses the display's SPI2 pins GPIO7/8/9, CS GPIO14,
detect GPIO15 and active-high power GPIO39. `sd.status`, `sd.list`,
`sd.read_text`, `sd.write_text`, `sd.read`, and `sd.write` reuse the bounded
FAT file API from E1001/E1002. It never formats a card. A board-level lock
keeps whole SD operations from interleaving with IT8951 command sequences.
On hardware, `sd.status` mounted a 15.9 GB card, and text and binary write/read
round trips matched. Five root `sd.list` requests timed out with the original
firmware; the root directory contained a legacy byte 0xE9 in a name,
which was invalid UTF-8 in the JSON result. After `sd.list` replaced that byte
with U+FFFD, a 23-entry reply reached the client with
`invalid_name_count: 1` and `truncated: false`. The displayed replacement is
not a usable file path.
Concurrent SD access during a display refresh and card removal during access
remain untested. The PDM microphone uses enable GPIO38, data GPIO41 and clock
GPIO42. `microphone.record` returns up to 500 ms of 16 kHz mono PCM as base64;
it powers the microphone down after recording. A 300 ms recording returned
9600 bytes, with a clear tapping signal and no clipping in the analyzed sample.

`display.draw_gray16_url` converts baseline JPEG or raw RGB565 luminance to
four-bit IT8951 data and requests the ED103TC2 gray full-refresh waveform.
`display.draw_url` keeps the already tested one-bit black/white path. A labeled
16-step gradient was checked on the E1003 panel: levels 0 through 15 appeared
in the expected black-to-white order without repeated steps or row offsets.

`display.draw_partial_url` accepts `url`, `x`, `y`, `width`, `height`. The image
must have exactly the requested dimensions and must fit the 1872x1404 screen;
`x` and `width` must be multiples of 16. The command changes only that
black/white rectangle, retaining the remainder of the current monochrome
image. The IT8951 display window expands horizontally to 32-pixel boundaries
in mirrored TCON coordinates; pixels in that margin retain their prior image.
After nine partial requests, the tenth performs a full black/white
refresh to limit ghosting. A successful full black/white refresh is required
before partial updates; a 16-gray refresh requires another full monochrome
refresh before partial updates resume. Three 320x160 updates at (160,160)
were observed on hardware, with markers outside the rectangle preserved;
the reported placement was approximate. A later 288x312 grid test at (288,312)
confirmed partial requests 1-9 and a full refresh on request 10. The partial
requests left a narrow strip at the right edge; the tenth full refresh restored
the complete square, isolating the defect to the partial path. Loading complete
rows before the partial display command did not change the strip. The TCON
window for this request starts at x=1296, which is 16 pixels off a 32-pixel
boundary; the driver now expands that display window outward to 32-pixel
boundaries before sending it. The user repeated the grid and vernier tests on
2026-10-20 and reported the missing strip removed, with placement accepted.
The serial log labels
each successful E1003 partial request 1/10 through 10/10.
The IT8951 has also logged occasional "LUT never reported
busy" warnings despite successful visible refreshes; refresh completion
timing remains to be measured before changing that wait logic.

## E100x structure and regression checks

E1001/E1002/E1003 Link hardware now has one registration/dispatch/health
interface in `main/reterminal_e100x.h` and `reterminal_e100x_commands.c`.
`reterminal_e100x.c` owns the selected board data, simple GPIO peripherals,
shared I2C initialization, E1003 SPI operation mutex and battery cache.
`reterminal_e100x_board.h` selects wiring from the existing board profile,
not from a hardware-feature switch. The old Kconfig switches retain their
meaning, including explicit `n`; this does not enable the Muse UI or change
Sticky's board configuration.

SD, PDM capture, events and PCF8563 each retain a separate E100x module.
GT911, SY6974B and the individual panel drivers remain separate. The panel
owns SPI2 initialization. E1003 SD operations take the SD lock before the
shared SPI operation lock; IT8951 takes that same operation lock before its
SPI device transactions. Initialization retains the existing SD power
transitions and the first-mount 300 ms delay. The SY6974B publishes its
handle and operation lock together, retries failed registration, and holds
that lock across each register sequence. Health never reads REG09.

From the repository root, with a host C/C++ compiler on PATH:

```sh
python -X utf8 -m unittest discover -s esp32/tests -p 'test_reterminal_e100x*.py'
python -X utf8 -m unittest discover -s esp32/tests -p 'test_link_epaper_status.py'
python -X utf8 -m unittest discover -s esp32/tests -p 'test_link_reterminal_sht4x.py'
python -X utf8 -m unittest discover -s esp32/tests -p 'test_link_image_fetch.py'
```

The command contract fixture records pre-refactor names, parameter schemas
and timeouts. Host tests compile production ADC/cache, command, RTC, PDM,
SD, event, IT8951 packing/window and display-state logic against fakes. PMIC
initialization uses real host threads; SD contention and GPIO ordering use
scripted host fakes. These are software checks, not electrical, on-device
concurrency, audio-quality or display-quality validation. Reflash and verify
E1001 mono/gray4, E1002 complete image replacement, E1003 mono/gray16/partial
and hardware commands before treating the refactor as hardware-validated.
