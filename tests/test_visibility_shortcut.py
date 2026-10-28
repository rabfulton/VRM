"""Shortcut parsing and safe Hyprland registration across configuration formats."""
import json
import unittest
from unittest.mock import patch

from vrm_overlay.visibility_shortcut import HyprlandBinding, parse_shortcut


class ShortcutTests(unittest.TestCase):
    def test_chords_are_canonical_and_have_x11_modifier_masks(self):
        self.assertEqual(parse_shortcut(' Control + ALT + Space '), ('Ctrl+Alt+space', 12, 'space'))
        self.assertEqual(parse_shortcut('Super+Shift+F9'), ('Super+Shift+F9', 65, 'F9'))
        self.assertEqual(parse_shortcut('Alt+a'), ('Alt+a', 8, 'a'))
        for invalid in ('a', 'Ctrl+', 'Ctrl+Ctrl+a', 'Ctrl+Control+a', 'Ctrl+a,b',
                        'Ctrl+Alt+F99', 'Ctrl+$(touch /tmp/bad)', 'Hyper+a'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                parse_shortcut(invalid)

    def test_lua_uses_owned_handle_and_quoted_command(self):
        binding = HyprlandBinding(12, 'space', '/tmp/quote\' and ü/companion.sock')
        calls = []
        def run(*args):
            calls.append(args)
            return '[]' if args == ('-j', 'binds') else 'ok'
        with patch('vrm_overlay.visibility_shortcut.hyprctl', side_effect=run):
            binding.install()
            binding.remove()
        self.assertTrue(binding.lua)
        code = next(args[1] for args in calls if args[0] == 'eval' and 'hl.bind(' in args[1])
        self.assertIn('hl.dsp.exec_cmd(', code)
        self.assertIn('description=', code)
        self.assertNotIn('hl.unbind(', '\n'.join(str(c) for c in calls))
        self.assertIn(':remove()', calls[-1][1])
        self.assertIn('--toggle-avatar', binding.command)

    def test_conflict_does_not_mutate_existing_bindings(self):
        binding = HyprlandBinding(12, 'space', '/tmp/companion.sock')
        with patch('vrm_overlay.visibility_shortcut.hyprctl', return_value=json.dumps([
                dict(modmask=12, key='SPACE', description='User launcher', arg='launcher')])) as run:
            with self.assertRaisesRegex(ValueError, 'already bound'):
                binding.install()
            binding.remove()
            run.assert_called_once_with('-j', 'binds')

    def test_legacy_binding_cleanup_preserves_a_new_user_binding(self):
        binding = HyprlandBinding(64, 'F9', '/tmp/companion.sock')
        calls = []
        def run(*args):
            calls.append(args)
            if args == ('-j', 'binds'):
                return '[]' if not binding.installed else json.dumps([
                    dict(modmask=64, key='F9', description=binding.marker, arg=binding.command),
                    dict(modmask=64, key='F9', description='Added by user', arg='other')])
            if args[0] == 'eval':
                raise ValueError('Lua is not the active configuration provider')
            return 'ok'
        with patch('vrm_overlay.visibility_shortcut.hyprctl', side_effect=run):
            binding.install()
            binding.remove()
        self.assertFalse(binding.lua)
        self.assertTrue(any(c[:2] == ('keyword', 'bindd') for c in calls))
        self.assertFalse(any(c[:2] == ('keyword', 'unbind') for c in calls))

    def test_legacy_owned_registration_is_recreated_on_reload_then_removed(self):
        binding = HyprlandBinding(12, 'space', '/tmp/companion.sock')
        calls = []
        def run(*args):
            calls.append(args)
            if args == ('-j', 'binds'):
                return json.dumps([dict(modmask=12, key='space', description=binding.marker,
                                        arg=binding.command)]) if binding.installed else '[]'
            if args[0] == 'eval':
                raise ValueError('Unknown command')
            return 'ok'
        with patch('vrm_overlay.visibility_shortcut.hyprctl', side_effect=run):
            binding.install()
            binding.install()
            binding.remove()
        self.assertEqual(sum(c[:2] == ('keyword', 'bindd') for c in calls), 2)
        self.assertEqual(sum(c[:2] == ('keyword', 'unbind') for c in calls), 2)
