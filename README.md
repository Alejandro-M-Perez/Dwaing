# Dwaing MVP (C)

This is the current MVP for an electrical panel CAD/validation engine written in C.

It supports:
- basic vector geometry primitives (`Vec2`, distance)
- block library definitions with connector points
- panel modeling with regions, rails, and placed devices
- XML-driven CLI workflow
- validation of geometry/electrical rules (AC/DC and AC voltage level segregation)
- OpenGL viewer for vector rendering (regions, rails, devices, sample boxes)

## How The Program Works Today

The CLI reads two files:
- a **block library** (`library.xml`) with reusable blocks/connectors
- a **panel definition** (`panel.xml`) with regions, rails, and devices

Then it either:
- prints a structured summary, or
- validates the panel against rule checks

### Current validation checks

- Device references a known block.
- Device references a known region.
- Device references a known rail.
- Device position is inside its region bounds.
- Device rail belongs to the same region as the device.
- Device domain must be `AC` or `DC`.
- `DC` devices cannot include an `ac_level`.
- `AC` devices must have `ac_level` `120`, `240`, or `480`.
- Devices on the same rail must keep minimum spacing.
- Devices on the same rail cannot mix `AC` and `DC`.
- `AC` devices on the same rail cannot mix different AC levels.

## Build And Run (Make Instructions)

### Prerequisites

- GCC/Clang compatible C compiler (`cc`)
- `make`
- OpenGL development libraries
- SDL2 development libraries

### Build

```sh
make
```

This creates the executable at `bin/dwaing`.

### Build the OpenGL viewer

```sh
make viewer
```

This creates `bin/dwaing_viewer`.

### Run with sample files

```sh
./bin/dwaing validate examples/library.xml examples/panel.xml
./bin/dwaing summary examples/library.xml examples/panel.xml
```

### Use the built-in demo target

```sh
make run-example
```

This runs both `validate` and `summary` using the sample XML files.

### Launch the viewer

```sh
./bin/dwaing_viewer examples/panel.xml
```

or:

```sh
make run-viewer
```

Viewer controls:
- `+` / `-` zoom in/out
- arrow keys pan
- `r` reset view
- `q` or `ESC` quit

### Clean build artifacts

```sh
make clean
```

## CLI Commands

```sh
./bin/dwaing validate <library.xml> <panel.xml>
./bin/dwaing summary <library.xml> <panel.xml>
```

`validate` exit codes:
- `0`: valid
- `2`: invalid (rules failed)
- `1`: parse/load/usage error

## XML Shape (Current MVP Parser)

The parser is intentionally strict and minimal (dependency-free).

### `library.xml`

- `<block id="...">`
- `<connector id="..." type="..." x="..." y="..." dx="..." dy="..." />`

### `panel.xml`

- `<panel name="...">`
- `<region id="..." min_x="..." min_y="..." max_x="..." max_y="..." />`
- `<rail id="..." region="..." x1="..." y1="..." x2="..." y2="..." />`
- `<device id="..." block="..." region="..." rail="..." x="..." y="..." domain="AC|DC" ac_level="120|240|480" />`

See:
- `examples/library.xml`
- `examples/panel.xml`

## Viewer Notes

- Regions are drawn as green outlines.
- Rails are drawn as brown line segments.
- Devices are drawn as small outlined boxes:
  - red for AC
  - blue for DC
- Extra sample vector geometry boxes are always rendered near the top area, each labeled with width/height.
