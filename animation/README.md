# Local animation assets

Put your own `.vrma` or animation `.glb` files here, or select another folder in
Studio. `Relax.vrma` is the optional startup clip. Studio's Previous/Next controls
work through the selected library; `make test-ui` tests all files it finds.

Models and animation packs are not bundled with the source. Supply a VRM avatar
with `python3 control.py --model /path/to/avatar.vrm` and select an animation folder
using `--animation-dir /path/to/animations` or Studio's file picker.

This directory's contents are ignored except for this README. Existing local
files and their notices may be kept here. Keep third-party license notices with
those copies and check permission before redistributing an asset. See
[development and integration checks](../docs/development.md) for test setup.
