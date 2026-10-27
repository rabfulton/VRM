from pathlib import Path
import tempfile
import unittest
from vrm_overlay.pose_routes import transition_route,current_pose
from vrm_overlay.animation_config import load_motion_properties,save_properties


class PoseRoutesTests(unittest.TestCase):
    def test_invalid_motion_metadata_is_not_advertised_to_chat(self):
        from vrm_overlay.chat import capabilities
        self.assertEqual(capabilities(dict(model_loaded=True),[dict(name='Broken',path='/unused/Broken.vrma',llm_enabled=True,config_error='bad pose')]),{})

    def test_shortest_safe_route_and_unknown_pose(self):
        entries=[dict(path=name,entry_pose=a,exit_pose=b,loopable=False) for name,a,b in
                 [('rise','lying','sitting'),('stand','sitting','standing'),('getup','lying','standing')]]
        self.assertEqual([e['path'] for e in transition_route(entries,'lying','standing')],['getup'])
        self.assertEqual(transition_route(entries,'standing','standing'),[])
        self.assertIsNone(transition_route(entries,'standing','lying'))
        entries[-1]['config_error']='invalid'
        self.assertEqual([e['path'] for e in transition_route(entries,'lying','standing')],['rise','stand'])
        entries[1]['loopable']=True
        self.assertIsNone(transition_route(entries,'lying','standing'))

    def test_mid_transition_cannot_be_interrupted_with_wrong_pose(self):
        entries=[dict(path='getup',entry_pose='lying',exit_pose='standing')]
        self.assertIsNone(current_pose(dict(animation_path='getup',duration=2,time=1),entries))
        self.assertEqual(current_pose(dict(animation_path='getup',duration=2,time=2),entries),'standing')

    def test_motion_config_round_trip_preserves_tuning_and_flag_only_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'get_up.vrma'
            path.with_suffix('.cfg').write_text('spring.drag=1.4\nfuture.setting=keep\n')
            flags=dict(loopable=False,startup=False,is_idle=False)
            motion=dict(entry_pose='lying',exit_pose='standing',floor_contact=False)
            save_properties(path,flags|motion)
            save_properties(path,flags|dict(is_idle=True))
            self.assertEqual(load_motion_properties(path),motion)
            self.assertIn('spring.drag=1.4',path.with_suffix('.cfg').read_text())
            path.with_suffix('.cfg').write_text('entry_pose=unknown\nfloor_contact=maybe\n')
            self.assertIn('config_error',load_motion_properties(path))
