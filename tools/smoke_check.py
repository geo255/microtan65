#!/usr/bin/env python3
"""Quick smoke checks for required runtime files."""

from pathlib import Path
import sys

REQUIRED_FILES = [
    "microtan.rom",
    "charset.rom",
    "sounds/explosion.wav",
    "sounds/heartbeat1.wav",
    "sounds/heartbeat2.wav",
    "sounds/hit.wav",
    "sounds/laser.wav",
    "sounds/saucer.wav",
    "sounds/saucerend.wav",
]


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    missing = [path for path in REQUIRED_FILES if not (root / path).is_file()]

    if missing:
        print("Smoke check failed. Missing files:")
        for path in missing:
            print(f"  - {path}")
        return 1

    print(f"Smoke check passed ({len(REQUIRED_FILES)} files present).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
