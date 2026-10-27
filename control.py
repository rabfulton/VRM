#!/usr/bin/env python3
"""Compatibility launcher for domovushka; prefer ./domovushka."""
from vrm_overlay.gui import main

if __name__ == "__main__":
    raise SystemExit(main())
