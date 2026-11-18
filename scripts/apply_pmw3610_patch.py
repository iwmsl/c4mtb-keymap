#!/usr/bin/env python3
"""Apply the reviewed recovery patch only to the exact audited driver revision."""
from pathlib import Path
import argparse
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REVISION = "5c34ea0eec246a1c986111417cd779b53144629a"
PATCH = ROOT / "patches" / "pmw3610-recovery.patch"


def apply(driver: Path) -> None:
    revision = subprocess.check_output(
        ["git", "-C", str(driver), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISION:
        raise SystemExit(f"Unexpected PMW3610 revision: {revision}; expected {REVISION}")
    command = ["git", "-C", str(driver), "apply", "--ignore-space-change"]
    if subprocess.run(command + ["--reverse", "--check", str(PATCH)],
                      capture_output=True).returncode == 0:
        print("PMW3610 recovery patch already applied")
        return
    subprocess.run(command + ["--check", str(PATCH)], check=True)
    subprocess.run(command + [str(PATCH)], check=True)
    print("PMW3610 recovery patch applied")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", type=Path, nargs="?", default=
                        ROOT / "dependencies/zmk-driver-pmw3610-with-custom-studio-rpc")
    apply(parser.parse_args().driver.resolve())
