# Grid Mix Display

> **Beta: not ready for use yet.** The firmware is currently a hardware test, not the finished display.

An animated 4.3" desk display that shows what's powering your electricity grid right now. Turbines spin with the wind, the coal plant smokes when coal is burning, and the header shows your own power price alongside the wholesale price.

**[See the live preview and installer →](https://fisher21james.github.io/gridmix/)**

## Features (in development)

- An animated scene where each power source comes alive in proportion to its share
- Renewable percentage and a colour-matched bar of every source
- Your tariff rate right now, with time-of-use periods and a pre-peak warning
- Switch between your local grid and your whole country
- Install and update from the browser, no software needed

## Hardware

- Guition **JC4827W543** ESP32-S3 4.3" display (capacitive touch version)
- USB-C data cable and a 5 V USB power adapter
- 3D-printed stand (MakerWorld link coming)

## Data sources

| Region | Source | Updates |
| --- | --- | --- |
| Australia | [Open Electricity](https://openelectricity.org.au) (CC BY-NC 4.0) | Every 5 minutes |

Australia is supported for now. More countries are planned for future updates once there's a free, up-to-date data source for them (the US is first on the list).

Each display uses its owner's own free API key and talks to the source directly. There's no account or cloud service in between.

## Building from source

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Licence

MIT. See [LICENSE](LICENSE).
