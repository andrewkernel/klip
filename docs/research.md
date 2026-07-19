# Design and capture research

## Product design

The interface direction follows the public `frontend-design` skill in Anthropic's
`anthropics/skills` repository. At review time the repository had about 162,000 stars.
The applied principles are: ground the design in the product's subject, choose one
memorable signature element, keep the surrounding hierarchy disciplined, use a deliberate
palette, and self-critique the result.

Klip's signature element is the replay ring. The midnight/lavender capture deck keeps clip,
record, source, audio, and system-load decisions in one scan path. The settings page separates
format controls from the live dashboard so recording remains the primary action.

Source: https://github.com/anthropics/skills/tree/main/skills/frontend-design

## OBS behavior translated to Klip

OBS documents Display Capture and Game Capture as separate sources. Its Game Capture source
directly captures DirectX/OpenGL and is the most efficient source for games; it supports
fullscreen, a specific window, or a foreground-window hotkey. OBS also exposes a frame-rate
limit and capture-cursor control.

Klip adopts the parts appropriate to a small recorder:

- Explicit **Game / Window** and **Display** modes with source pickers.
- FPS throttling before conversion/encoding.
- Configurable cursor capture.
- Hardware encoder selection, bitrate, resolution, and quality/load presets.
- Visible audio activity and resource/drop metrics.

Klip does not copy OBS's injected game hook, scene graph, streaming stack, browser source,
or plugin architecture. Its game/window mode uses Windows Graphics Capture to reduce code,
security surface, maintenance, and idle overhead. Display mode is the compatibility fallback.

Sources:

- https://obsproject.com/kb/quick-start-guide
- https://obsproject.com/kb/game-capture-source
