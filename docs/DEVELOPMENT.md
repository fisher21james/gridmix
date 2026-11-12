# Development notes

Notes for working on the firmware. For what the project is, see the main [README](../README.md).

## Repo layout

| Path | What it is |
| --- | --- |
| `platformio.ini` | Build settings for the board (PSRAM, flash size, libraries) |
| `include/board_config.h` | Pin map. Check against your board before first flash. |
| `include/lv_conf.h` | LVGL 9 settings |
| `src/main.cpp` | Current firmware: display, touch, LVGL, test turbine, Wi-Fi |
| `src/improv_serial.*` | Receives Wi-Fi details from the browser installer (Improv protocol) |
| `lib/gridcore/` | Data translators: Open Electricity and EIA responses → one common `GridSnapshot` |
| `test/` | Translator tests with real, trimmed API responses in `test/fixtures/` |
| `scripts/version.py` | Puts the number from `VERSION` into the firmware |
| `VERSION` | Bump this to release a new version |
| `web/` | The install page (ESP Web Tools) and its manifest |
| `.github/workflows/build.yml` | Builds on every push to `main`, merges the image, publishes the install page |

## Building locally

```
pip install platformio
pio run -e jc4827w543              # build
pio run -e jc4827w543 -t upload    # flash over USB
pio device monitor                 # serial log
pio test -e native                 # translator tests, no board needed
```

Uses the community `pioarduino` platform (Arduino core 3.x), which the display library needs. Partitions are `min_spiffs.csv`: two 1.9 MB app slots so over-the-air updates work.

## First-boot checklist

- [ ] Screen lights up and shows the turbine. If it stays black, check the pins in `board_config.h` and the backlight pin.
- [ ] Colours look right (blue hub, white blades). If red and blue are swapped, flip the colour order in the display setup.
- [ ] Dragging the slider changes the turbine speed. If taps land in the wrong place, change `ROTATION_NORMAL` to `ROTATION_INVERTED` in `main.cpp`.
- [ ] Note the FPS in the corner at slow and full speed.
- [ ] The installer's Wi-Fi step connects, and the IP address shows on screen.

## Data notes

Open Electricity, two requests per refresh (each costs 1 credit, including `/v4/me`):

1. Generation: `/v4/data/network/NEM?metrics=power&interval=5m&network_region=SA1&secondary_grouping=fueltech&date_start=…`
2. Market: `/v4/market/network/NEM?metrics=price&metrics=solar_rooftop_forecast&interval=5m&network_region=SA1&date_start=…&date_end=…`

- **Always send `date_start`.** Without it the API returns half the maximum range (4 days of 5-minute data, about 300 KB).
- **Dates are NEM time without a timezone:** always UTC+10, no daylight saving, regardless of where the display is. Convert from UTC on the board; don't use local time.
- **Rooftop solar:** 5-minute generation data has no rooftop series at all, so rooftop comes from AEMO's `solar_rooftop_forecast` (30-minute values) for the same interval, and the snapshot is marked estimated. In SA on 12 Nov this moved renewables from 56% to 78%.
- **Credits:** Community plan, 500 shown. 1 credit per request. Still to confirm whether it resets daily or monthly.
- Licence: Community data is non-commercial only.

EIA:

- Can lag several hours to a day. Negative battery values mean charging.

## Known unknowns

- Pin map and touch chip vary between batches; the defaults are the commonly published ones.
- Resistive ("R") and no-touch ("N") versions: set `HAS_GT911_TOUCH 0` for now.

## Before release

- Remove the beta banner at the top of `web/index.html` (marked with a comment) and the "Beta" note in the Install section.
- Turn off the FPS overlay in `include/lv_conf.h`.
