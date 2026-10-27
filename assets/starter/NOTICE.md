# Starter asset provenance

These asset permissions are independent of the project's source-code licensing:
the starter assets use CC0 1.0. The complete dedication is in [CC0-1.0.txt](CC0-1.0.txt). Credits are
retained as a courtesy; no creator endorses this application.

## Humanoid motions: Quaternius

- Creator: Quaternius (https://quaternius.com/).
- Source: [Universal Animation Library](https://opengameart.org/content/universal-animation-library),
  posted by Quaternius, and [Universal Animation Library 2](https://opengameart.org/content/universal-animation-library-2),
  also posted by Quaternius. Both source pages designate the packs CC0.
- Creator pages: https://quaternius.com/packs/universalanimationlibrary.html and
  https://quaternius.com/packs/universalanimationlibrary2.html.
- Changes: selected clips baked at 30 Hz onto an animation-only humanoid hierarchy,
  helper-bone motion retained, T-pose orientations normalized, hips height normalized,
  locomotion-root translation removed and files renamed. Movement, seated and
  lying families have a baked 90° vertical-axis facing turn, including rotated
  hips displacement; standing social idles retain their original facing.
  `lie_down` reverses
  `LayToIdle`; `lying_idle` holds its initial pose. These two are derived motions,
  not additional original clips supplied by Quaternius.
- The source archives' pinned SHA-256 values, original clip names and generated
  file hashes are recorded in [manifest.json](manifest.json).

## Anime avatar: VRoid Project / pixiv Inc.

- Model: legacy `AvatarSample_D.vrm`, Sendagaya Shibu (β Ver AvatarSample_1).
- Creator's explicit CC0 declaration:
  https://vroid.pixiv.help/hc/en-us/articles/360012381793.
- Creator's sample-model licence overview:
  https://vroid.pixiv.help/hc/en-us/articles/4402614652569.
- Download mirror used: https://opengameart.org/content/vroid-studio-cc0-models,
  which distinguishes these legacy samples from newer similarly named models.
- `models/anime.vrm` is an unmodified copy. Its original Japanese title, author
  and legacy VRoid Hub permission URLs remain embedded. That older file labels
  the embedded licence `Other`; the URLs permit redistribution/modification,
  and the creator's declaration above explicitly releases this sample as CC0.
- This designation does not apply to all modern VRoid presets or Hub avatars.

## Fox: Kenney

- Creator: Kenney (https://kenney.nl/).
- Source: https://kenney.nl/assets/cube-pets, CC0.
- Original archive licence: [KENNEY-LICENSE.txt](KENNEY-LICENSE.txt), identifying
  Cube Pets 2.0 (the source download URL retains an older version suffix).
- Changes: palette PNG embedded into GLB; the model's container is rotated 180°
  and uniformly scaled to 0.65 for the overlay. Skeleton/node names and source
  animation content are retained. Seven individual animation files omit meshes,
  textures and unused tracks. The complete original set remains in `fox.glb`.

The build recipe is [build_starter_assets.py](../../scripts/build_starter_assets.py).
Mixamo assets are not included in this pack.
