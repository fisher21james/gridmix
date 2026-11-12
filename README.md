# Grid Mix Display

Live electricity mix on a Guition **JC4827W543** (ESP32-S3, 4.3" 480×272 touch): an animated scene showing what's powering your grid right now, your tariff rate, and the wholesale price.

> Status: **bring-up firmware (step 1)**. It draws a test wind turbine with an FPS counter and a slider to check touch. The real screens come next.

## Install (no software needed)

Open the flasher page in Chrome or Edge on a computer, plug in the board, click **Install**. The page is published automatically to GitHub Pages from this repo.

## Repo layout

| Path | What it is |
| --- | --- |
| `platformio.ini` | Build settings for the board (PSRAM, flash size, libraries) |
| `include/board_config.h` | Pin map. **Check against your board before first flash.** |
| `include/lv_conf.h` | LVGL 9 settings |
| `src/main.cpp` | Bring-up firmware: display, touch, LVGL, Wi-Fi via the web flasher |
| `scripts/version.py` | Puts the number from `VERSION` into the firmware |
| `VERSION` | Bump this to release a new version |
| `web/` | The browser flasher page (ESP Web Tools) and its manifest |
| `.github/workflows/build.yml` | Builds on every push to `main`, merges the image, publishes the flasher |

## One-time GitHub setup

1. Create a new GitHub repo and push this folder to the `main` branch.
2. In the repo: **Settings → Pages → Source → GitHub Actions**.
3. Push any change (or run the workflow by hand from the **Actions** tab). The flasher appears at `https://<your-username>.github.io/<repo-name>/`.

## Building locally (optional)

```
pip install platformio
pio run -e jc4827w543              # build
pio run -e jc4827w543 -t upload    # flash over USB
pio device monitor                 # serial log
```

## First-boot checklist when the board arrives

- [ ] Screen lights up and shows the turbine. If it stays black, check the pins in `board_config.h` and the backlight pin.
- [ ] Colours look right (blue hub, white blades). If red and blue are swapped, we flip the colour order in the display setup.
- [ ] Dragging the slider changes the turbine speed. If taps land in the wrong place, change `ROTATION_NORMAL` to `ROTATION_INVERTED` in `main.cpp`.
- [ ] Note the FPS in the corner at slow and full speed.
- [ ] The web flasher's Wi-Fi step connects, and the IP address shows on screen.

## Known unknowns

- Pin map and touch chip vary between batches; the defaults are the commonly published ones.
- The display library may need the newer ESP32 Arduino core (3.x). If the first build fails on the display bus class, switch the platform to the community `pioarduino` build of espressif32.
- Resistive ("R") and no-touch ("N") versions: set `HAS_GT911_TOUCH 0` for now.

## Data sources

Open Electricity (Australia, CC BY-NC 4.0), U.S. EIA Hourly Electric Grid Monitor (US), Electricity Maps (worldwide). Each user brings their own free API key.
