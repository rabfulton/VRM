#!/usr/bin/env python3
"""Install native speech without pip/torch; optionally warm the selected model."""
import argparse
import os
from pathlib import Path
import sys
import threading

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from vrm_overlay.tts import CrispAsrTts, SpeechConfig
from vrm_overlay.tts_install import install_crispasr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", choices=["cpu", "vulkan"], default="vulkan")
    parser.add_argument("--directory", type=Path, help="Runtime install root (default: user application data)")
    parser.add_argument("--prepare", action="store_true", help="Also download/load the model (Nano: approximately 700 MB)")
    parser.add_argument("--model", default="chatterbox-nano")
    args = parser.parse_args()
    if args.directory:
        args.directory = args.directory.resolve()
        os.environ["VRM_SPEECH_HOME"] = str(args.directory)
    stopped = threading.Event()
    engine = None
    try:
        binary = install_crispasr(args.device, args.directory, stopped, lambda message: print(message, flush=True))
        print(f"CrispASR executable: {binary}", flush=True)
        if args.prepare:
            engine = CrispAsrTts(SpeechConfig(executable=binary, model=args.model, device=args.device),
                                 lambda message: print(message, flush=True))
            engine.prepare(stopped)
            print("Speech model ready", flush=True)
    except KeyboardInterrupt:
        stopped.set()
        return 130
    except Exception as error:
        print(f"Speech setup failed: {error}", file=sys.stderr)
        return 1
    finally:
        if engine:
            engine.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
