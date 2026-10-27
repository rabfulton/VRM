import math
import unittest
from unittest.mock import patch
from vrm_overlay.asset_build import MotionSource,append_accessor,humanoid_clip,slerp,face_humanoid_clip,rotate
from vrm_overlay.vrma import accessor_values


class AssetBuildTests(unittest.TestCase):
    def test_side_facing_turns_hips_displacement_and_keeps_local_limb_animation(self):
        source = self.source()
        with patch('vrm_overlay.asset_build.rig_mapping',return_value=dict(hips=1,head=5,leftHand=3,rightHand=4)):
            front, binary = humanoid_clip(source, 'Motion', 'Front', fps=10)
            side, data = humanoid_clip(source, 'Motion', 'Side', fps=10, facing_yaw=90)
        self.assertEqual(front['nodes'], side['nodes'])  # Rest/retarget axes must not turn.
        self.assertEqual(self.track(front,binary,'head','rotation'), self.track(side,data,'head','rotation'))
        for q in self.track(side,data,'hips','rotation'):
            forward = rotate(q,(0,0,-1))
            self.assertAlmostEqual(forward[0], -1, places=5)
            self.assertAlmostEqual(forward[2], 0, places=5)
        # Give the hips a horizontal pose displacement, independently of root locomotion.
        from vrm_overlay.asset_build import append_accessor
        mutable = bytearray(binary)
        animation = front['animations'][0]
        channel = next(c for c in animation['channels'] if c['target']['path'] == 'translation')
        sampler = animation['samplers'][channel['sampler']]
        original = self.track(front,binary,'hips','translation')
        displaced = [(p[0]+.1,p[1],p[2]+.2) for p in original]
        sampler['output'] = append_accessor(front,mutable,displaced,'VEC3')
        front['buffers'][0]['byteLength'] = len(mutable)
        side,data = face_humanoid_clip(front,bytes(mutable),90)
        for old,new in zip(displaced,self.track(side,data,'hips','translation')):
            self.assertAlmostEqual(new[0], .2, places=5)
            self.assertAlmostEqual(new[1], old[1], places=5)
            self.assertAlmostEqual(new[2], -.1, places=5)
        self.assertEqual(face_humanoid_clip(side,data,90), (side,data))
        restored,restored_data = face_humanoid_clip(side,data,0)
        for a,b in zip(displaced,self.track(restored,restored_data,'hips','translation')):
            for x,y in zip(a,b): self.assertAlmostEqual(x,y,places=5)
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
