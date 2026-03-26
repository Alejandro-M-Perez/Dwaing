#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BIN_COPY=$(mktemp /tmp/dwaing_test.XXXXXX)
trap 'rm -f "$BIN_COPY"' EXIT INT TERM
cp ./bin/dwaing "$BIN_COPY"
chmod +x "$BIN_COPY"

panel_validate_output=$($BIN_COPY validate libraries/default/library.xml examples/panel.xml)
printf '%s\n' "$panel_validate_output" | grep -q "^VALID: panel 'FEATURE_DEMO_PANEL' passed all checks\."

panel_summary_output=$($BIN_COPY summary libraries/default/library.xml examples/panel.xml)
printf '%s\n' "$panel_summary_output" | grep -q '^Kind: panel$'
printf '%s\n' "$panel_summary_output" | grep -q '^Devices: 13$'

ab_panel_validate_output=$($BIN_COPY validate libraries/custom/AB_library_extended.xml examples/AB_panel.xml)
printf '%s\n' "$ab_panel_validate_output" | grep -q "^VALID: panel 'AB_DEMO_PANEL' passed all checks\."

ab_panel_summary_output=$($BIN_COPY summary libraries/custom/AB_library_extended.xml examples/AB_panel.xml)
printf '%s\n' "$ab_panel_summary_output" | grep -q '^Kind: panel$'
printf '%s\n' "$ab_panel_summary_output" | grep -q '^Devices: 12$'

generic_validate_output=$($BIN_COPY validate libraries/generic/process_library.xml examples/general_arrangement.xml)
printf '%s\n' "$generic_validate_output" | grep -q "^VALID: ga 'GA_SAMPLE' passed all checks\."

generic_summary_output=$($BIN_COPY summary libraries/generic/process_library.xml examples/general_arrangement.xml)
printf '%s\n' "$generic_summary_output" | grep -q '^Kind: ga$'
printf '%s\n' "$generic_summary_output" | grep -q '^Connections: 1$'
printf '%s\n' "$generic_summary_output" | grep -q '^connection=link_a from=pump_1.OUT to=tank_1.IN points=2$'

one_line_validate_output=$($BIN_COPY validate libraries/generic/process_library.xml examples/one_line.xml)
printf '%s\n' "$one_line_validate_output" | grep -q "^VALID: one_line 'ONE_LINE_SAMPLE' passed all checks\."

one_line_summary_output=$($BIN_COPY summary libraries/generic/process_library.xml examples/one_line.xml)
printf '%s\n' "$one_line_summary_output" | grep -q '^Kind: one_line$'
printf '%s\n' "$one_line_summary_output" | grep -q '^Assets: 6$'
printf '%s\n' "$one_line_summary_output" | grep -q '^connection=feeder_1 from=main_breaker.LOAD to=pump_motor.LINE points=4$'

pid_validate_output=$($BIN_COPY validate libraries/generic/process_library.xml examples/pid.xml)
printf '%s\n' "$pid_validate_output" | grep -q "^VALID: pid 'PID_SAMPLE' passed all checks\."

pid_summary_output=$($BIN_COPY summary libraries/generic/process_library.xml examples/pid.xml)
printf '%s\n' "$pid_summary_output" | grep -q '^Kind: pid$'
printf '%s\n' "$pid_summary_output" | grep -q '^Objects: 5$'
printf '%s\n' "$pid_summary_output" | grep -q '^Connections: 3$'

set +e
invalid_output=$($BIN_COPY validate libraries/generic/process_library.xml examples/general_arrangement_invalid.xml 2>&1)
invalid_status=$?
set -e
[ "$invalid_status" -eq 2 ]
printf '%s\n' "$invalid_output" | grep -q "connection 'broken_link': unknown to object 'missing_tank'"
