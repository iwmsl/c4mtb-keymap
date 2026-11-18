#!/usr/bin/env python3
"""Compile real patched driver functions with deterministic GPIO/SPI/work mocks."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
driver = Path(sys.argv[1])
source = (driver / "src/pmw3610.c").read_text(encoding="utf-8")
names = ["pmw3610_write", "pmw3610_report_data", "pmw3610_recover_locked",
         "pmw3610_health_check", "pmw3610_work_callback", "on_activity_state"]
functions = []
for name in names:
    start = source.index(f"static {'void' if name in names[2:5] else 'int'} {name}(")
    end = source.index("\n}", start) + 2
    functions.append(source[start:end])
harness = (ROOT / "tests/pmw3610_recovery_harness.c").read_text(encoding="utf-8")
harness = harness.replace("/* DRIVER_FUNCTIONS */", "\n\n".join(functions))
compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
if not compiler:
    raise SystemExit("C compiler required (CC, cc, gcc, or clang)")
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "recovery.c"
    exe = Path(tmp) / ("recovery.exe" if os.name == "nt" else "recovery")
    c.write_text(harness, encoding="utf-8")
    subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
