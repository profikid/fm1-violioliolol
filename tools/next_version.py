#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The next release version, from the repository's tags and the commits being released.

  tools/next_version.py [--bump patch|minor|major] [--messages FILE] [--head REV]

Prints the version (no "v"), or "skip". The tags are vX.Y or vX.Y.Z (a suffix such as -rc1 is a prerelease and is
not counted); fm1-base's build.py --release takes up to two digits a part.

  patch  vX.Y or vX.Y.Z  ->  X.Y.(Z+1)      the default: every push to main
  minor  ->  X.(Y+1)                        "[minor]" in a commit's subject line or the PR title
  major  ->  (X+1).0                        "[major]"

"[skip release]" (or "[no release]") there releases nothing. Only those lines count, never a message's body, so a
commit can describe the markers without setting one. The messages file holds `git log --format=%s%n%b%x1e`: each
commit's subject and body, ended by an ASCII record separator. For a merge commit ("Merge pull request #N ..."), the
first line of its body is the PR title, and that is the line read. --bump, given by a person running the workflow,
wins over the messages and over a skip. A commit that already has a version tag is not released again: "skip"."""
import argparse
import re
import subprocess
import sys

TAG = re.compile(r"^v(\d+)\.(\d+)(?:\.(\d+))?$")


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, check=True).stdout


def parse(tag):
    m = TAG.match(tag.strip())
    return (int(m[1]), int(m[2]), int(m[3] or 0)) if m else None


def latest(tags):
    versions = [v for v in (parse(t) for t in tags) if v]
    return max(versions) if versions else None


def marker_lines(messages):
    """the subject of each commit, or for a GitHub merge commit the PR title under it"""
    lines = []
    for record in messages.split("\x1e"):
        rows = [r.strip() for r in record.strip().splitlines()]
        if not rows or not rows[0]:
            continue
        lines.append(rows[0])
        if rows[0].startswith("Merge pull request") or rows[0].startswith("Merge branch"):
            title = next((r for r in rows[1:] if r), "")
            if title:
                lines.append(title)
    return lines


def bump_from(messages):
    text = "\n".join(marker_lines(messages)).lower()
    if "[skip release]" in text or "[no release]" in text:
        return "skip"
    if "[major]" in text:
        return "major"
    if "[minor]" in text:
        return "minor"
    return "patch"


def next_version(current, bump):
    if current is None:
        return "0.1"
    major, minor, patch = current
    if bump == "major":
        major, minor, patch = major + 1, 0, None
    elif bump == "minor":
        minor, patch = minor + 1, None
    else:
        patch += 1
    if major > 99 or minor > 99 or (patch or 0) > 99:
        raise SystemExit(f"next_version: {major}.{minor}.{patch}: build.py --release takes two digits a part")
    return f"{major}.{minor}" + (f".{patch}" if patch is not None else "")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bump", choices=["patch", "minor", "major"])
    ap.add_argument("--messages", help="a file with the released commits' messages")
    ap.add_argument("--head", default="HEAD")
    a = ap.parse_args(argv)
    if any(parse(t) for t in git("tag", "--points-at", a.head).split()):
        print("skip")
        return 0
    bump = a.bump
    if not bump:
        bump = bump_from(open(a.messages, encoding="utf-8").read() if a.messages else "")
    if bump == "skip":
        print("skip")
        return 0
    print(next_version(latest(git("tag", "--list", "v*").split()), bump))
    return 0


if __name__ == "__main__":
    sys.exit(main())
