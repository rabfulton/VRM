# Avatar library

Open Studio → **Choose / import avatar…**. The gallery works before connecting to
the renderer. Discover includes the bundled Sendagaya Shibu anime model, plus
creator pages for Rian, Amane and Keiro. Select a card to see its author, terms
summary and import instructions. The basic Kenney fox is still supplied as a
rig/animation example, but is no longer featured.

## Add a companion

For the included model, select its card and **Use avatar**. Its matching motions
and camera profile are applied automatically.

For a creator-hosted model:

1. Select its card and open **Creator’s download page**.
2. Review the current terms, sign in/purchase if required, and download the VRM
   or ZIP in your browser. The app does not handle your BOOTH/pixiv credentials.
3. Choose **Import downloaded file…**, or drop the file onto the dialog. If a ZIP
   contains several models, choose the dressed outfit/variant you want.
4. The model appears under **My avatars**. Select **Use avatar** to switch; while
   disconnected the button connects to the runtime first.

**Import file…** accepts other local models. **Add current avatar to library**
saves the current external model, including one previously opened with `--model`.
Imports copy the model: deleting/moving the downloaded original afterwards does
not remove the installed copy. Reimporting identical content reuses its install.
Importing alone does not interrupt the current avatar or its playback.

## Formats and presentation

- `.vrm`: VRM 0/1 metadata supplies the name, creator and embedded thumbnail.
- `.glb`: self-contained glTF 2.0 with a mesh. Generic rigs require their own
  animation library; humanoid VRMA motions cannot animate arbitrary skeletons.
- `.gltf`: JSON plus local buffers/images. Relative dependencies are copied while
  retaining their paths. Network resources and paths outside the model's folder
  are rejected; export a self-contained GLB if necessary.
- `.zip`: self-contained VRM/GLB candidates, including nested folders. The selected
  model is imported without unpacking unrelated assets; accompanying text/Markdown
  and PDF notices are retained. Extract packages containing a `.gltf` first.

A missing thumbnail displays an initial instead. Creator-hosted previews load
when the dialog opens, with an offline fallback. Imported VRM thumbnails are local.
No additional Python or native dependencies are required.

The bundled anime model has a known camera/motion profile. Other humanoid imports
keep your chosen animation library, except when replacing the bundled fox or an
empty generic-rig library, when the starter humanoid library is selected. A new
generic model starts with an empty library so unrelated humanoid motions do not
play on it. Choose its own library in Studio. Appearance and clothing behaviour
still depend on the model and the renderer's material/physics support; featuring
a creator page does not certify every supplied variant.

## Storage and terms

Imports live under Qt's application data directory, in `avatars/<content hash>/`.
On a normal Linux launch this is usually
`~/.local/share/vrm-overlay/VRM Studio/avatars/` (respecting `XDG_DATA_HOME`). The
dialog shows the actual location. Each install has the model, its resources,
`avatar.json`, an optional thumbnail, and ZIP notices when supplied. QSettings
remembers the selected installed path. Original model bytes/metadata are preserved.

Creator models remain private user downloads. They are neither bundled in this
repository nor fetched through a mirror or an unofficial Hub API. Author credits,
source pages and embedded license links remain accessible when available. The
short terms summaries are navigation aids; the creator's current full terms govern
use. Discover thumbnails stay on the creator's image host rather than being
redistributed in the repository. Adding a featured entry requires a public creator
page, an actual VRM download option, and reviewed terms and preview URLs in
`vrm_overlay/avatar_catalog.py`.

Importing and ZIP inspection run on a worker thread. Failed/cancelled imports
remove their temporary copies. Imports reject unsafe archive paths, symbolic
links, encrypted ZIPs, animation-only files and unsupported external resources.
Models/resources are limited to 512 MB; ZIP contents to 1 GB and 4,096 entries.
