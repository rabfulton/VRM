# Local animation assets

Put your own `.vrma` or animation `.glb` files here, or select another folder in
Studio. `Relax.vrma` is an optional idle; `Enter_from_left.vrma` is recognized as
an optional startup clip. Studio's Previous/Next controls
work through the selected library; `make test-ui` tests all files it finds.

The [CC0 starter pack](../assets/starter/README.md) has a separate versioned
library and two avatars selectable through **Starter avatars…** in Studio.
For additional user content, supply a VRM/glTF avatar
with `./domovushka --model /path/to/avatar.vrm` and select an animation folder
using `--animation-dir /path/to/animations` or Studio's file picker.

This directory's contents are ignored except for this README. Existing local
files and their notices may be kept here. Keep third-party license notices with
those copies and check permission before redistributing an asset. See
[development and integration checks](../docs/development.md) for test setup.
