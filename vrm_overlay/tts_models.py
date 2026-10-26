"""Pinned Nano/Turbo model pairs with verified, resumable downloads."""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen

from .tts_install import model_directory

MODEL_LICENSE = """MIT License

Copyright (c) 2025 Resemble AI

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


@dataclass(frozen=True)
class ModelFile:
    repo: str
    revision: str
    name: str
    size: int
    sha256: str

    @property
    def url(self):
        return f"https://huggingface.co/{self.repo}/resolve/{self.revision}/{self.name}"


NANO = ModelFile("cstr/chatterbox-nano-GGUF", "e974b29a5aa94ddf1ea8653e79e1601fecb8ba16",
                 "chatterbox-nano-t3-q8_0.gguf", 345147872,
                 "303927da8c52ff9d1aa651a45955a1320bd418ec522a6bca32fa41f5274b4771")
TURBO = ModelFile("cstr/chatterbox-turbo-GGUF", "b544cf8b49504d880640864a757b3ff3e4421a42",
                  "chatterbox-turbo-t3-q8_0.gguf", 658897472,
                  "35520ec377209a55e064464996f71694546eb8fdf793c719a24d44729e098d57")
DECODER = ModelFile("cstr/chatterbox-turbo-GGUF", "b544cf8b49504d880640864a757b3ff3e4421a42",
                    "chatterbox-turbo-s3gen-q8_0.gguf", 366820384,
                    "a77e3ef2559e8c4411b06a2692eb35646a43b0e5eef899633271aa281122e750")
MODEL_FILES = {"chatterbox-nano": (NANO, DECODER), "chatterbox-turbo": (TURBO, DECODER)}


def _hash_file(path, stop_event=None):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024*1024):
            if stop_event is not None and stop_event.is_set():
                raise RuntimeError("Speech setup cancelled")
            digest.update(chunk)
    return digest.hexdigest()


def download_model(asset, directory, stop_event=None, on_status=lambda _value: None):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / asset.name
    if target.is_file() and target.stat().st_size == asset.size:
        on_status(f"Verifying {asset.name}…")
        if _hash_file(target, stop_event) == asset.sha256:
            return target
    partial = directory / (asset.name + ".download")
    offset = partial.stat().st_size if partial.is_file() else 0
    if offset > asset.size:
        partial.unlink()
        offset = 0
    if stop_event is not None and stop_event.is_set():
        raise RuntimeError("Speech setup cancelled")
    if offset < asset.size:
        headers = {"User-Agent": "vrm-overlay-speech/1"}
        if offset:
            headers["Range"] = f"bytes={offset}-"
        with urlopen(Request(asset.url, headers=headers), timeout=30) as response:
            if response.status == 206:
                if not response.headers.get("Content-Range", "").startswith(f"bytes {offset}-"):
                    raise RuntimeError("Model server returned an unexpected resume range")
            elif response.status == 200:
                offset = 0  # server ignored Range; replace our partial, never append
            else:
                raise RuntimeError(f"Model download failed: HTTP {response.status}")
            with partial.open("ab" if offset else "wb") as output:
                received = offset
                while True:
                    if stop_event is not None and stop_event.is_set():
                        raise RuntimeError("Speech setup cancelled; Prepare voice will resume the download")
                    chunk = response.read(256*1024)
                    if not chunk:
                        break
                    received += len(chunk)
                    if received > asset.size:
                        raise RuntimeError("Downloaded model exceeds its pinned size")
                    output.write(chunk)
                    on_status(f"Downloading {asset.name}: {received//1048576} / {asset.size//1048576} MB")
    if partial.stat().st_size != asset.size:
        raise RuntimeError("Model download was interrupted; Prepare voice will resume it")
    if _hash_file(partial, stop_event) != asset.sha256:
        partial.unlink()
        raise RuntimeError("Speech model checksum mismatch; retry Prepare voice")
    partial.replace(target)
    return target


def prepare_model_files(model, stop_event=None, on_status=lambda _value: None, include_decoder=True):
    assets = MODEL_FILES.get(model)
    if not assets:
        return None
    if not include_decoder:
        assets = assets[:1]
    directory = model_directory()
    paths = [download_model(asset, directory, stop_event, on_status) for asset in assets]
    # Keep exact provenance and licensing with the downloaded model pair.
    notice = directory / (model + ".source.json")
    notice.write_text(json.dumps({"model": model, "license": "MIT", "files": [
        {"url": a.url, "sha256": a.sha256, "size": a.size} for a in assets],
        "upstream": "https://github.com/resemble-ai/chatterbox",
        "license_url": "https://github.com/resemble-ai/chatterbox/blob/master/LICENSE"}, indent=2)+"\n")
    (directory / "chatterbox.LICENSE").write_text(MODEL_LICENSE)
    return paths
