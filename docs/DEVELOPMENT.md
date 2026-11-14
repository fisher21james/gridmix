# Development notes

Notes for working on the firmware. For what the project is, see the main [README](../README.md).

## Repo layout

| Path | What it is |
| --- | --- |
| `platformio.ini` | Build settings for the board (PSRAM, flash size, libraries) |
| `include/board_config.h` | Pin map. Check against your board before first flash. |
| `include/lv_conf.h` | LVGL 9 settings |
| `src/main.cpp` | Firmware: display, touch, LVGL, Wi-Fi, builds the screen from live data (or sample scenes until a key is set) |
| `src/fetcher.*` | Background task that fetches the mix (and the 24 h history once after start-up), on the schedule in `lib/gridcore/schedule.*` |
| `src/data_cache.*` | Last good data and the 24 h history kept in flash, shown straight away after a restart |
| `src/health.*` | Wi-Fi recovery, 60 s watchdog, fetcher heartbeat, low-memory restart, "why it last restarted" |
| `src/ota.*` | Over-the-air updates from `firmware/ota.json` on the Pages site (SHA-256 checked) |
| `src/setup_server.*`, `src/settings_store.*` | The phone setup page at `gridmix.local`, and saving settings to flash |
| `src/improv_serial.*` | Receives Wi-Fi details from the browser installer (Improv protocol) |
| `lib/gridcore/` | Data translators (Open Electricity, EIA → `GridSnapshot`), tariff engine, settings, refresh timing. No Arduino code, so it's tested on a PC. |
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
- [x] WA: `/v4/data/network/WEM` works at 5-minute detail in Perth time (fixed 13 Nov), includes rooftop solar.
- [ ] Leave it running a few days; check `/api/status` → `device.heap_min` and `last_restart`.
- [ ] UK: pick London on the setup page; the key card should disappear and data arrive within 15 minutes.
- [ ] Try an over-the-air update from the setup page (push any change, wait for the build, press Install).
- [ ] Pull the power, plug back in: the last data should show straight away with an amber dot.

## Data notes

Open Electricity, two requests per refresh (each costs 1 credit, including `/v4/me`):

1. Generation: `/v4/data/network/NEM?metrics=power&interval=5m&network_region=SA1&secondary_grouping=fueltech&date_start=…`
2. Rooftop solar estimate: `/v4/market/network/NEM?metrics=solar_rooftop_forecast&interval=5m&network_region=SA1&date_start=…&date_end=…`

- **Always send `date_start`.** Without it the API returns half the maximum range (4 days of 5-minute data, about 300 KB).
- **Dates are NEM time without a timezone:** always UTC+10, no daylight saving, regardless of where the display is. Convert from UTC on the board; don't use local time.
- **Rooftop solar:** 5-minute generation data has no rooftop series at all, so rooftop comes from AEMO's `solar_rooftop_forecast` (30-minute values) for the same interval, and the snapshot is marked estimated. In SA on 12 Nov this moved renewables from 56% to 78%.
- **Credits:** Community plan, 500 a day (and about 10,000 a month), reset at midnight UTC (10:30 am Adelaide), confirmed 13 Nov 2026 (494 left the day before, 499 just after the reset). 1 credit per request, including `/v4/me`.
- **Refresh timing** (`lib/gridcore/schedule.*`, tested): mix 90 s after every 10 minutes, rooftop solar estimate every 30 minutes, about 192 requests a day. Under 40 credits left it slows to 30/60 minutes. Failures back off 1, 2, 5, 10, 20 minutes. A rejected key waits 6 hours (or until settings are saved); running out waits for the midnight-UTC reset.
- **Whole-country view** for Australia is the NEM (every state but WA and the NT); WA is a separate network (`WEM`); its 5-minute data already includes rooftop solar.
- Licence: Community data is non-commercial only.

EIA (US support paused, 13 Nov 2026):

- **Paused for launch.** The fuel mix was 16–19 hours behind for every region checked (TEX, ERCO, CISO, PJM, US48), while demand was about 1.5 hours behind. The code stays (translator, URLs, tests) but the setup page, install page and simulator no longer offer the US. Next option to look at: Grid Status's hosted API (real-time ISO fuel mix; free tier reportedly 500,000 rows a month; check terms and per-user keys).

- Can lag several hours to a day. Negative battery values mean charging.
- Live check 13 Nov 2026 (browser sim): region codes TEX and US48 work; data was 18–19 hours behind; the US48 answer took about 12 s, so the HTTPS timeout is 30 s. US48 includes newer codes (WNB wind+battery, SNB solar+battery, OES/UES storage, PS pumped storage), all mapped.
- Open Electricity sends no CORS headers, so a web page can't call it directly (the browser sim uses a copy-paste command instead). The display isn't affected.
- Checked every 15 minutes (newest 30 rows, sorted newest first). The key goes on the end of the URL, so the serial log cuts it off.

## Great Britain: Carbon Intensity API (13 Nov 2026)

- `api.carbonintensity.org.uk`, run by NESO with the University of Oxford. CC BY 4.0, no key, allows web pages (CORS).
- Current mix: `/regional/regionid/{id}` (ids 1-14 are DNO areas, 15-17 England/Scotland/Wales, 18 all of GB).
  History: `/regional/intensity/{to}/pt24h/regionid/{id}` gives the 48 half hours before `{to}` (UTC).
- Shares (%) per fuel, not MW: biomass, coal, imports, gas, nuclear, other, hydro, solar, wind. Solar includes
  rooftops and small farms (NESO estimates). Snapshot time = the half hour's end; it's normally 0-30 min old.
- Polled every 15 minutes (5 past each quarter): 96 small requests a day.
- Biomass counts as renewable (as official AU and UK figures do); the detail screen says "Renewable % includes
  biomass" when it's in the mix. Imports have their own colour and don't count as renewable.
- Northern Ireland isn't covered (separate grid). Rates are in pence; time zone `GMT0BST,M3.5.0/1,M10.5.0`.
- Live check 13 Nov 2026: London 42-45% renewable early morning (wind ~37%, imports ~30%).

## France: RTE éCO2mix on ODRÉ (13 Nov 2026)

- Rule for new countries: free data no more than ~30 minutes behind, with a real fuel breakdown.
  Checked 13 Nov 2026: Energy-Charts / ENTSO-E (all of Europe) 1h40-3h behind; Belgium (Elia) ~50 min;
  France's regions ~45 min; Denmark (Energinet, 5 min) and Finland (Fingrid, 3 min, key) lump power
  stations together, so their renewable % would be a guess. Only France nationally passed.
- `odre.opendatasoft.com/api/explore/v2.1/catalog/datasets/eco2mix-national-tr/records`, no key, CORS allowed,
  Licence Ouverte / Etalab 2.0. MW per fuel every 15 minutes; the newest quarter hour appears ~15-20 min after
  it starts (the row exists earlier with nulls, hence `where=nucleaire is not null`).
- Now: `select=<our columns>&order_by=date_heure desc&limit=1` (~300 bytes). History: same columns,
  `date_heure>=date'<24 h ago>Z'`, oldest first, `limit=100` (96 rows, ~27 KB); the parser keeps :00 and :30.
- Mapping: nucleaire, eolien, solaire (includes rooftop), hydraulique minus hydraulique_step_turbinage (pumped
  storage, counted as Other), gaz, charbon, fioul (Other), bioenergies (Biomass), destockage_batterie (Battery),
  stockage_batterie (charging), ech_physiques > 0 (Imports). A few columns come back as strings.
- Polled every 5 minutes (288 small requests a day). Freshness dot: up to 45 min OK, 2 h amber.
- Rates in euro cents ("27,0c"); time zone `CET-1CEST,M3.5.0,M10.5.0/3`; one region, so no start-up view choice.
- France is ~70% nuclear. The big number stays renewable (the figure official sources quote), and grids with
  nuclear also show "97% low-carbon" under it (renewables + nuclear; imports count as neither). The pill treats a
  grid that's 70%+ low-carbon like a renewable one ("Mostly low-carbon, but your rate is high"), and the detail
  badge is coloured by the low-carbon share. Australia has no nuclear, so nothing changes there.

## United States: grid operators (14 Nov 2026)

- EIA's national data runs 16-19 h behind, so the US now reads each grid operator directly (`lib/gridcore/us_grid.*`,
  `SRC_US`, region = grid code). All keyless; measured 4-15 min behind on 14 Nov 2026 from GitHub's US runners:
  | Code | Operator | Request | Notes |
  |---|---|---|---|
  | CAL | CAISO | `caiso.com/outlook/current/fuelsource.csv` (12 KB, today, Pacific time) | geothermal, biomass+biogas, small+large hydro, batteries (+/-), imports |
  | TEX | ERCOT | `ercot.com/api/1/services/read/dashboards/fuel-mix.json` (~200 KB, yesterday+today) | US visitors only (403 from Australia); read one 5-min object at a time |
  | MIDW | MISO | `public-api.misoenergy.org/api/FuelMix` (1 KB) + `/api/FuelMix/Today` (210 KB) for the chart | EST all year; hydro and biomass are inside "Other" |
  | CENT | SPP | `portal.spp.org/.../generation-mix-historical?path=/SPP/GenMix2Hour_SPP.csv` (4.5 KB, 2 h, UTC) | Market + Self per fuel; waste disposal counted as biomass |
  | NY | NYISO | `mis.nyiso.com/public/csv/rtfuelmix/YYYYMMDDrtfuelmix.csv` (70 KB, today, Eastern date) | Dual Fuel shown as gas; Other Renewables as biomass |
  | NE | ISO-NE | POST `iso-ne.com/ws/wsclient` `_nstmp_requestType=fuelmix&_nstmp_fuelType=all` (1.5 KB) | the website's own feed (their documented API needs a login); wood/refuse/landfill gas as biomass |
  | NW | BPA | `transmission.bpa.gov/business/operations/wind/baltwg.txt` (80 KB, 7 days, Pacific) | VER shown as wind, Fossil/Biomass as gas |
- Not covered: PJM (hourly, needs a key), the Southeast, Florida, Carolinas, Tennessee, desert Southwest (no ISO; EIA only).
- No whole-country view (`CountryInfo.single_view`): no chevron, header tap does nothing. No key needed.
- Polled every 5 minutes (+2.5 min). Most answers hold the day, so the 24 h chart fills from the first answer;
  the Midwest asks for `/Today` once. US daylight saving is worked out in `us_local_to_utc`.
- `.github/workflows/probe-us.yml` (run by hand) pushes fresh samples of all seven to the `probe-samples` branch.

## New Zealand (in progress, 14 Nov 2026)

- Source: Electricity Authority real-time dispatch API (emi.developer.azure-api.net), free key, subscription
  approved by hand (requested 14 Nov). Subscribe to the "Wholesale market prices" product; only its Real-time
  dispatch API still runs (the three price APIs in it were retired in 2022). Data is MW per grid connection
  point every 5 minutes, so it needs a point-to-fuel table built from the Authority's generation fleet list.
- Rejected: em6 free endpoints (renewable % only, undocumented), Transpower's live page (figures embedded in
  the web page, ~80 min old when checked, best effort), em6 full API (paid).
- Done so far: `GEOTHERMAL` source (renewable, teal), a geothermal station drawn in the nuclear plant's place
  when `CountryInfo.geothermal` (NZ), country "NZ" with regions NI / SI, NZ$ in cents, key `keys.ea`,
  `SRC_EA` (the board stays on "Waiting for data" until the source is written). Not yet on the setup page.
- Cache magic is now GMC4 (one more source in the snapshot).

## Screen language (13 Nov 2026)

- `lib/gridcore/i18n.*`: English is written in the code; `T("...")` returns the translation for the chosen
  language (French so far), falling back to English. Format strings keep the same % codes.
- Setting `display.language`: "en" or "local" (the country's own language, from `CountryInfo.lang`).
  Shown on the setup page only for countries whose language isn't English.
- French also switches the clock to 24 hours ("16:05", tariff times "22h"), decimal commas and space thousands.
- Fixed labels made with `label_tr()` are re-labelled by `gridui::set_language()`; everything else is rebuilt on update.
- Fonts include accented Latin letters (French, plus Danish/Norwegian/Swedish ready) and €.

## No wholesale price (13 Nov 2026)

The display is about how renewable the grid is; your own tariff covers cost. The wholesale price, the
"Grid: Normal/Busy" status and the price line on the detail screen were removed. The advice pill now
leads with the grid, then adds your rate: "Mostly renewable" (70%+), "Mostly coal & gas" (only when coal + gas
are half the mix or more), "Few renewables" (under 30%) or "Part renewable", followed by the tariff part, e.g.
"Mostly renewable, but your rate is high" or "Mostly coal & gas · peak rate, hold off if you can".

## Updates over Wi-Fi

- Every push publishes `firmware/gridmix-app.bin` and `firmware/ota.json` (version, build = GitHub run number, size, SHA-256).
- The display checks two minutes after start-up and then daily; it only installs when you press **Install update** on the setup page.
- The download is hashed while it's written to the spare app slot; on any mismatch or interruption the old version stays.
- To release: push to `main`. Bump `VERSION` for a version people should notice; the build number alone is enough to trigger the update.

## Unattended running

- Wi-Fi: reconnect after 30 s, restart Wi-Fi after 3 min, restart the board after 20 min (only once Wi-Fi has been set up).
- Watchdog: 60 s on the screen loop. The fetcher must check in every 3 min. Internal RAM's largest free block under 20 KB for 5 min also restarts it.
- `/api/status` → `device`: uptime, last restart reason, heap now/minimum/largest block, PSRAM free, Wi-Fi reconnects. Worth watching during the first long run.
- Last data is saved to flash at most every 30 min (about 50 writes a day, well within the flash's life).

## Known unknowns

- Pin map and touch chip vary between batches; the defaults are the commonly published ones.
- Resistive ("R") and no-touch ("N") versions: set `HAS_GT911_TOUCH 0` for now.

## Before release

- Remove the beta banner at the top of `web/index.html` (marked with a comment) and the "Beta" note in the Install section.
- Turn off the FPS overlay in `include/lv_conf.h`.
