# Development notes

Notes for working on the firmware. For what the project is, see the main [README](../README.md).

## Repo layout

| Path | What it is |
| --- | --- |
| `platformio.ini` | Build settings for the board (PSRAM, flash size, libraries) |
| `include/board_config.h` | Pin map. Check against your board before first flash. |
| `include/lv_conf.h` | LVGL 9 settings |
| `src/main.cpp` | Firmware: display, touch, LVGL, Wi-Fi, builds the screen from live data (or sample scenes until a key is set) |
| `src/fetcher.*` | Background task that fetches the mix, on the schedule in `lib/gridcore/schedule.*` |
| `src/setup_server.*`, `src/settings_store.*` | The phone setup page at `gridmix.local`, and saving settings to flash |
| `src/improv_serial.*` | Receives Wi-Fi details from the browser installer (Improv protocol) |
| `lib/gridcore/` | Data translators (Open Electricity, EIA → `GridSnapshot`), grid status, tariff engine, settings, refresh timing. No Arduino code, so it's tested on a PC. |
| `lib/gridui/` | The screens (LVGL). Shared by the firmware and the simulator. Artwork is drawn at start-up by a small anti-aliased rasteriser (`raster.*`), not stored as images. |
| `lib/gridui/fonts/` | Inter and Space Grotesk, converted by `scripts/make_fonts.py` (OFL licences included) |
| `sim/` | Desktop simulator: runs `lib/gridui` in memory and saves screenshots |
| `test/` | Tests: translators with real, trimmed API responses (`test/fixtures/`), and the tariff engine with a real SA plan |
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

Simulator (needs a C compiler, an LVGL 9.6 checkout and an ArduinoJson 7 checkout):

```
git clone --depth 1 --branch v9.6.0 https://github.com/lvgl/lvgl.git ../lvgl
git clone --depth 1 --branch v7.4.3 https://github.com/bblanchon/ArduinoJson.git ../ArduinoJson
LVGL_DIR=../lvgl AJSON_DIR=../ArduinoJson sim/build.sh sim-out --frames 40
python scripts/sim_gallery.py sim-out     # GIFs + index.html
```

GitHub builds this on every push and publishes it at `/sim/` on the Pages site.

Uses the community `pioarduino` platform (Arduino core 3.x), which the display library needs. Partitions are `min_spiffs.csv`: two 1.9 MB app slots so over-the-air updates work.

## First-boot checklist

- [ ] Screen lights up and plays the sample scenes. If it stays black, check the pins in `board_config.h` and the backlight pin.
- [ ] Colours match the simulator (`/sim/` on the Pages site). If red and blue are swapped, flip the colour order in the display setup.
- [ ] Tapping the scene skips to the next sample. Tapping the region name opens the picker; tapping the bar switches fat/thin. If taps land in the wrong place, change `ROTATION_NORMAL` to `ROTATION_INVERTED` in `main.cpp`.
- [ ] Note the FPS in the corner (LVGL's monitor). Turbines should look smooth.
- [ ] The installer's Wi-Fi step connects; the pill changes to "finish setup at gridmix.local".
- [ ] Setup page saves a key and region; within a minute or two the real mix replaces the samples. Serial log shows `[fetch] mix -> 200`.
- [ ] US: check the EIA region codes (TEX, CAL, ... and US48) return data.
- [ ] WA: check `/v4/data/network/WEM` works at 5-minute detail.

## Data notes

Open Electricity, two requests per refresh (each costs 1 credit, including `/v4/me`):

1. Generation: `/v4/data/network/NEM?metrics=power&interval=5m&network_region=SA1&secondary_grouping=fueltech&date_start=…`
2. Market: `/v4/market/network/NEM?metrics=price&metrics=solar_rooftop_forecast&interval=5m&network_region=SA1&date_start=…&date_end=…`

- **Always send `date_start`.** Without it the API returns half the maximum range (4 days of 5-minute data, about 300 KB).
- **Dates are NEM time without a timezone:** always UTC+10, no daylight saving, regardless of where the display is. Convert from UTC on the board; don't use local time.
- **Rooftop solar:** 5-minute generation data has no rooftop series at all, so rooftop comes from AEMO's `solar_rooftop_forecast` (30-minute values) for the same interval, and the snapshot is marked estimated. In SA on 12 Nov this moved renewables from 56% to 78%.
- **Credits:** Community plan, 500 a day (and about 10,000 a month), reset at midnight UTC (10:30 am Adelaide). 1 credit per request, including `/v4/me`.
- **Refresh timing** (`lib/gridcore/schedule.*`, tested): mix 90 s after every 10 minutes, price and rooftop forecast every 30 minutes, about 192 requests a day. Under 40 credits left it slows to 30/60 minutes. Failures back off 1, 2, 5, 10, 20 minutes. A rejected key waits 6 hours (or until settings are saved); running out waits for the midnight-UTC reset.
- **Whole-country view** for Australia is the NEM (every state but WA and the NT); WA is a separate network (`WEM`) with no price or rooftop forecast.
- Licence: Community data is non-commercial only.

EIA:

- Can lag several hours to a day. Negative battery values mean charging.
- Checked every 15 minutes (newest 30 rows, sorted newest first). The key goes on the end of the URL, so the serial log cuts it off.

## Known unknowns

- Pin map and touch chip vary between batches; the defaults are the commonly published ones.
- Resistive ("R") and no-touch ("N") versions: set `HAS_GT911_TOUCH 0` for now.

## Before release

- Remove the beta banner at the top of `web/index.html` (marked with a comment) and the "Beta" note in the Install section.
- Turn off the FPS overlay in `include/lv_conf.h`.
