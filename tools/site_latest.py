#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Adapted from CHOPPAH's tools/site_latest.py (same author).
"""A stable name for the firmware package on the site.
   tools/site_latest.py site
The site keeps one package, firmware/fm1-violioliolol-<version>.fwsc. A catalogue that names that file by version
goes stale at the next release, so this copies it as firmware/fm1-violioliolol-latest.fwsc as well. The installer
keeps naming the versioned file."""
import shutil
import sys
from pathlib import Path

STEM = "fm1-violioliolol"


def main(argv):
    if len(argv) != 2:
        print("usage: site_latest.py <site-dir>", file=sys.stderr)
        return 2
    fw = Path(argv[1]) / "firmware"
    pkgs = sorted(p for p in fw.glob(f"{STEM}-*.fwsc") if not p.name.endswith("-latest.fwsc")) if fw.is_dir() else []
    if len(pkgs) != 1:
        print(f"{fw}: want exactly one {STEM}-*.fwsc, found {len(pkgs)}", file=sys.stderr)
        return 1
    dst = fw / f"{STEM}-latest.fwsc"
    shutil.copyfile(pkgs[0], dst)
    print(f"site: {dst.name} ({pkgs[0].name}, {dst.stat().st_size} B)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
