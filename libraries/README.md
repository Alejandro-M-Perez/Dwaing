# Libraries

This folder contains both legacy panel block libraries and generalized asset libraries.

- `default/library.xml`: default legacy electrical panel block library.
- `custom/AB_library_extended.xml`: Allen-Bradley-oriented legacy/custom panel library.
- `generic/process_library.xml`: generalized symbol/assembly library for one-line, P&ID, and GA documents.

Legacy panel libraries use:

- `<panel_vector ... />`
- `<backplane_vector ... />`
- `<block ...>` with nested `<V ... />` or `<connector ... />`

Generic asset libraries use:

- `<symbol ...>` with nested `<port ... />`
- `<assembly ...>` with nested `<port ... />` and `<member ... />`

Examples:

```sh
./bin/dwaing validate libraries/default/library.xml examples/panel.xml
./bin/dwaing validate libraries/custom/AB_library_extended.xml examples/AB_panel.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/pid.xml
```
