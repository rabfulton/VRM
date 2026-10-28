"""Pinned native CrispASR installer; model downloads remain owned by CrispASR."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import platform
import shutil
import tarfile
import tempfile
from urllib.request import Request, urlopen
import zipfile
from .paths import data_home, development_file

VERSION = "v0.8.45"
RELEASE_URL = f"https://github.com/CrispStrobe/CrispASR/releases/download/{VERSION}"
# Digests from the publisher's GitHub release manifest, pinned with the version.
ASSETS = {
    ("Linux", "x86_64", "cpu"): ("crispasr-linux-x86_64-cpu-legacy.tar.gz", "b3d773b7eb5f3e1e9688352f81531fd554ffd78eabf73a0ff67a57a6a6281f60"),
    ("Linux", "x86_64", "vulkan"): ("crispasr-linux-x86_64-vulkan.tar.gz", "5be72cf582d28a289ab1fe06cd32005dc2cc082d896162c40048d581564d248e"),
    ("Linux", "aarch64", "cpu"): ("crispasr-linux-arm64.tar.gz", "d84e7094e9d1af366d7e93a5660111d48f0944d78a087fde9eedff9bbc5f985c"),
    ("Darwin", "arm64", "cpu"): ("crispasr-macos-arm64.tar.gz", "dde5a633ee68077c58a296976ffcc01949129649de03106f5b7aa92834f4bbf0"),
    ("Darwin", "x86_64", "cpu"): ("crispasr-macos-x86_64-cpu-legacy.tar.gz", "168950940d0f3f0141792a41f42fb9fe60643a1b4a15e379695530ea6d3d4fc8"),
    ("Windows", "x86_64", "cpu"): ("crispasr-windows-x86_64-cpu-legacy.zip", "ce8ce5d3efb23a1068b93326aa0b73b1929335ea65821dd857548e483d2a5cb6"),
    ("Windows", "x86_64", "vulkan"): ("crispasr-windows-x86_64-vulkan.zip", "714b5de48aca97c2e0b36fbd0c20b5ce2be2ec95bb0880df744aa6420eb93c5b"),
}


def data_directory():
    if os.environ.get("VRM_SPEECH_HOME"):
        return Path(os.environ["VRM_SPEECH_HOME"]).expanduser()
    if platform.system() == "Windows":
        return Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "vrm-overlay/speech"
    if platform.system() == "Darwin":
        return Path.home() / "Library/Application Support/vrm-overlay/speech"
    return data_home() / "vrm-overlay/speech"


def _machine():
    machine = platform.machine().lower()
    return {"amd64": "x86_64", "arm64": "arm64" if platform.system() == "Darwin" else "aarch64"}.get(machine, machine)


def model_directory():
    if os.environ.get("CRISPASR_CACHE_DIR"):
        return Path(os.environ["CRISPASR_CACHE_DIR"]).expanduser()
    installed = data_directory() / "models"
    development = development_file("build/speech/models")
    if not os.environ.get("VRM_SPEECH_HOME") and not installed.exists() and development is not None and development.is_dir():
        return development
    return installed


def _binary(directory):
    name = "crispasr.exe" if platform.system() == "Windows" else "crispasr"
    return next((p for p in directory.rglob(name) if p.is_file() and os.access(p, os.X_OK)), None)


def find_crispasr(executable="", device="vulkan"):
    if executable:
        candidate = shutil.which(str(Path(executable).expanduser()))
        if candidate:
            return candidate
        raise RuntimeError(f"CrispASR executable not found: {executable}")
    variant = "vulkan" if device == "vulkan" else "cpu"
    roots = [data_directory()]
    development = development_file('build/speech')
    if development is not None:
        roots.append(development)
    for root in roots:
        directory = root / VERSION / variant
        if directory.is_dir():
            binary = _binary(directory)
            if binary:
                return str(binary.resolve())
    candidate = shutil.which("crispasr")
    if candidate:
        return candidate
    raise RuntimeError("CrispASR is not installed. Use Prepare speech in the Speech tab, or set its executable path.")


def install_crispasr(device="vulkan", directory=None, stop_event=None, on_status=lambda _value: None):
    variant = "vulkan" if device == "vulkan" else "cpu"
    key = (platform.system(), _machine(), variant)
    if key not in ASSETS:
        raise RuntimeError(f"No bundled CrispASR {variant} download for {key[0]}/{key[1]}. Select CPU or provide a native executable.")
    name, expected = ASSETS[key]
    destination = (Path(directory) if directory else data_directory()) / VERSION / variant
    if destination.is_dir():
        binary = _binary(destination)
        if binary:
            return str(binary.resolve())
        raise RuntimeError(f"Incomplete speech installation: {destination}. Choose another installation directory.")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".speech-install-", dir=destination.parent) as work:
        archive = Path(work) / name
        digest = hashlib.sha256()
        request = Request(f"{RELEASE_URL}/{name}", headers={"User-Agent": "vrm-overlay-speech/1"})
        with urlopen(request, timeout=30) as response, archive.open("wb") as output:
            total = int(response.headers.get("Content-Length", 0))
            received = 0
            while True:
                if stop_event is not None and stop_event.is_set():
                    raise RuntimeError("Speech setup cancelled")
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                received += len(chunk)
                if received > 256 * 1024 * 1024:
                    raise RuntimeError("Speech engine archive exceeded its size limit")
                output.write(chunk)
                digest.update(chunk)
                on_status(f"Downloading speech engine: {received//1048576} MB" + (f" / {total//1048576} MB" if total else ""))
        if digest.hexdigest() != expected:
            raise RuntimeError("Speech engine checksum mismatch; installation was not changed")
        unpacked = Path(work) / "runtime"
        unpacked.mkdir()
        if name.endswith(".zip"):
            with zipfile.ZipFile(archive) as zipped:
                for member in zipped.infolist():
                    target = (unpacked / member.filename).resolve()
                    if not target.is_relative_to(unpacked.resolve()) or (member.external_attr >> 16) & 0o170000 == 0o120000:
                        raise RuntimeError("Unsafe path in speech engine archive")
                zipped.extractall(unpacked)
        else:
            with tarfile.open(archive) as tar:
                tar.extractall(unpacked, filter="data")
        if stop_event is not None and stop_event.is_set():
            raise RuntimeError("Speech setup cancelled")
        binary = _binary(unpacked)
        if binary is None:
            raise RuntimeError("Speech engine archive contains no usable executable")
        relative = binary.relative_to(unpacked)
        unpacked.rename(destination)
        on_status(f"Speech engine installed ({VERSION})")
        return str((destination / relative).resolve())
