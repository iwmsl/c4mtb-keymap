#!/usr/bin/env python3
"""Static privacy and wiring checks for C4MTB telemetry."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/c4mtb_telemetry.c").read_text()
RIGHT_CONF = (ROOT / "config/c4mtb_r.conf").read_text()
LEFT_CONF = (ROOT / "config/c4mtb_l.conf").read_text()
OVERLAY = (ROOT / "boards/shields/c4mtb/c4mtb_r.overlay").read_text()
BUILD = (ROOT / "build.yaml").read_text()
EXPORT_CONF = (ROOT / "snippets" / "c4mtb-telemetry-export" / "c4mtb-telemetry-export.conf").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


require("CONFIG_C4MTB_TELEMETRY=y" in RIGHT_CONF, "telemetry must be enabled on central half")
require("CONFIG_C4MTB_TELEMETRY=y" not in LEFT_CONF, "telemetry must not run on peripheral half")
for mode in range(3):
    require(f"<&c4mtb_telemetry {mode}>" in OVERLAY, f"pointer mode {mode} is not wired")
require("INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(mkp)), mouse_button_input_callback, NULL)" in SOURCE,
        "mouse-button telemetry is not wired")
require("c4mtb_R_telemetry_export" in BUILD, "dedicated export artifact is missing")
require("CONFIG_ZMK_RUNTIME_INPUT_PROCESSOR=y" in EXPORT_CONF,
        "telemetry export must keep the overlay runtime processors linked")
require("TELEMETRY_POSITION_COUNT 38" in SOURCE, "position count contract changed")
require("TELEMETRY_LAYER_COUNT 6" in SOURCE, "layer count contract changed")

for forbidden in ("keycode_count", "timestamp_count", "application_name", "window_title", "clipboard"):
    require(forbidden not in SOURCE.lower(), f"privacy boundary violation: {forbidden}")

print("C4MTB telemetry checks passed")
