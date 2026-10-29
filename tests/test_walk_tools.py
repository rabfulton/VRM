"""Tool schemas reject ambiguous distances before opening a companion socket."""
import unittest

from vrm_overlay.companion import validate_call


class WalkToolTests(unittest.TestCase):
    def test_direction_default_distance_and_finite_range(self):
        validate_call('avatar_walk', dict(direction='left'))
        validate_call('avatar_walk', dict(direction='right', distance=24))
        validate_call('avatar_walk', dict(direction='left', distance=1600.0))
        for args in ({},dict(direction='up'),dict(direction='left',duration=1),
                     *(dict(direction='left',distance=d) for d in (True,False,'240',None,float('nan'),float('inf'),23,1601,-100))):
            with self.subTest(args=args),self.assertRaises(ValueError):
                validate_call('avatar_walk', args)

    def test_status_and_cancel_require_only_motion_id(self):
        for tool in ('avatar_motion_status','avatar_motion_cancel'):
            validate_call(tool, dict(motion_id='walk_123'))
            for args in ({},dict(motion_id=''),dict(motion_id='x',owner='another-agent')):
                with self.subTest(tool=tool,args=args),self.assertRaises(ValueError):
                    validate_call(tool,args)

    def test_absolute_coordinates_accept_small_negative_and_zero_but_cannot_mix_modes(self):
        for x in (-131072,-100,0,.5,900,131072):
            validate_call('avatar_walk', dict(x=x))
        for args in (dict(x=0,direction='left'),dict(x=0,distance=240),dict(distance=240),
                     *(dict(x=x) for x in (True,None,'900',float('nan'),float('inf'),-131073,131073,10**400))):
            with self.subTest(args=args),self.assertRaises(ValueError):
                validate_call('avatar_walk',args)
