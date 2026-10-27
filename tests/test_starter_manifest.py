"""The versioned starter pack is self-contained and matches its provenance manifest."""
import hashlib
import json
import math
from pathlib import Path
import unittest
from vrm_overlay.avatar_catalog import STARTER,starter_avatars
from vrm_overlay.vrma import load_glb
from vrm_overlay.animation_config import load_motion_properties
from vrm_overlay.asset_build import rotate
from vrm_overlay.vrma import accessor_values


class StarterManifestTests(unittest.TestCase):
    def test_movement_and_floor_pose_families_have_a_baked_side_profile(self):
        side_clips = ('walk', 'walk_formal', 'jog', 'push', 'sit_down', 'sitting_idle',
                      'stand_up', 'get_up', 'lie_down', 'lying_idle', 'climb_up')
        for name in (*side_clips, 'idle', 'idle_talking', 'idle_folded_arms'):
            doc,binary=load_glb(STARTER/f'animations/humanoid/{name}.vrma')
            expected=90 if name in side_clips else 0
            self.assertEqual(doc['extras']['vrmOverlay'].get('facingYaw',0),expected)
            if name in ('walk','push','idle'):
                hips=doc['extensions']['VRMC_vrm_animation']['humanoid']['humanBones']['hips']['node']
                anim=doc['animations'][0]
                channel=next(c for c in anim['channels'] if c['target']==dict(node=hips,path='rotation'))
                q=accessor_values(doc,binary,anim['samplers'][channel['sampler']]['output'])[0]
                forward=rotate(q,(0,0,-1))
                horizontal=math.hypot(forward[0],forward[2])
                self.assertGreater(horizontal,.2)
                self.assertGreater(abs(forward[0] if expected else forward[2])/horizontal,.95)
                self.assertLess(abs(forward[2] if expected else forward[0])/horizontal,.35)

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
