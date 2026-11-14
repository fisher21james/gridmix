# Grid Mix Display

> **Beta: not ready for use yet.** The firmware is currently a hardware test, not the finished display.

An animated 4.3" desk display that shows what's powering your electricity grid right now. Turbines spin with the wind, the coal plant smokes when coal is burning, and a big number shows how much of your power is renewable right now. Add your own tariff and it shows your rate too.

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
| UK (Great Britain) | [Carbon Intensity API](https://carbonintensity.org.uk) by NESO and the University of Oxford (CC BY 4.0), no key | Every 30 minutes |
| France | [RTE éCO2mix](https://www.rte-france.com/en/eco2mix) via [ODRÉ](https://odre.opendatasoft.com) (Licence Ouverte 2.0), no key. Screen in French or English | Every 15 minutes |
| United States (California, Texas, Midwest/MISO, Central/SPP, New York, New England, Northwest/BPA) | Each grid operator's own live fuel mix (CAISO, ERCOT, MISO, SPP, NYISO, ISO-NE, BPA), no key | Every 5 minutes |

Supported: Australia, Great Britain, France and seven US grid regions; New Zealand is in progress. A place is only added when free data is no more than about 30 minutes behind. The renewable figure counts solar, wind, hydro, geothermal and bioenergy (biomass), as official figures do; the details screen says when biomass is included, and grids with nuclear also show a low-carbon figure.

Each display uses its owner's own free API key and talks to the source directly. There's no account or cloud service in between.

## Building from source

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Licence

MIT. See [LICENSE](LICENSE).
