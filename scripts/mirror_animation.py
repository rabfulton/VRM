#!/usr/bin/env python3
"""Create a mirrored humanoid VRMA without changing or overwriting its source."""
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from vrm_overlay.vrma import mirror_vrma


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    try:
        result = mirror_vrma(args.source, args.destination)
    except (OSError, ValueError, KeyError, IndexError) as error:
        parser.exit(1, f'Mirror failed: {error}\n')
    print(f'Created {result} and {result.with_suffix(".cfg")}')


if __name__ == '__main__':
    main()
