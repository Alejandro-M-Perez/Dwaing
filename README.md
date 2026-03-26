# Dwaing

Dwaing is a C-based industrial drawing engine with validation and an OpenGL viewer. It now supports both legacy electrical panel layouts and generalized document types for one-line diagrams, P&IDs, and general arrangements.

## Quick Start

Prerequisites:

- `cc`
- `make`
- SDL2 development libraries
- OpenGL development libraries

Build and run the regression suite:

```sh
make
make test
```

Validate the shipped examples:

```sh
./bin/dwaing validate libraries/default/library.xml examples/panel.xml
./bin/dwaing validate libraries/custom/AB_library_extended.xml examples/AB_panel.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/one_line.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/pid.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/general_arrangement.xml
```

Inspect them in the viewer:

```sh
make viewer
./bin/dwaing_viewer examples/panel.xml libraries/default/library.xml
./bin/dwaing_viewer examples/AB_panel.xml libraries/custom/AB_library_extended.xml
./bin/dwaing_viewer examples/pid.xml libraries/generic/process_library.xml
```

## Example Documents

- [examples/panel.xml](/home/loopk/Documents/Projects/Dwaing/examples/panel.xml): default panel-layout demo with nested regions, rails, wire ducts, and device placement rules.
- [examples/AB_panel.xml](/home/loopk/Documents/Projects/Dwaing/examples/AB_panel.xml): current-schema panel demo using the Allen-Bradley custom library.
- [examples/one_line.xml](/home/loopk/Documents/Projects/Dwaing/examples/one_line.xml): one-line electrical diagram using generic assets and a routed wire.
- [examples/pid.xml](/home/loopk/Documents/Projects/Dwaing/examples/pid.xml): grouped P&ID example with pipes and an assembly asset.
- [examples/general_arrangement.xml](/home/loopk/Documents/Projects/Dwaing/examples/general_arrangement.xml): general arrangement example with generic objects and links.
- [examples/README.md](/home/loopk/Documents/Projects/Dwaing/examples/README.md): quick command index for all shipped examples.

## Supported Concepts

- Legacy panel libraries with `block`, `V`, `panel_vector`, and `backplane_vector`
- Generic asset libraries with `symbol`, `assembly`, `port`, and `member`
- Panel documents with regions, rails, wire ducts, and devices
- Generic documents with objects, groups, ports, and connections
- Document kinds: `panel`, `one_line`, `pid`, and `ga`
- CLI validation and structured summaries
- SDL/OpenGL viewer for both panel and generic documents

## CLI

Build:

```sh
make
```

Viewer:

```sh
make viewer
```

Commands:

```sh
./bin/dwaing validate <library.xml[,library2.xml,...]> <document.xml>
./bin/dwaing summary <library.xml[,library2.xml,...]> <document.xml>
```

Exit codes:

- `0`: valid
- `2`: invalid
- `1`: parse/load/usage error

## Libraries

Library files live under [libraries/](/home/loopk/Documents/Projects/Dwaing/libraries).

- [libraries/default/library.xml](/home/loopk/Documents/Projects/Dwaing/libraries/default/library.xml): default panel block library
- [libraries/custom/AB_library_extended.xml](/home/loopk/Documents/Projects/Dwaing/libraries/custom/AB_library_extended.xml): Allen-Bradley custom panel library
- [libraries/generic/process_library.xml](/home/loopk/Documents/Projects/Dwaing/libraries/generic/process_library.xml): generic process and equipment library

The loader can merge multiple library files by comma-separating paths in the CLI arguments.

## Viewer

Launch examples:

```sh
./bin/dwaing_viewer examples/panel.xml libraries/default/library.xml
./bin/dwaing_viewer examples/general_arrangement.xml libraries/generic/process_library.xml
./bin/dwaing_viewer examples/pid.xml libraries/generic/process_library.xml
```

Controls:

- `+` / `-`: zoom
- Arrow keys: pan
- `r`: reset view
- `1..5`: toggle layers
- Click objects or connections to inspect them
- `q` or `Esc`: quit

Current limitation:

- Editing from the viewer is implemented for legacy panel documents. Generic document inspection is read-only.

## XML Overview

Legacy panel libraries:

- `<panel_vector ... />`
- `<backplane_vector ... />`
- `<block ...>` with nested `<V ... />` or `<connector ... />`

Generic asset libraries:

- `<symbol ...>` with nested `<port ... />`
- `<assembly ...>` with nested `<port ... />` and `<member ... />`

Generic documents:

- `<document kind="one_line|pid|ga" ...>`
- `<group ...>`
- `<object kind="symbol" asset="..." ... />`
- `<connection kind="wire|pipe|link" from="object.port" to="object.port" />`

Legacy panel documents:

- `<panel ...>`
- `<region ...>`
- `<wire_duct ...>`
- `<rail ...>`
- `<device ...>`

## Project Structure

```text
include/                 C headers
src/                     C source files
libraries/               Legacy and generic asset libraries
examples/                Working sample documents
tests/                   Regression test harness
```
