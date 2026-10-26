#!/usr/bin/env python3
"""Exercise QML controls and all local animation files against the actual renderer.

Requires an X11 display and user-supplied assets. The Qt control window
uses offscreen rendering; the avatar renderer still opens on the desktop.
"""
import os
import argparse
from pathlib import Path
import sys
import shutil
import tempfile
import time
import wave

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ["QT_QPA_PLATFORMTHEME"] = "basic"
os.environ.setdefault("QT_QUICK_BACKEND", "software")
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PySide6.QtCore import QPoint, QSettings, Qt, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest
from vrm_overlay.qt_controller import Controller, ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--speech", action="store_true", help="Also synthesize Nano through the real CrispASR sidecar")
    parser.add_argument("--speech-device", choices=["cpu", "vulkan"], default="vulkan")
    parser.add_argument("--model", type=Path, default=Path(os.environ.get("VRM_SMOKE_MODEL", ROOT / "test.vrm")))
    parser.add_argument("--animation-dir", type=Path, default=Path(os.environ.get("VRM_SMOKE_ANIMATIONS", ROOT / "animation")))
    parser.add_argument("--reference-fixture", action="store_true", help="Require the original local avatar's collider and stance coverage")
    args = parser.parse_args()
    if not args.model.is_file():
        parser.error("Avatar not found; pass --model or VRM_SMOKE_MODEL (see docs/development.md)")
    if not args.animation_dir.is_dir() or not any(p.suffix.lower() in (".vrma", ".glb") for p in args.animation_dir.iterdir() if p.is_file()):
        parser.error("No animations found; pass --animation-dir or VRM_SMOKE_ANIMATIONS")
    QQuickStyle.setStyle("Fusion")
    app = QGuiApplication([])
    app.setQuitOnLastWindowClosed(False)
    with tempfile.TemporaryDirectory(prefix="vrm-studio-smoke-") as directory:
        root = Path(directory)
        bridge = Controller(str(root / "runtime.sock"), model=str(args.model.resolve()),
                            animation_dir=str(args.animation_dir.resolve()),
                            settings=QSettings(str(root / "settings.ini"), QSettings.IniFormat))
        engine = QQmlApplicationEngine()
        engine.rootContext().setContextProperty("bridge", bridge)
        engine.load(QUrl.fromLocalFile(str(ROOT / "vrm_overlay/qml/Main.qml")))
        assert engine.rootObjects(), "QML did not load"
        window = engine.rootObjects()[0]

        def wait(predicate, timeout=35):
            deadline = time.monotonic() + timeout
            while not predicate():
                if bridge.lastError:
                    raise AssertionError(bridge.lastError + "\n" + bridge.logText[-3000:])
                if time.monotonic() > deadline:
                    raise AssertionError("Timed out: " + bridge.status + "\n" + bridge.logText[-3000:])
                QTest.qWait(10)

        def click(name):
            item = window.findChild(QQuickItem, name)
            assert item is not None and item.isEnabled(), name + " is unavailable"
            point = item.mapToScene(item.boundingRect().center())
            QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, QPoint(round(point.x()), round(point.y())))

        def command(action, values, predicate=lambda: True):
            bridge.command(action, values)
            wait(lambda: not bridge.busy and predicate())

        try:
            bridge.connectRuntime()
            wait(lambda: bridge.connected and bridge.state["model_loaded"] and not bridge.busy)
            assert bridge.state["lip_sync"], "Lip sync should be enabled before any audio command"
            physics=bridge.state["physics"]
            if args.reference_fixture:
                assert bridge.state["foot_lock"]["supported"] == 2, bridge.state["foot_lock"]
                assert physics["garment_chains"] == 6, physics
                assert physics["arm_colliders"] >= 16, physics
            print(f"Physics: {physics['garment_chains']} garment chains, {physics['arm_colliders']} arm volumes",flush=True)
            command("set_auto_resize", {"enabled": False})
            assert bridge.animationFiles, "No animations in the selected library"
            for index, entry in enumerate(bridge.animationFiles):
                bridge.playAnimation(index)
                wait(lambda: bridge.selectedAnimation == index and not bridge.busy)
                assert bridge.state["playing"], entry["file"] + " did not start"
                assert bridge.state["duration"] > 0, entry["file"] + " has no duration"
                print(f"Animation {index + 1}/{len(bridge.animationFiles)}: {entry['file']}", flush=True)
            # Let representative poses evolve beyond their transition frames.
            import math
            observed_plant=False
            for name in ("Relax", "restless_arms", "push_left", "push_right"):
                index=next((i for i,f in enumerate(bridge.animationFiles) if f["name"]==name),None)
                if index is None:
                    continue
                bridge.playAnimation(index)
                wait(lambda: bridge.selectedAnimation==index and not bridge.busy)
                wait(lambda: not bridge.state["blend"]["active"])
                maximum=0
                for _ in range(4):
                    QTest.qWait(250)
                    physics=bridge.state["physics"]
                    assert math.isfinite(physics["max_penetration"]), physics
                    maximum=max(maximum,physics["garment_arm_penetration"])
                    for side in ("left", "right"):
                        foot=bridge.state["foot_lock"][side]
                        assert math.isfinite(foot["error"]), foot
                        if foot["planted"] and foot["weight"] >= 0.99:
                            observed_plant=True
                            assert foot["error"] < 0.01, foot
                feet=bridge.state["foot_lock"]
                print(f"Motion check {name}: peak garment/arm proxy overlap {maximum*1000:.1f} mm; feet {feet['left']['planted']}/{feet['right']['planted']}, error {feet['left']['error']*1000:.1f}/{feet['right']['error']*1000:.1f} mm; raw height {feet['left']['height']:.3f}/{feet['right']['height']:.3f}",flush=True)
            if args.reference_fixture:
                assert observed_plant, "No planted stance was detected in the motion checks"
            jump=next((i for i,f in enumerate(bridge.animationFiles) if f["name"]=="Jump"),None)
            if jump is not None:
                bridge.playAnimation(jump)
                wait(lambda: bridge.selectedAnimation==jump and not bridge.busy)
                deadline=time.monotonic()+min(bridge.state["duration"]+0.3,10)
                airborne=0
                while time.monotonic()<deadline:
                    QTest.qWait(25)
                    for side in ("left", "right"):
                        foot=bridge.state["foot_lock"][side]
                        if foot["height"]>0.05:
                            assert not foot["planted"], foot
                            airborne+=1
                assert airborne>0, "Jump never entered an airborne contact state"
                print("Jump check: airborne feet release their anchors",flush=True)
            command("set_foot_lock", {"enabled": False}, lambda: not bridge.state["foot_lock"]["enabled"])
            command("set_foot_lock", {"enabled": True}, lambda: bridge.state["foot_lock"]["enabled"])
            previous = bridge.selectedAnimation
            click("nextAnimationButton")
            wait(lambda: bridge.selectedAnimation == (previous + 1) % len(bridge.animationFiles) and not bridge.busy)
            click("previousAnimationButton")
            wait(lambda: bridge.selectedAnimation == previous and not bridge.busy)
            click("playPauseButton")
            wait(lambda: not bridge.state["playing"] and not bridge.busy)
            command("set_time", {"t": 0.2}, lambda: abs(bridge.state["time"] - 0.2) < 0.02)
            command("set_playback", {"speed": 0.5, "loop": True}, lambda: bridge.state["speed"] == 0.5)
            click("playPauseButton")
            wait(lambda: bridge.state["playing"] and not bridge.busy)
            initial_blink = bridge.state["blink"]["enabled"]
            click("autoBlinkSwitch")
            wait(lambda: bridge.state["blink"]["enabled"] != initial_blink and not bridge.busy)
            click("autoBlinkSwitch")
            wait(lambda: bridge.state["blink"]["enabled"] == initial_blink and not bridge.busy)
            assert bridge.state["expressions"], "No expressions found"
            click("applyExpressionButton")
            wait(lambda: not bridge.busy)
            command("clear_expressions", {})
            command("set_spring", {"stiffness": 1.25, "cloth_mode": True},
                    lambda: bridge.state["spring"]["stiffness"] == 1.25 and bridge.state["spring"]["cloth_mode"])
            command("reset_spring", {})
            command("set_blend", {"duration": 0.4, "seam": 0.2, "group": 3, "factor": 0.9},
                    lambda: abs(bridge.state["blend"]["duration"] - 0.4) < 0.001)
            for shader in ("toon", "pbr", "anime"):
                command("set_render", {"shader": shader}, lambda: bridge.state["shader"] == shader)
            wav_path = root / "silence.wav"
            with wave.open(str(wav_path), "wb") as wav:
                wav.setparams((1, 2, 24000, 0, "NONE", "not compressed"))
                wav.writeframes(bytes(4800))
            bridge.playAudio(str(wav_path))
            wait(lambda: not bridge.busy and bridge.state["lip_sync"])
            command("audio_cancel", {})
            if args.speech:
                speech = bridge.speech
                speech.selectModel("chatterbox-nano")
                speech.setOption("device", args.speech_device)
                statuses = []
                speech.changed.connect(lambda: statuses.append(speech.status))
                speech.speak("Hello! I am your desktop companion.")
                wait(lambda: not speech.busy, timeout=900)
                assert not speech.error, speech.error
                assert "Speaking" in statuses, statuses
                assert bridge.state["lip_sync"]
                print(f"Speech check: real Nano/{args.speech_device}, synthesized PCM playback and drain passed", flush=True)
            window.grabWindow().save("/tmp/vrm-studio-connected.png")
            # Verify persistence without changing a source animation's tuning file.
            copied = root / "tuning.vrma"
            shutil.copyfile(bridge.animationFiles[0]["path"], copied)
            bridge.setAnimationFolder(str(root))
            bridge.playAnimation(0)
            wait(lambda: bridge.state["animation_path"] == str(copied) and not bridge.busy)
            command("set_spring", {"stiffness": 1.25})
            command("set_blend", {"seam": 0.2})
            command("save_tuning", {})
            saved = copied.with_suffix(".cfg").read_text()
            assert "spring.stiffness=1.250" in saved, saved
            assert "anim.seam_blend_time=0.200" in saved, saved
            print("Qt Quick smoke passed: all animations, next/previous, pause/seek/speed, blinking, expressions, springs, shaders and audio.")
        finally:
            bridge.shutdown()
            window.close()
            engine.deleteLater()
            app.processEvents()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
