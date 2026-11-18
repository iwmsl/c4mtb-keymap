#!/usr/bin/env python3
"""Run the firmware/hardware verifier while allowing Keymap Editor combo moves.

The base verifier intentionally protects C4mtb hardware/layout invariants. Combo key
positions, however, are user-editable in the ZMK Keymap Editor. This wrapper keeps
all base checks, but accepts failures that are *only* caused by moving the three
standard combos, provided their actions remain the expected recovery/click actions
and every referenced key position is valid for the 38-key layout.
"""
from __future__ import annotations

import ast
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "scripts" / "verify_firmware_hardware.py"
KEYMAP = ROOT / "config" / "c4mtb.keymap"
KEY_COUNT = 38

EXPECTED_BINDINGS = {
    "settings_layer": "&mo 4",
    "left_click": "&mkp LCLK",
    "right_click": "&mkp RCLK",
}


def parse_combo_mismatch(failure: str) -> tuple[str, list[int], str] | None:
    match = re.fullmatch(
        r"ZMK combo (settings_layer|left_click|right_click): "
        r"\((\[[^]]*\]), '([^']*)'\) != .*",
        failure,
    )
    if not match:
        return None
    name, positions_text, binding = match.groups()
    try:
        positions = ast.literal_eval(positions_text)
    except (SyntaxError, ValueError):
        return None
    if not isinstance(positions, list) or not all(isinstance(pos, int) for pos in positions):
        return None
    return name, positions, binding


def positions_are_valid(positions: list[int]) -> bool:
    return (
        len(positions) >= 2
        and len(set(positions)) == len(positions)
        and all(0 <= pos < KEY_COUNT for pos in positions)
    )


def main() -> int:
    proc = subprocess.run(
        [sys.executable, str(BASE)],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )

    if proc.returncode == 0:
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return 0

    failures = [
        line.removeprefix("  - ")
        for line in proc.stdout.splitlines()
        if line.startswith("  - ")
    ]
    if not failures:
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return proc.returncode

    accepted: list[str] = []
    rejected: list[str] = []

    for failure in failures:
        parsed = parse_combo_mismatch(failure)
        if parsed:
            name, positions, binding = parsed
            if binding == EXPECTED_BINDINGS[name] and positions_are_valid(positions):
                accepted.append(f"{name}: positions={positions}")
                continue

        # Keymap Editor may add/remove/change timeout-ms. Timeout is intentionally
        # user-editable; the recovery action itself is still protected above.
        if failure == "ZMK combo settings_layer: timeout-ms=80が見つからない":
            accepted.append("settings_layer: timeout is editor-managed")
            continue

        rejected.append(failure)

    if rejected:
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        return proc.returncode

    # Guard against accidentally accepting an old runtime-combo keymap. The base
    # verifier already checks this, but keeping the condition explicit here makes
    # the compatibility exception narrow and self-documenting.
    keymap = KEYMAP.read_text(encoding="utf-8")
    if 'compatible = "zmk,combos"' not in keymap or "runtime_combo_defaults" in keymap:
        sys.stdout.write(proc.stdout)
        print("FAIL: Keymap Editor compatibility guard rejected non-standard combo state")
        return 1

    print("PASS: firmware/hardware checks; Keymap Editor combo positions accepted")
    for item in accepted:
        print(f"  - {item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
