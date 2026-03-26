# Examples

This folder contains working sample documents for each supported workflow.

- `panel.xml`: current panel-layout demo using the default legacy electrical library.
- `AB_panel.xml`: panel-layout demo using the Allen-Bradley custom library.
- `one_line.xml`: one-line electrical diagram using generic symbols and a wire connection.
- `pid.xml`: P&ID-style document with grouped equipment, piping connections, and an assembly.
- `general_arrangement.xml`: general arrangement document with generic placed objects and a logical link.
- `general_arrangement_invalid.xml`: intentionally broken GA document used by the regression tests.

Quick checks:

```sh
./bin/dwaing validate libraries/default/library.xml examples/panel.xml
./bin/dwaing validate libraries/custom/AB_library_extended.xml examples/AB_panel.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/one_line.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/pid.xml
./bin/dwaing validate libraries/generic/process_library.xml examples/general_arrangement.xml
```

Viewer:

```sh
./bin/dwaing_viewer examples/panel.xml libraries/default/library.xml
./bin/dwaing_viewer examples/AB_panel.xml libraries/custom/AB_library_extended.xml
./bin/dwaing_viewer examples/pid.xml libraries/generic/process_library.xml
```
