"""Animation sidecars and the behaviour metadata advertised to chat."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from vrm_overlay.animation_config import load_properties, load_access_properties, save_properties
from vrm_overlay.animation_library import describe_animations
from vrm_overlay.chat import capabilities, system_prompt


class AnimationConfigTests(unittest.TestCase):
    def test_optional_exit_role_round_trips_and_survives_other_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Exit.vrma'
            flags = dict(loopable=False, startup=False, is_idle=False)
            self.assertFalse(load_properties(path)['is_exit'])
            save_properties(path, flags | dict(is_exit=True))
            save_properties(path, flags | dict(llm_enabled=False))
            self.assertTrue(load_properties(path)['is_exit'])
            with self.assertRaises(ValueError):
                save_properties(path, flags | dict(is_exit='yes'))
            path.with_suffix('.cfg').write_text('is_exit=bad\n')
            self.assertIn('config_error', load_properties(path))

    def test_llm_access_sidecar_is_optional_strict_and_preserved_by_flag_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Wave.vrma'
            flags = dict(loopable=False, startup=False, is_idle=False)
            self.assertEqual(load_access_properties(path), {})
            save_properties(path, flags | dict(llm_enabled=True))
            self.assertEqual(load_access_properties(path), dict(llm_enabled=True))
            save_properties(path, flags | dict(is_idle=True))
            self.assertEqual(load_access_properties(path), dict(llm_enabled=True))
            files = [dict(name='Wave', path=str(path), **load_access_properties(path))]
            self.assertTrue(describe_animations(files, {'Wave.vrma': False})[0]['llm_enabled'])
            save_properties(path, flags | dict(llm_enabled=False))
            self.assertFalse(describe_animations([dict(name='Wave', path=str(path), **load_access_properties(path))],
                                                 {'Wave.vrma': True})[0]['llm_enabled'])
            path.with_suffix('.cfg').write_text('llm_enabled=maybe\n')
            self.assertFalse(load_access_properties(path)['llm_enabled'])
            self.assertIn('config_error', load_access_properties(path))
            with self.assertRaises(ValueError):
                save_properties(path, flags | dict(llm_enabled=1))

    def test_master_and_llm_access_are_both_required_for_capabilities(self):
        for enabled in (False, True):
            for llm_enabled in (False, True):
                with self.subTest(enabled=enabled, llm_enabled=llm_enabled):
                    files = [dict(name='Wave', path='/local/Wave.vrma', enabled=enabled, llm_enabled=llm_enabled)]
                    self.assertEqual(bool(capabilities(dict(model_loaded=True), files)), enabled and llm_enabled)

    def test_flags_override_filename_defaults_and_round_trip_without_losing_tuning(self):
        with tempfile.TemporaryDirectory() as directory:
            animation = Path(directory) / "Relax.vrma"
            cfg = animation.with_suffix('.cfg')
            original = '# Motion notes\nspring.drag=1.250\nbounds.world_w=2.123\nloopable=1\nloopable=0 # final value\ncustom.future=keep'
            cfg.write_text(original)
            self.assertFalse(load_properties(animation)['loopable'])
            values = dict(loopable=False, startup=True, is_idle=False)
            save_properties(animation, values)
            self.assertEqual(load_properties(animation), values | dict(is_exit=False))
            text = cfg.read_text()
            self.assertIn('# Motion notes\n', text)
            self.assertIn('spring.drag=1.250\n', text)
            self.assertIn('bounds.world_w=2.123\n', text)
            self.assertIn('custom.future=keep\n', text)
            self.assertEqual(text.count('loopable='), 1)

    def test_atomic_write_failure_keeps_existing_config_and_leaves_no_temporary(self):
        with tempfile.TemporaryDirectory() as directory:
            animation = Path(directory) / 'test.vrma'
            cfg = animation.with_suffix('.cfg')
            cfg.write_text('spring.stiffness=1.250\n')
            with patch('vrm_overlay.animation_config.os.replace', side_effect=PermissionError('fixture')):
                with self.assertRaises(PermissionError):
                    save_properties(animation, dict(loopable=True, startup=False, is_idle=True))
            self.assertEqual(cfg.read_text(), 'spring.stiffness=1.250\n')
            self.assertEqual(list(Path(directory).iterdir()), [cfg])

    def test_defaults_and_invalid_values_are_conservative(self):
        with tempfile.TemporaryDirectory() as directory:
            entrance = Path(directory) / 'Enter_from_left.vrma'
            self.assertEqual(load_properties(entrance), dict(loopable=False, startup=True, is_idle=False, is_exit=False))
            entrance.with_suffix('.cfg').write_text('startup=0\nloopable=1\nis_idle=1 # allowed variation\n')
            self.assertEqual(load_properties(entrance), dict(loopable=True, startup=False, is_idle=True, is_exit=False))
            entrance.with_suffix('.cfg').write_text('loopable=1\nloopable=oops\nstartup=2\n')
            props=load_properties(entrance)
            self.assertFalse(props['loopable'])
            self.assertFalse(props['startup'])
            self.assertIn('config_error', props)

    def test_metadata_controls_llm_loop_and_descriptions_without_granting_access(self):
        files=describe_animations([dict(name='Rest',path='/local/Rest.vrma',loopable=True,startup=False,is_idle=True)])
        self.assertFalse(files[0]['llm_enabled'])
        self.assertEqual(capabilities(dict(model_loaded=True), files), {})
        files[0]['llm_enabled']=True
        available=capabilities(dict(model_loaded=True), files)
        self.assertTrue(available[('animation','rest')].commands[1]['loop'])
        prompt=system_prompt('Test',available,())
        self.assertIn('idle/resting',prompt)
        files[0]['loopable']=False
        self.assertFalse(capabilities(dict(model_loaded=True),files)[('animation','rest')].commands[1]['loop'])
