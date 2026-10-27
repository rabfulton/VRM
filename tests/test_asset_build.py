import math
import unittest
from unittest.mock import patch
from vrm_overlay.asset_build import MotionSource,append_accessor,humanoid_clip,slerp
from vrm_overlay.vrma import accessor_values


class AssetBuildTests(unittest.TestCase):
    def source(self):
        doc=dict(nodes=[dict(name='root',children=[1]),dict(translation=[0,2,0],children=[2,3,4]),
                        dict(translation=[0,.5,0],children=[5]),dict(translation=[1,.5,0]),
                        dict(translation=[-1,.5,0]),dict(translation=[0,.5,0])],animations=[],accessors=[],bufferViews=[])
        binary=bytearray()
        time=append_accessor(doc,binary,[(0,),(1,)],'SCALAR')
        for name,move in [('A_TPose',False),('Motion',True)]:
            animation=dict(name=name,channels=[],samplers=[])
            for node,kind,values in [(0,'translation',[(0,0,0),(2 if move else 0,0,0)]),
                                      (1,'translation',[(0,2,0),(0,1 if move else 2,0)]),
                                      (2,'rotation',[(0,0,0,1),(0,0,math.sqrt(.5) if move else 0,math.sqrt(.5) if move else 1)])]:
                output=append_accessor(doc,binary,values,'VEC4' if kind=='rotation' else 'VEC3')
                animation['channels'].append(dict(sampler=len(animation['samplers']),target=dict(node=node,path=kind)))
                animation['samplers'].append(dict(input=time,output=output))
            doc['animations'].append(animation)
        return MotionSource(doc,bytes(binary))

    def track(self,doc,binary,bone,path):
        node=next(i for i,n in enumerate(doc['nodes']) if n['name']==bone)
        animation=doc['animations'][0]
        channel=next(c for c in animation['channels'] if c['target']==dict(node=node,path=path))
        return accessor_values(doc,binary,animation['samplers'][channel['sampler']]['output'])

    def test_bake_collapses_helpers_preserves_pose_height_and_removes_root_motion(self):
        source=self.source()
        with patch('vrm_overlay.asset_build.rig_mapping',return_value=dict(hips=1,head=5,leftHand=3,rightHand=4)):
            doc,binary=humanoid_clip(source,'Motion','Test',fps=10)
        hips=self.track(doc,binary,'hips','translation')
        self.assertAlmostEqual(hips[0][1],1)
        self.assertAlmostEqual(hips[-1][1],.5)
        self.assertAlmostEqual(hips[-1][0],0)  # Locomotion root moved two metres.
        head=self.track(doc,binary,'head','rotation')[-1]
        self.assertAlmostEqual(abs(head[2]),math.sqrt(.5),places=5)  # Unmapped helper motion retained.
        self.assertTrue(doc['extras']['vrmOverlay']['normalizedHips'])
        for c in doc['animations'][0]['channels']:
            if c['target']['path']=='rotation':
                for q in accessor_values(doc,binary,doc['animations'][0]['samplers'][c['sampler']]['output']):
                    self.assertAlmostEqual(sum(v*v for v in q),1,places=5)

    def test_reverse_hold_and_shortest_quaternion_interpolation(self):
        source=self.source()
        with patch('vrm_overlay.asset_build.rig_mapping',return_value=dict(hips=1,head=5,leftHand=3,rightHand=4)):
            reverse,data=humanoid_clip(source,'Motion','Reverse',reverse=True)
            held,held_data=humanoid_clip(source,'Motion','Held',hold=True)
        values=self.track(reverse,data,'hips','translation')
        self.assertAlmostEqual(values[0][1],.5);self.assertAlmostEqual(values[-1][1],1)
        held_values=self.track(held,held_data,'hips','translation')
        self.assertTrue(all(row==held_values[0] for row in held_values))
        self.assertEqual(slerp((0,0,0,1),(0,0,0,-1),.5),(0,0,0,1))
