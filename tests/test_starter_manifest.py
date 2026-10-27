"""The versioned starter pack is self-contained and matches its provenance manifest."""
import hashlib
import json
from pathlib import Path
import unittest
from vrm_overlay.avatar_catalog import STARTER,starter_avatars
from vrm_overlay.vrma import load_glb
from vrm_overlay.animation_config import load_motion_properties


class StarterManifestTests(unittest.TestCase):
    def test_manifest_hashes_and_embedded_assets(self):
        manifest=json.loads((STARTER/'manifest.json').read_text())
        self.assertEqual({s['license'] for s in manifest['sources'].values()},{'CC0-1.0'})
        for name,entry in manifest['artifacts'].items():
            path=STARTER/name
            self.assertTrue(path.resolve().is_relative_to(STARTER))
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),entry['sha256'],name)
            if path.suffix not in ('.vrm','.vrma','.glb'):continue
            doc,binary=load_glb(path)
            self.assertTrue(all('uri' not in image for image in doc.get('images',[])),name)
            if path.suffix=='.vrma':
                self.assertNotIn('meshes',doc)
                self.assertGreaterEqual(len(doc['extensions']['VRMC_vrm_animation']['humanoid']['humanBones']),50)
                self.assertEqual(len(doc['animations']),1)
                self.assertNotIn('config_error',load_motion_properties(path))
        self.assertEqual(len(starter_avatars()),2)
        self.assertTrue((STARTER/'CC0-1.0.txt').is_file())
