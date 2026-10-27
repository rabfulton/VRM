"""Local, self-contained avatar installs. No model downloads or extra dependencies."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import struct
import tempfile
from urllib.parse import unquote, urlsplit
import zipfile

MAX_MODEL = 512 * 1024 * 1024
MAX_JSON = 16 * 1024 * 1024
MAX_THUMBNAIL = 8 * 1024 * 1024
MAX_ARCHIVE = 1024 * 1024 * 1024


def _text(value, fallback=""):
    return value[:500] if isinstance(value, str) else fallback


def _relative(value):
    path = PurePosixPath(value)
    if not value or "\\" in value or ":" in value or path.is_absolute() or ".." in path.parts:
        raise ValueError("The avatar contains an unsafe file path.")
    return path


def _copy(source, target, cancelled=None, digest=None):
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("wb") as output:
        while chunk := source.read(1024 * 1024):
            if cancelled and cancelled.is_set():
                raise ValueError("Import cancelled.")
            output.write(chunk)
            if digest is not None:
                digest.update(chunk)


def inspect_model(path):
    """Read bounded GLB/glTF metadata and its embedded VRM thumbnail."""
    path = Path(path)
    size = path.stat().st_size
    if size > MAX_MODEL:
        raise ValueError("This avatar exceeds the 512 MB import limit.")
    binary = None
    with path.open("rb") as stream:
        if path.suffix.lower() == ".gltf":
            if size > MAX_JSON:
                raise ValueError("The glTF JSON is too large.")
            doc = json.load(stream)
        else:
            header = stream.read(20)
            if len(header) != 20:
                raise ValueError("This is not a complete VRM/GLB file.")
            magic, version, length, chunk_size, chunk_type = struct.unpack("<5I", header)
            if (magic != 0x46546C67 or version != 2 or length != size or
                    chunk_type != 0x4E4F534A or chunk_size > MAX_JSON or chunk_size > size - 20):
                raise ValueError("This is not a supported VRM/GLB 2.0 file.")
            doc = json.loads(stream.read(chunk_size))
            chunk = stream.read(8)
            if len(chunk) == 8:
                count, kind = struct.unpack("<2I", chunk)
                if kind == 0x004E4942 and stream.tell() + count <= size:
                    binary = (stream.tell(), count)
    if not isinstance(doc, dict) or not isinstance(doc.get("meshes"), list) or not doc["meshes"]:
        raise ValueError("This file has no avatar mesh. Import a model, rather than an animation.")
    if not isinstance(doc.get("asset"), dict) or doc["asset"].get("version") != "2.0":
        raise ValueError("Only glTF 2.0 avatars are supported.")
    extensions = doc.get("extensions", {})
    vrm = extensions.get("VRMC_vrm") or extensions.get("VRM") or {}
    if path.suffix.lower() == ".vrm" and not vrm:
        raise ValueError("The file has no VRM avatar metadata.")
    meta = vrm.get("meta", {})
    authors = meta.get("authors", [])
    author = ", ".join(_text(a) for a in authors[:8]) if isinstance(authors, list) else ""
    details = dict(name=_text(meta.get("name") or meta.get("title"), path.stem),
                   author=author or _text(meta.get("author"), "Unknown creator"),
                   rig="humanoid" if vrm.get("humanoid") else "generic",
                   license=_text(meta.get("licenseName"), "See embedded metadata / creator terms"),
                   license_url=_text(meta.get("licenseUrl") or meta.get("otherLicenseUrl")))
    thumbnail = None
    try:
        if "thumbnailImage" in meta:
            index = meta["thumbnailImage"]
        else:
            texture = meta["texture"]
            if not isinstance(texture, int) or texture < 0:
                raise ValueError()
            index = doc["textures"][texture]["source"]
        if not isinstance(index, int) or index < 0:
            raise ValueError()
        image = doc["images"][index]
        view_index = image["bufferView"]
        if not isinstance(view_index, int) or view_index < 0:
            raise ValueError()
        view = doc["bufferViews"][view_index]
        offset, count = view.get("byteOffset", 0), view["byteLength"]
        if (binary and view.get("buffer", 0) == 0 and isinstance(offset, int) and
                isinstance(count, int) and 0 <= offset and 0 < count <= MAX_THUMBNAIL and
                offset + count <= binary[1]):
            with path.open("rb") as stream:
                stream.seek(binary[0] + offset)
                data = stream.read(count)
            if data.startswith(b"\x89PNG\r\n\x1a\n"):
                thumbnail = ("png", data)
            elif data.startswith(b"\xff\xd8\xff"):
                thumbnail = ("jpg", data)
    except (KeyError, IndexError, TypeError, ValueError):
        pass  # A missing/broken preview must not prevent using the model.
    return doc, details, thumbnail


class AvatarStore:
    def __init__(self, root):
        self.root = Path(root).resolve()

    def entries(self):
        rows = []
        for manifest in sorted(self.root.glob("*/avatar.json")):
            if manifest.parent.name.startswith('.'):
                continue  # Only publish installs after their atomic rename.
            try:
                entry = json.loads(manifest.read_text())
                model = (manifest.parent / _relative(entry["file"])).resolve()
                if not model.is_relative_to(self.root) or not model.is_file():
                    continue
                thumbnail = manifest.parent / "thumbnail.png"
                if not thumbnail.is_file():
                    thumbnail = manifest.parent / "thumbnail.jpg"
                rows.append(dict(id=manifest.parent.name, name=_text(entry.get("name"), model.stem),
                                 author=_text(entry.get("author"), "Unknown creator"), path=str(model),
                                 thumbnail=thumbnail.as_uri() if thumbnail.is_file() else "",
                                 rig=entry.get("rig", "generic"), badge="Imported", kind="installed",
                                 description="Installed locally · " + entry.get("original", model.name),
                                 source_url=_text(entry.get("source_url")),
                                 license=_text(entry.get("license")),
                                 license_url=_text(entry.get("license_url")),
                                 instructions="", source_id=_text(entry.get("source_id"))))
            except (OSError, ValueError, KeyError, TypeError):
                continue
        return sorted(rows, key=lambda e: e["name"].casefold())

    @staticmethod
    def archive_models(path):
        with zipfile.ZipFile(path) as archive:
            files = archive.infolist()
            if len(files) > 4096 or sum(f.file_size for f in files) > MAX_ARCHIVE:
                raise ValueError("The ZIP is too large. Extract it and import a VRM file instead.")
            names = set()
            models = []
            for entry in files:
                _relative(entry.filename)
                if entry.filename in names or stat.S_ISLNK(entry.external_attr >> 16):
                    raise ValueError("This ZIP contains duplicate paths or symbolic links.")
                names.add(entry.filename)
                if entry.flag_bits & 1:
                    raise ValueError("Encrypted ZIP files must be extracted before importing.")
                if not entry.is_dir() and PurePosixPath(entry.filename).suffix.lower() in (".vrm", ".glb"):
                    if entry.file_size > MAX_MODEL:
                        raise ValueError("A model in this ZIP exceeds the 512 MB import limit.")
                    models.append(entry.filename)
            if not models:
                raise ValueError("No VRM/GLB model found in this ZIP. For glTF, extract it and import the .gltf file.")
            return sorted(models, key=lambda s: (not s.lower().endswith(".vrm"), s.casefold()))

    def install(self, path, *, member="", source=None, cancelled=None):
        path = Path(path).resolve()
        if not path.is_file():
            raise ValueError("The selected avatar file no longer exists.")
        suffix = path.suffix.lower()
        if suffix not in (".vrm", ".glb", ".gltf", ".zip"):
            raise ValueError("Choose a VRM, GLB, glTF or ZIP file.")
        self.root.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(prefix=".import-", dir=self.root))
        try:
            digest = hashlib.sha256()
            if suffix == ".zip":
                models = self.archive_models(path)
                if member not in models:
                    raise ValueError("Choose a model from this ZIP before importing.")
                model = work / ("model" + PurePosixPath(member).suffix.lower())
                with zipfile.ZipFile(path) as archive:
                    with archive.open(member) as stream:
                        _copy(stream, model, cancelled, digest)
                    # Keep the archive's accompanying terms without extracting its other assets.
                    for i, entry in enumerate(archive.infolist()):
                        name = PurePosixPath(entry.filename).name
                        if (not entry.is_dir() and entry.file_size < 10 * 1024 * 1024 and
                                name.lower().endswith((".txt", ".md", ".pdf"))):
                            with archive.open(entry) as stream:
                                _copy(stream, work / "notices" / f"{i}-{name}", cancelled)
                original = f"{path.name} / {member}"
            else:
                if path.stat().st_size > MAX_MODEL:
                    raise ValueError("This avatar exceeds the 512 MB import limit.")
                model = work / ("model" + suffix)
                with path.open("rb") as stream:
                    _copy(stream, model, cancelled, digest)
                original = path.name
            doc, details, thumbnail = inspect_model(model)
            if details['name'] == model.stem:
                details['name'] = PurePosixPath(member).stem if member else path.stem
            total = model.stat().st_size
            resources = set()
            for resource in doc.get("buffers", []) + doc.get("images", []):
                uri = resource.get("uri", "")
                if not uri or uri.startswith("data:"):
                    continue
                if suffix != ".gltf":
                    raise ValueError("This GLB has external files. Export a self-contained GLB/VRM first.")
                url = urlsplit(uri)
                if url.scheme or url.netloc or url.query or url.fragment:
                    raise ValueError("Import requires local glTF resources.")
                relative = str(_relative(unquote(url.path)))
                if relative in resources:
                    continue
                resources.add(relative)
                resource_path = (path.parent / relative).resolve()
                if not resource_path.is_relative_to(path.parent) or not resource_path.is_file():
                    raise ValueError(f"Missing or unsafe glTF resource: {relative}")
                total += resource_path.stat().st_size
                if total > MAX_MODEL:
                    raise ValueError("This avatar and its resources exceed 512 MB.")
                target = work / relative
                if target == model or relative == "avatar.json" or relative.startswith("thumbnail."):
                    raise ValueError("A glTF resource conflicts with an import metadata file.")
                digest.update(relative.encode())
                with resource_path.open("rb") as stream:
                    _copy(stream, target, cancelled, digest)
            if thumbnail:
                (work / f"thumbnail.{thumbnail[0]}").write_bytes(thumbnail[1])
            details.update(file=model.name, original=original,
                           source_id=(source or {}).get("id", ""),
                           source_url=(source or {}).get("source_url", ""))
            (work / "avatar.json").write_text(json.dumps(details, ensure_ascii=False, indent=2))
            if cancelled and cancelled.is_set():
                raise ValueError("Import cancelled.")
            identity = digest.hexdigest()
            target = self.root / identity
            if not target.exists():
                os.rename(work, target)
            installed = next((e for e in self.entries() if e["id"] == identity), None)
            if installed is None:
                raise ValueError("This avatar's existing install is incomplete. Remove its folder and import again.")
            return installed
        finally:
            if work.exists():
                shutil.rmtree(work)
