# ADR-004 — Borderless fullscreen as the default window mode

**Status:** accepted · **Date:** 2026-11-21

## Context

The owner asked for the game to start borderless fullscreen as standard. AGENTS.md R12 leaves window style to an ADR. Until now the window opened as a framed 1920×1080 client. On a 1080p monitor that does not fit with a frame: Windows clipped the client to 1920×1055 and the window spilled past the screen edges.

## Decision

- **Borderless fullscreen by default.** An interactive session opens a `WS_POPUP` window covering the primary monitor's full rectangle (`rcMonitor`, taskbar included). The swap chain is unchanged — an ordinary flip-model swap chain sized to the client area. **DXGI exclusive fullscreen is never used:** `DXGI_MWA_NO_ALT_ENTER` stays set, and `SetFullscreenState` is never called. Alt+Tab, overlays and multiple monitors therefore behave as they do for any window, and a mode switch is just a window resize, which goes through the existing `WM_SIZE` → `Resize` path.
- **F11 and Alt+Enter switch** between borderless and a framed window, in `MainWindow::SetMode`.
  - Going borderless saves the window's placement, then fits it to the monitor the window is on.
  - Going back restores that placement. The first time back, there is no saved placement, so it uses the initial windowed rectangle.
- **The windowed size** is `--size` (default 1920×1080). If that will not fit in the work area with its frame, it is scaled down with the aspect ratio kept and centred: on a 1920×1080 monitor it becomes a 1729×973 client.
- **`--windowed`** starts in the framed window.
- **The smoke test is always windowed** at exactly `--size`, so its captures and statistics do not depend on the monitor.
- **DPI and display changes** (`WM_DPICHANGED`, `WM_DISPLAYCHANGE`) re-fit a borderless window to its monitor rather than taking the suggested rectangle.

## Verified

The Debug build was driven by a script on a 1920×1080 monitor, with window and client rectangles read through `GetWindowRect`/`GetClientRect` from a per-monitor-DPI-aware process:
- At start, and after each switch back, the window is a popup with no caption whose rectangle equals the monitor's, with a 1920×1080 client.
- F11 and Alt+Enter each switch both ways.
- Started `--windowed`, the client is 1729×973.
- F12 captured the full 1920×1080 frame, and close exited 0 with the debug layer reporting no errors.

The WARP smoke test's image statistics are unchanged by this decision.

## Consequences

- A borderless window shows the desktop's refresh rate and resolution; there is no in-game resolution or refresh-rate choice. That would need exclusive fullscreen or a scaled render target, and it is not settled here.
- The window starts on the primary monitor. Moving it to another monitor means F11 to a window, dragging it there, then F11 again.
