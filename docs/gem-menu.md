# Radial gem menu

Right-click the avatar to open a constellation of faceted gems. **Talk to me**,
**Studio**, **Always on top**, **Hide avatar**, **Colours** and **Quit** are the
initial actions. The centre closes the main menu and goes back from a submenu.
External-agent mode disables local chat and labels that gem **External agent**.

The orbit opens with a short fade/scale animation, then stays still. Hover and
keyboard focus highlight a gem; a small checkmark indicates active toggles.
Labels have their own dark backing to remain readable over varied desktops.
Right-click again, click empty space/outside, press Escape or switch focus to
close. Tab/Up/Down select native Wayland actions, with Enter/Space to activate;
Qt supports normal Tab/Enter/Space focus traversal. Left/Right change pages.
Menus centre on the click, slide inside the available workarea and scale down
when necessary. They do not move the avatar or its rendering window.

## Avatar colours

The default theme samples the loaded avatar's base-colour materials and textures,
including base-colour factors. It prefers saturated costume/hair/accessory colours
and gives skin-like hues less weight. It never samples the desktop or an avatar
thumbnail, so embedded backgrounds do not become the menu colour scheme.
Neutral, missing or unsupported resources use a rose-quartz fallback.

The work runs on a single background worker. Sampling is bounded to eight images,
small pixel grids, bounded JSON/image data and restricted raster formats. External
texture references must remain inside the model's directory; remote URLs and
parent-directory traversal are not fetched. A stale result cannot recolour a new
avatar. Up to sixteen fingerprinted results are retained in memory, keyed by model
path, size and modification time; no work runs per animation frame.

**Colours** opens six preview gems: Avatar, Rose, Moonstone, Jade, Amber and Pearl.
Selections preview immediately and keep the menu open. The choice is saved per
model path in the existing Studio preferences under `menu/themes/<path hash>`.
Choosing Avatar restores automatic colours. The character assets are unchanged.

## Adding actions

The action registry is application code in `vrm_overlay/qt_gem_menu.py`, separate
from the menu geometry, drawing, palette extraction and renderer actions. Future
features add a declaration and a trusted callback:

```python
from vrm_overlay.qt_gem_menu import MenuAction

controller.gemMenu.register(MenuAction(
    'resume_idle', 'Resume idle', 'play', controller.resumeIdle,
    enabled=lambda: controller.connected and controller.avatarShown,
))
```

Six actions fit on each page. Additional actions automatically create pages with
navigation controls; they keep comfortable hit areas and label sizes. Up to 64
registered actions are supported, including the initial actions and palette choices.
IDs must be unique. Labels are limited to 28 characters and elided visually when
needed; Qt accessibility retains the full label. Icons use the shared path
vocabulary in `menu_design.py`. Enabled/checked conditions are re-evaluated on
controller state changes; call `refresh()` after changes owned by an extension.

To add a submenu, register its children using `group='tools'`, then register a
main action with `target='tools', close=False`. Palette choices demonstrate
callbacks that keep the menu open with `close=False`. The registry is not an
executable-plugin loader; external agent tools remain with their agent harness.

## Two presentations, one contract

Qt Quick draws the X11 menu. Native Wayland retains its renderer-owned
`xdg_popup` and compositor input grab, using the real button serial. Both receive
the same page/action data, palette and normalized vector icon commands. Qt Canvas
and Cairo draw the faceted gems without new graphics dependencies or raster assets.
Native hover/opening redraws use two shared-memory buffers and wait for release
before reusing a buffer. Animation ends after opening; the popup has no continuous
render loop afterward. Output-size/scale changes dismiss an open native popup.

The controller sends `configure_menu` over its existing renderer IPC connection.
The renderer validates the bounded contract before replacing its current menu.
Navigation is local to each presentation. Selected action IDs return through
`menu_action_seq`, `menu_action_id` and `menu_action_context` in renderer state.
The initial sequence is not replayed, model contexts must match and the controller
rechecks enabled state before invoking a known callback. Callbacks and arbitrary
commands never cross the menu IPC contract. Model changes dismiss open menus.

The default spec uses a 448 × 472 logical canvas and at most six actions per page.
Native limits include 16 pages, 64 actions, 32 icons and 64 commands per icon;
requests must also fit the renderer's 64 KiB input limit. Future action additions
within these limits do not require editing either menu's drawing/input code.

## Verification

`make test` covers palette extraction/fallbacks, stale workers, per-model choices,
extra pages, native parsing/hit targets, action validation, small workareas and
Qt interaction. `make test-sanitize` includes the native menu parser. The real
`make test-desktop` check exercises X11 grabs, actual avatar right-clicks and menu
actions. Native Cairo rendering can also be checked offscreen; live Hyprland
popup/focus/scale behavior still needs testing on the Wayland machine.
