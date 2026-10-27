# Starter avatars and motions

Open Studio using the avatar's right-click menu, then **Choose / import avatar…**
in the Avatar panel. Discover includes the anime companion; **Use avatar** selects
its matching animation library and recommended camera framing. Existing user
libraries are left in place. The basic fox remains available by file import or
the launch command below; it is not featured in Discover.

```sh
python3 control.py --studio --model assets/starter/models/anime.vrm
python3 control.py --studio --model assets/starter/models/fox.glb
```

A fresh clone uses the anime avatar when no saved/local avatar exists. An explicit
`--animation-dir` is respected at initial launch. Manually choosing a starter avatar
in Studio selects that avatar's library. LLM access stays opt-in through the existing
library checkboxes; automatic idle/transition coordination uses local metadata.

## Humanoid library

| Clips | Purpose |
| --- | --- |
| `idle`, `idle_talking`, `idle_folded_arms` | Standing idle pool |
| `walk`, `walk_formal`, `jog` | Looping locomotion previews in place |
| `push` | Looping push gesture |
| `sit_down`, `sitting_idle`, `stand_up` | Standing ↔ seated sequence |
| `lie_down`, `lying_idle`, `get_up` | Standing ↔ lying sequence |
| `climb_up` | Short climb-up motion in place |

`lie_down` is a reversed get-up motion and is an initial preview, not an authored
relaxed bedtime animation. `lying_idle` is a held pose. The sitting clip assumes
a seat underneath the character; these assets do not create a chair or attach to
a desktop window. Climbing/walking do not move the overlay across the desktop.
Window movement/contact and a continuous wall-climb loop remain separate work.

These clips normalize hips displacement for VRM 0 humanoids and retarget rotations
through the runtime's humanoid mapping. Full cloth/body collision is still limited;
lying and sitting can reveal clothing issues on some avatars.

## Fox library

The fox has idle, walk, run, eat, dance and positive/negative reaction animations.
It uses its own node hierarchy, with no humanoid retargeting. It has no VRM facial
expression/viseme definitions; speech can play, but this model does not lip-sync.
Humanoid floor locking is unsupported and disabled for its motion files.

## Behaviour metadata

Each clip has the existing loop/startup/idle flags plus `entry_pose`, `exit_pose`
and `floor_contact`. Stable idles are chosen in the current pose: a lying character
stays in its lying idle. Automatic character actions can use one-shot transitions
to reach another pose, e.g. get up before walking. Requesting get-up while already
standing is a no-op. Interrupted/missing routes fail safely. Manual library playback
remains direct for inspection; **Resume idle** returns to automatic behaviour.

Edit these settings with **⋯** beside a library animation. Floor-contact policy
does not change the user's global foot-lock preference. See
[animation properties](../../docs/animation-properties.md).

## Provenance and regeneration

See [NOTICE.md](NOTICE.md), original notices and the source/output hashes in
[manifest.json](manifest.json). All bundled third-party assets have CC0 releases.
The anime model's embedded metadata is preserved verbatim.

```sh
python3 scripts/build_starter_assets.py --download
```

This downloads pinned archives into ignored `build/assets/sources`, checks their
hashes and regenerates the curated outputs using only Python's standard library.
An existing changed output is protected unless `--force` is supplied. `--sources`
accepts an existing cache; `--output` allows a separate review directory.
The converter supports the two pinned Quaternius rig layouts and their linear
animation channels. It is not a general FBX importer. Blender is optional for
user-supplied Mixamo FBX → baked GLB conversion; Mixamo files should remain in a
user library rather than this redistributable pack.

`make test-assets` checks every clip with the real OpenGL renderer. Offline visual
poses can be rendered with `build/tests/asset-preview` (build it with Make):

```sh
build/tests/asset-preview assets/starter/models/anime.vrm assets/starter/animations/humanoid/get_up.vrma 0.8 /tmp/get-up.ppm
```
