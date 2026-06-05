# ESPHome component for the Seeed XIAO ePaper Display Board EE02 (13.3" Spectra 6)

An ESPHome external component that drives the **Seeed Studio XIAO ePaper Display
Board EE02** with the **13.3" Spectra 6 (E6) colour E-Ink panel** (1200×1600
native / 1600×1200 landscape, six colours). At the time of writing there is no
mainline ESPHome support for this panel, which uses a **dual-controller**
architecture that the standard `waveshare_epaper` / `epaper_spi` components
cannot drive.

This component implements the full ESPHome `DisplayBuffer` drawing API
(`it.print`, `it.image`, `it.filled_rectangle`, …), so you can render text,
shapes, and images natively from ESPHome lambdas — and use it as a normal
ESPHome `display:` in automations.

> Status: working but young. Built by reverse-engineering the panel's init
> sequence (with thanks to the references below). Tested on the EE02 + 13.3"
> Spectra 6 kit. Issues and PRs welcome.

## Why this is needed

The 13.3" Spectra 6 panel (Good Display **GDEP133C02** / module marking
**T133A01**) is internally **two side-by-side controllers**. A full image must be
clocked into a *master* half and a *slave* half over two separate chip-select
lines. Single-CS ESPHome drivers send data down one CS only: the panel ACKs the
SPI and completes a refresh, but nothing usable lands on the glass. This
component handles the dual-CS split transfer and the panel's specific init
sequence.

## Hardware facts (verified)

These were determined empirically against real hardware — recorded here because
they are not documented in one place anywhere else.

| Item | Value |
|------|-------|
| ESPHome board | `seeed_xiao_esp32s3` |
| Framework | `esp-idf` |
| PSRAM | required — `octal`, 80 MHz (≈960 KB framebuffer) |
| Native orientation | **portrait 1200 × 1600** (use `rotation:` for landscape) |
| Row layout | 600 bytes/row, two 300-byte halves: `[master 300][slave 300]` |
| Pixel packing | 4 bits/pixel, two pixels per byte |
| SPI clock | 2 MHz (higher rates were unreliable) |
| BUSY polarity | **LOW = busy, HIGH = idle** |

### Pin map (Seeed EE02)

From the EE02 schematic and the `Seeed_GFX` board definition:

| Signal | GPIO |
|--------|------|
| SPI CLK | 7 |
| SPI MOSI | 9 |
| CS master | 44 |
| CS slave | 41 |
| DC | 10 |
| BUSY | 4 |
| RESET | 38 |
| Power enable | 43 |
| User button 1 / 2 / 3 | 2 / 3 / 5 |

### Colour codes (Spectra 6, verified by full-screen sweep)

| Colour | Nibble code |
|--------|-------------|
| Black | 0x0 |
| White | 0x1 |
| Yellow | 0x2 |
| Red | 0x3 |
| Blue | 0x5 |
| Green | 0x6 |

(Codes 0x4 and 0x7 are not primaries; they render as muddy dark tones.)

## Installation

Add as an external component, pinned to a commit for reproducibility:

```yaml
external_components:
  - source: github://rkaramandi/esphome-seeed-ee02@COMMIT_SHA
    components: [ ee02_epaper ]
```

Or vendor it locally by copying `components/ee02_epaper/` next to your YAML and
using `source: { type: local, path: components }`.

## Minimal configuration

```yaml
esp32:
  board: seeed_xiao_esp32s3
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

spi:
  clk_pin: GPIO7
  mosi_pin: GPIO9

font:
  - file: "gfonts://Inter@700"
    id: my_font
    size: 72

display:
  - platform: ee02_epaper
    id: epd
    cs_master_pin: GPIO44
    cs_slave_pin: GPIO41
    dc_pin: GPIO10
    busy_pin: GPIO4
    reset_pin: GPIO38
    power_pin: GPIO43
    rotation: 90          # native is portrait; 90 => landscape 1600x1200
    update_interval: never
    lambda: |-
      it.fill(Color(255, 255, 255));   // IMPORTANT: buffer is NOT white by default
      it.print(80, 80, id(my_font), Color(0,0,0), "Hello Spectra 6!");
```

Trigger a redraw with `component.update: epd` (e.g. from a button or automation).

## Gotchas (learned the hard way)

- **Always `it.fill()` first.** On the `update()`/lambda path the framebuffer is
  cleared to the display default (black, code 0), *not* white. If you skip the
  fill you get black background with your drawn elements on top.
- **PSRAM is mandatory.** The ≈960 KB framebuffer won't allocate without it; you'll
  see an allocation error in the log.
- **Refresh is slow.** A full Spectra 6 refresh takes ~25–35 s and audibly
  buzzes during the boost/init phase — this is normal; the buzz stops when init
  completes. Use long `deep_sleep` intervals for battery photo-frame use.
- **Clean builds when editing the component.** ESPHome caches object files; after
  changing the C++ run a clean build or your edits won't take. Look for
  `Compiling .../ee02_epaper.cpp.o` in the log to confirm a recompile.
- **No MISO needed** for the panel; it's write-only half-duplex.

## Optional diagnostics

The component exposes two helpers handy for bring-up on other panel batches:

- `id(epd).show_nibble_test();` — paints raw codes 0–7 as strips.
- `id(epd).show_next_solid();` — fills the whole screen with the next raw code
  each call (logs the code at WARN level). Use this to verify the colour map on a
  new panel; read each full-screen colour and adjust the `E6Color` enum in
  `ee02_epaper.h` if your batch differs.

## Configuration variables

- **cs_master_pin** / **cs_slave_pin** (*Required*): the two controller chip-selects.
- **dc_pin**, **busy_pin**, **reset_pin**, **power_pin** (*Required*).
- All standard `display` options (`rotation`, `update_interval`, `lambda`, `pages`).
- Standard `spi` options on the `spi:` block.

## References & credits

- Seeed Studio XIAO ePaper Display Board EE02 wiki and schematic.
- `Seeed_GFX` library board definitions (pin map).
- [`amir-hadi/esphome-waveshare-13.3-epd`](https://github.com/amir-hadi/esphome-waveshare-13.3-epd)
  — the init sequence, command constants, and dual-half transfer approach are
  derived from this photo-frame component; this project adds the full ESPHome
  `DisplayBuffer` drawing API on top.

## License

MIT — see [LICENSE](LICENSE).
