# Arch packaging and installation

The [PKGBUILD](../packaging/arch/PKGBUILD) builds `domovushka-git` for x86_64 Arch
derivatives, including both renderer backends, Studio and the CC0 starter pack.
Hyprland and Xorg are the maintained desktop targets. The project is MIT licensed;
bundled components retain their own notices.

## Build a package for another machine

From a checkout with ozz sources initialized, build the current working tree:

```sh
python3 scripts/build_arch_package.py -- --syncdeps
sudo pacman -U build/packages/domovushka-git-r*.pkg.tar.zst
domovushka --studio
```

The helper snapshots tracked and non-ignored source files, including uncommitted
changes, plus ozz sources. It runs the PKGBUILD's build, check and package functions
against that snapshot. It excludes ignored local avatars, settings, speech models
and build output. `--syncdeps` lets makepkg install missing build dependencies;
omit it when dependencies are already installed. Output goes to `build/packages/`
or `--output /path/to/folder`. Copy the main `.pkg.tar.zst` to another up-to-date
Arch machine and install with `pacman -U`; the optional debug package is not needed.
Package creation never prepares speech or accesses your API credentials.
Binary packages record the build machine's Python minor version: this package
uses a versioned site-packages directory. If another derivative has a different
Python minor, build from source on that machine rather than copying the binary;
pacman checks the version bounds before installation.

The standalone PKGBUILD fetches GitHub and the pinned ozz submodule. Once these
changes are in the remote repository, it can be used directly:

```sh
cd packaging/arch
makepkg --cleanbuild --syncdeps
```

The working-tree helper is useful before committing or pushing changes. It marks
its package version `.local`. The Git recipe generates a revision/hash version;
there is no tagged stable package yet. Review the recipe and `.SRCINFO` before
publishing to AUR. GitHub Actions package builds are planned for later; packaging
currently runs locally.

## Package and installation layout

The package uses Arch's system Python, PySide6 and keyring. The reusable Python
client remains importable without loading Qt. Checkout launches retain `.venv`
support; the installed launcher uses the system interpreter.

Installed layout with `/usr` as the package prefix:

| Content | Destination |
| --- | --- |
| Launcher and renderer | `/usr/bin/domovushka`, `/usr/bin/vrm-overlay` |
| Python module, QML and thumbnail resources | Python's versioned `site-packages/vrm_overlay/` |
| Read-only starter pack, catalogues and provenance | `/usr/share/domovushka/assets/starter/` |
| Desktop entry | `/usr/share/applications/domovushka.desktop` |
| Desktop icon | `/usr/share/pixmaps/domovushka.png` (bundled CC0 thumbnail) |
| Project and bundled component licence notices | `/usr/share/licenses/domovushka-git/` in the Arch package; `/usr/share/licenses/domovushka/` for manual installs |
| Guides | `/usr/share/doc/domovushka/` |

`make install PREFIX=/usr DESTDIR=/path/to/stage` stages installation without
touching user configuration. `PREFIX` defaults to `/usr/local` for manual installs.
The renderer and ozz honour packager compiler/linker flags. Python, QML and
thumbnail resources are installed together, and install-time paths locate the
renderer and data independently of the working directory. `VRM_RENDERER` and
`VRM_DATA_DIR` override the executable and resource root for testing/staged launches.

## User files and compatibility

Paths follow the [XDG base directory specification](https://specifications.freedesktop.org/basedir/latest/).
Empty or relative config/data overrides use the home-directory defaults. Native
and Python code use the same socket and renderer-preference policy.

| Content | Storage policy |
| --- | --- |
| Renderer preferences | `$XDG_CONFIG_HOME/domovushka/renderer.cfg`, defaulting under `~/.config` |
| Studio/chat/speech preferences | Preserve Qt's existing `vrm-overlay` / `VRM Studio` QSettings identity and XDG config location |
| Imported avatars and generated animation libraries | Preserve Qt AppDataLocation storage under the user's data root |
| Prepared speech engines/models | Preserve `$XDG_DATA_HOME/vrm-overlay/speech` and existing explicit overrides |
| Thumbnails and other reproducible caches | Preserve Qt CacheLocation storage under the user's cache root |
| Future persistent logs/activity, if enabled | `$XDG_STATE_HOME/domovushka/`, defaulting under `~/.local/state` |
| IPC socket and ownership lock | Private `$XDG_RUNTIME_DIR/domovushka/renderer.sock` and `.lock` |
| Future user-authored skills | Proposed `$XDG_DATA_HOME/domovushka/skills/` with enablement/configuration under the config root |

Existing Qt and speech roots are already user locations. Renaming them is not a
prerequisite for installation. Keep the `vrm-overlay.chat` keyring service and
endpoint account IDs so existing credentials continue to work. Package scripts
must not initialize per-user preferences, access keyrings or remove user data.

The renderer supports `--settings PATH` and `VRM_SETTINGS_PATH` (the command-line
option wins). An explicit file's parent directory must already exist. A checkout
renderer imports its sibling legacy `settings.cfg` only when the new default file
does not exist, preserving the original and unrelated fields. Arbitrary settings
in the launch directory are ignored. An installed binary cannot discover an old
checkout: launch the updated checkout once to migrate, or explicitly copy the old
file to the new config location before first use. Existing model, animation,
speech and socket path overrides remain supported.

Renderer, controller, Python client and examples share the new default socket.
`--socket` still selects an explicit path. If XDG_RUNTIME_DIR is unavailable or
invalid, the fallback is `/tmp/domovushka-UID/renderer.sock`. The application checks
runtime-directory ownership and mode 0700, rejects symlink children and checks
Unix-socket path lengths. Scripts using the former `/tmp/vrm-overlay.sock` should
use the Python client default or pass that path explicitly to both processes.

Studio seeds installed starter animations into
`$XDG_DATA_HOME/domovushka/starter/animations/` so behaviour/tuning sidecars remain
writable. Models and packaged defaults stay read-only. Upgrades add missing files
but never replace existing working copies; `.packaged-version` records the most
recent package manifest used for seeding. To adopt changed defaults, back up the
desired working files and remove them before restarting Studio. Existing user
libraries retain their sibling-sidecar workflow. Checkout starter libraries keep
their existing source-tree locations.

## Dependency audit

The PKGBUILD declares direct runtime and build dependencies. Dynamically loaded
audio/GLFW libraries need explicit attention as well as ELF-linked libraries.

| Group | Candidates and reasons |
| --- | --- |
| Native runtime | `glibc`, `libstdc++`, `libgcc`, `libepoxy`, `glfw`, `libx11`, `libxext`, `wayland`, `libglvnd`, `cairo`; GLFW's X11 paths also use `libxcursor`, `libxi`, `libxinerama`, `libxrandr` |
| Studio runtime | `python`, `pyside6`, `qt6-base`, `qt6-declarative`, `qt6-wayland`, `python-keyring`; verify QML imports and platform plugins |
| Audio | `alsa-lib` and `libpulse` for miniaudio's runtime-loaded backends; JACK is optional. A working desktop audio service/device remains a user/session choice |
| Build/check only | `git`, `cmake`, `wayland-protocols`; `desktop-file-utils` for checks. Normal `base-devel` tools are assumed by makepkg; `wayland` supplies wayland-scanner |
| Optional speech | `vulkan-icd-loader` and a user-selected GPU driver for Vulkan inference; CPU remains selectable. The pinned CrispASR bundle supplies its own additional BLAS/OpenMP/Fortran libraries |
| Optional credentials | An unlocked Secret Service/KWallet provider; document backend-specific requirements without forcing a particular desktop or plaintext fallback |
| Future optional music skill | `beets`, `playerctl` and a configured compatible player; not current package dependencies |

Arch currently packages GLFW as `glfw` (the pkg-config module is `glfw3`) and Qt's
Python bindings as `pyside6`. The C++/GCC runtime libraries are now separate
`libstdc++` and `libgcc` packages; do not rely solely on the older `gcc-libs` name.
See the official [GLFW](https://archlinux.org/packages/extra/x86_64/glfw/),
[PySide6](https://archlinux.org/packages/extra/x86_64/pyside6/) and
[keyring](https://archlinux.org/packages/extra/any/python-keyring/) package records.

The code imports only the Python standard library, PySide6 and keyring. It uses
Qt Network directly for chat; there is no HTTP SDK or Python ML stack dependency.
Ozz is built statically from the recorded submodule commit; cglm, tinygltf, cJSON,
stb and miniaudio are vendored. Preserve their licences. List ozz as a VCS source
and populate the submodule from makepkg's fetched source at the superproject
revision, allowing offline build/package steps after source retrieval. Do not
download speech engines/models or regenerate starter assets during package build
or installation; include the audited existing starter pack instead.

The recipe separates `depends`, `makedepends`, `checkdepends` and `optdepends`; see the
[PKGBUILD reference](https://wiki.archlinux.org/title/PKGBUILD), and follow the
[VCS package guidelines](https://wiki.archlinux.org/title/VCS_package_guidelines)
for `pkgver()`, sources and package relationships. The installed licence directory
includes the project's MIT licence plus bundled-library and protocol notices;
the starter pack also retains its provenance and CC0 documents.

The explicit audio, Qt plugin and GLFW extension dependencies can produce
namcap "may not be needed" warnings because these paths include runtime loading
and Python imports. It may also flag the launcher's Python module despite that
module being shipped in the same package; the extracted-install check verifies
the import. Keep the dependencies required by the supported features.

## Acceptance

Build in a clean Arch chroot, run headless checks with offscreen QML enabled, and
inspect the PKGBUILD and package with namcap. Verify contents, dependencies,
desktop entry and notices. Test a staged install from outside the checkout with
isolated XDG roots and read-only program/data directories.

On a real Xorg session, check fresh launch, starter selection, animation property
edits, persistence and Quit. Repeat native Hyprland acceptance on the other machine
when available. Check migration with existing settings, imports and credentials;
check upgrades preserve edits and removal leaves user-owned files intact. Speech
preparation should work only when requested and report missing optional components
clearly. AUR publication follows review of the tested PKGBUILD and generated `.SRCINFO`.

For an isolated real-display check of an installed or extracted package:

```sh
python3 tests/installed_smoke.py --prefix /usr --backend x11
# Or extract first, without installing it:
mkdir -p build/package-stage
bsdtar -xf build/packages/domovushka-git-r*.pkg.tar.zst -C build/package-stage
python3 tests/installed_smoke.py --prefix build/package-stage/usr --backend x11
```

The check loads packaged Python/QML and the real renderer, starts the bundled
avatar, verifies visible pixels and writable starter properties, restarts to check
saved preferences, and quits. User files are temporary; it does not prepare speech
or make API requests. Use `--backend wayland` on the Hyprland machine. Run extraction
with a single package filename if several package versions are present.
