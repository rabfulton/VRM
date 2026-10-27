"""Avatar import persistence, thumbnails and untrusted package boundaries."""
import base64
import json
from pathlib import Path
import stat
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

from vrm_overlay.avatar_library import AvatarStore, inspect_model
from vrm_overlay.avatar_catalog import featured_avatars

PNG = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGMIOr3lPwAGFQLRvwiVNgAAAABJRU5ErkJggg==')


def model_bytes(name='Companion', *, vrm1=False, mesh=True, thumbnail=True):
    meta = dict(name=name, authors=['Artist'], thumbnailImage=0) if vrm1 else dict(title=name, author='Artist', texture=0, licenseName='Other')
    doc = dict(asset=dict(version='2.0'), meshes=[dict(primitives=[])] if mesh else [],
               extensions={('VRMC_vrm' if vrm1 else 'VRM'): dict(meta=meta, humanoid=dict(humanBones={}))})
    if thumbnail:
        doc.update(buffers=[dict(byteLength=len(PNG))], bufferViews=[dict(buffer=0, byteLength=len(PNG))],
                   images=[dict(bufferView=0, mimeType='image/png')], textures=[dict(source=0)])
    data = json.dumps(doc).encode()
    data += b' ' * (-len(data) % 4)
    binary = PNG + b'\0' * (-len(PNG) % 4)
    return (struct.pack('<5I', 0x46546c67, 2, 28 + len(data) + len(binary), len(data), 0x4e4f534a) +
            data + struct.pack('<2I', len(binary), 0x004e4942) + binary)


class AvatarLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = AvatarStore(self.root / 'library')
        self.model = self.root / 'original.vrm'
        self.model.write_bytes(model_bytes())

    def test_vrm_versions_thumbnail_metadata_and_persistent_copy(self):
        for vrm1 in (False, True):
            self.model.write_bytes(model_bytes('New' if vrm1 else 'Legacy', vrm1=vrm1))
            entry = self.store.install(self.model)
            self.assertEqual(entry['author'], 'Artist')
            self.assertEqual(entry['rig'], 'humanoid')
            self.assertEqual(Path(entry['path']).parent.joinpath('thumbnail.png').read_bytes(), PNG)
        self.model.unlink()
        rows = AvatarStore(self.store.root).entries()
        self.assertEqual([r['name'] for r in rows], ['Legacy', 'New'])
        self.assertTrue(all(Path(r['path']).is_file() for r in rows))

    def test_duplicate_import_reuses_install_and_keeps_creator_context(self):
        entry = self.store.install(self.model, source=dict(id='rian', source_url='https://booth.pm/ja/items/8165924'))
        other = self.root / 'another-name.vrm'
        other.write_bytes(self.model.read_bytes())
        again = self.store.install(other)
        self.assertEqual(entry['path'], again['path'])
        self.assertEqual(again['source_id'], 'rian')
        self.assertEqual(len(self.store.entries()), 1)
        self.assertFalse(list(self.store.root.glob('.import-*')))

    def test_pending_and_incomplete_installs_are_not_exposed(self):
        pending = self.store.root / '.import-pending'
        pending.mkdir(parents=True)
        (pending / 'model.vrm').write_bytes(model_bytes())
        (pending / 'avatar.json').write_text(json.dumps(dict(file='model.vrm', name='Pending')))
        self.assertEqual(self.store.entries(), [])
        import hashlib
        existing = self.store.root / hashlib.sha256(self.model.read_bytes()).hexdigest()
        existing.mkdir()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.store.install(self.model)

    def test_zip_variants_and_accompanying_notices(self):
        package = self.root / 'download.zip'
        with zipfile.ZipFile(package, 'w') as z:
            z.writestr('outfits/Dressed.vrm', model_bytes('Dressed'))
            z.writestr('outfits/Casual.vrm', model_bytes('Casual'))
            z.writestr('terms/LICENSE.txt', 'Private user copy, creator retains copyright.')
            z.writestr('unneeded/texture.png', PNG)
        self.assertEqual(self.store.archive_models(package), ['outfits/Casual.vrm', 'outfits/Dressed.vrm'])
        with self.assertRaisesRegex(ValueError, 'Choose a model'):
            self.store.install(package)
        entry = self.store.install(package, member='outfits/Dressed.vrm')
        installed = Path(entry['path']).parent
        self.assertEqual(entry['name'], 'Dressed')
        self.assertEqual(len(list((installed / 'notices').glob('*LICENSE.txt'))), 1)
        self.assertFalse((installed / 'unneeded').exists())

    def test_zip_rejects_traversal_symlinks_and_excessive_size(self):
        package = self.root / 'bad.zip'
        for filename in ('../outside.vrm', '/absolute.vrm', 'foo\\outside.vrm', 'C:/drive.vrm'):
            with self.subTest(filename=filename):
                with zipfile.ZipFile(package, 'w') as z:
                    z.writestr(filename, model_bytes())
                with self.assertRaises(ValueError):
                    self.store.archive_models(package)
        with zipfile.ZipFile(package, 'w') as z:
            link = zipfile.ZipInfo('avatar.vrm')
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(link, 'outside.vrm')
        with self.assertRaisesRegex(ValueError, 'symbolic links'):
            self.store.archive_models(package)
        with zipfile.ZipFile(package, 'w') as z:
            z.writestr('avatar.vrm', model_bytes())
        with patch('vrm_overlay.avatar_library.MAX_ARCHIVE', 20):
            with self.assertRaisesRegex(ValueError, 'too large'):
                self.store.archive_models(package)

    def test_gltf_resources_are_copied_and_references_preserved(self):
        source = self.root / 'source'
        source.mkdir()
        (source / 'textures').mkdir()
        (source / 'textures' / 'image with spaces.png').write_bytes(PNG)
        (source / 'mesh.bin').write_bytes(b'geometry')
        path = source / 'local.gltf'
        doc = dict(asset=dict(version='2.0'), meshes=[{}], buffers=[dict(uri='mesh.bin')],
                   images=[dict(uri='textures/image%20with%20spaces.png')])
        path.write_text(json.dumps(doc))
        entry = self.store.install(path)
        target = Path(entry['path']).parent
        self.assertEqual((target / 'textures' / 'image with spaces.png').read_bytes(), PNG)
        self.assertEqual((target / 'mesh.bin').read_bytes(), b'geometry')
        self.assertEqual(entry['name'], 'local')
        # A resource edit is a distinct install even if the glTF JSON is unchanged.
        (source / 'mesh.bin').write_bytes(b'changed geometry')
        self.assertNotEqual(self.store.install(path)['id'], entry['id'])

    def test_gltf_resource_traversal_network_missing_and_metadata_collision(self):
        path = self.root / 'bad.gltf'
        for uri in ('../secret.bin', '%2e%2e/secret.bin', 'https://example.com/mesh.bin', 'missing.bin', 'file:///secret.bin'):
            with self.subTest(uri=uri):
                path.write_text(json.dumps(dict(asset=dict(version='2.0'), meshes=[{}], buffers=[dict(uri=uri)])))
                with self.assertRaises(ValueError):
                    self.store.install(path)
        self.assertFalse(list(self.store.root.glob('.import-*')))
        self.assertEqual(self.store.entries(), [])
        (self.root / 'avatar.json').write_text('resource cannot overwrite import metadata')
        path.write_text(json.dumps(dict(asset=dict(version='2.0'), meshes=[{}], buffers=[dict(uri='avatar.json')])))
        with self.assertRaisesRegex(ValueError, 'conflicts'):
            self.store.install(path)

    def test_invalid_models_and_animation_only_files_do_not_leave_installs(self):
        for payload in (b'not a model', model_bytes(mesh=False), model_bytes()[:-10]):
            self.model.write_bytes(payload)
            with self.assertRaises(ValueError):
                self.store.install(self.model)
        self.assertEqual(self.store.entries(), [])
        self.assertFalse(list(self.store.root.glob('.import-*')))

    def test_cancelled_import_cleans_temporary_files(self):
        cancelled = threading.Event()
        cancelled.set()
        with self.assertRaisesRegex(ValueError, 'cancelled'):
            self.store.install(self.model, cancelled=cancelled)
        self.assertEqual(self.store.entries(), [])
        self.assertFalse(list(self.store.root.glob('.import-*')))

    def test_catalog_has_real_thumbnails_and_creator_links_without_model_downloads(self):
        rows = featured_avatars()
        self.assertTrue(any(e['kind'] == 'bundled' for e in rows))
        self.assertFalse(any('kenney' in e['author'].lower() for e in rows))
        for entry in rows:
            self.assertTrue(entry['thumbnail'])
            self.assertTrue(entry['source_url'].startswith('https://'))
            if entry['kind'] == 'featured':
                self.assertEqual(entry['path'], '')
                self.assertTrue(entry['thumbnail'].startswith('https://booth.pximg.net/'))
        # Hand-edited/corrupt manifests cannot make the library load outside its root.
        folder = self.store.root / 'bad'
        folder.mkdir(parents=True)
        (folder / 'avatar.json').write_text(json.dumps(dict(file='../../original.vrm')))
        self.assertEqual(self.store.entries(), [])


if __name__ == '__main__':
    unittest.main()
