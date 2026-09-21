<!--
Copyright 2026, Leia Inc.
SPDX-License-Identifier: BSL-1.0
-->
# Color-management regression matrix (ADR-021)

Manual verification for the encoding-state contract and Model A/B. Closes the
ADR-021 verification gap ("no test app renders true-linear / honest-sRGB"). The
multi-layer-blend and workspace cases need the live display + shell, so this is a
manual procedure, not an automated test.

## Driving the encoding axis

The cube test apps select their color swapchain via `DXR_SWAPCHAIN_ENCODING`
(handled in `common/xr_session_common.cpp::SelectColorSwapchainFormat`):

| Value | Swapchain | Bytes reaching the runtime | Exercises |
|---|---|---|---|
| `srgb` | an advertised `*_SRGB` format | GPU auto-encodes on the per-frame RTV write → **honest encoded**; runtime sets `atlas_holds_srgb_bytes=true` | the **single-layer fast path** (raw, byte-identical atlas) and the **Model B decode leg** when ≥2 such clients blend |
| `unorm` | a plain UNORM format | app writes raw. Since #1589 the runtime reads these as **scene-linear** and encodes them on the way to the atlas — so an app that wrote *encoded* bytes here now looks washed out, and that is the app's bug | the **encode** leg |
| unset | runtime-preferred (`formats[0]`) | unchanged | default behavior |

Set it process-level (the runtime DLL has its own static-CRT env block; use a real
env var, not a run-script line):
```cmd
set DXR_SWAPCHAIN_ENCODING=srgb && test_apps\handle\cube_handle_d3d11_win\build\cube_handle_d3d11_win.exe
```

## Matrix

`{sRGB swapchain, UNORM-encoded, true-linear} × {single-layer opaque, multi-layer blend} × {in-process, IPC, workspace}`

- **Encoding** → `DXR_SWAPCHAIN_ENCODING` (`srgb` / `unorm`). "true-linear" = `unorm` with the renderer writing linear radiance.
- **Single-layer** → one app. **Multi-layer blend** → two overlapping windows under the shell (or a transparent-bg app: `DISPLAYXR_TRANSPARENT_BG=1`).
- **Transport** → in-process (default), IPC (`set XRT_FORCE_MODE=ipc` + running `displayxr-service.exe`), workspace (launch under `displayxr-shell.exe`).

Model B engages **only** in the workspace/service multi-layer column with honest-`srgb` clients against a `LINEAR`/`EITHER` DP; every other cell is Model A passthrough (B == A when nothing blends), so the in-process fast path is identical across encodings.

## In-repo DP test double

`sim_display`'s **D3D11** variant declares `XRT_DP_COLOR_EITHER` and applies the
standard sRGB OETF on output when the runtime declares a `LINEAR` atlas
(`out_encode()` in `sim_display_processor_d3d11.cpp`). It is the hardware-free
exerciser of the Model-B encode-at-handoff direction — register the freshly built
`DisplayXR-SimDisplay.dll` and run two `DXR_SWAPCHAIN_ENCODING=srgb` cubes under
the shell. (The VK/GL/Metal sim_display variants are in-process-only and stay
`ENCODED` passthrough.)

## Capture verification

The D3D11 service compositor captures the combined atlas **post-compose, pre-DP**:
```bash
rm -f "$TEMP/workspace_screenshot_atlas.png"
touch "$TEMP/workspace_screenshot_trigger"   # wait ~3s, then read the PNG
```
- **No double-encode (Model A):** the captured atlas bytes must equal the app's output for an `_SRGB` client (no ~2.2× darkening).
- **Model B:** the captured atlas is intentionally **linear** (numerically darker) — the encode happens *after* capture, in the DP — so a dark pre-DP atlas under B is expected, not a regression. Confirm the on-screen (post-DP) result is correct by eyeballing the live display (screenshots during eye-tracking warmup miss UI).

### The #1589 numerical oracle (hardware-free arithmetic, hardware-read bytes)

A **true-linear** value written into a **UNORM** swapchain must arrive in the atlas
encoded. The four acceptance bytes are pinned CPU-side in
`tests/tests_aux_color_encoding.cpp` and read back from the capture PNG:

| linear in a UNORM swapchain | atlas byte (correct) | atlas byte (pre-#1589 passthrough) |
|---|---|---|
| 0.0 | 0 | 0 |
| 0.2 | **124** | 51 |
| 0.5 | **188** | 128 |
| 1.0 | 255 | 255 |

`128` for a linear 0.5 is the **negative control**: the frame did not take the encode.
0.0 and 1.0 are fixed points and prove nothing on their own.

**Which path a frame took** is readable from the log without a debugger:
- one `Color (#1589) [<component>]:` WARN at init states whether
  `DXR_COLOR_LEGACY_UNORM_ENCODED` is on (legacy) or off (format-honest);
- a `Color (#1610) [<component>]: compose target` WARN the first time the private
  `_SRGB`-view target is created, naming its format and the atlas format. **No such
  line ⟹ every frame so far took the fast path**, and the atlas is byte-identical to
  the pre-#1589 runtime — which is what a single-layer `_SRGB` app must show.

**Blend oracle (#1610).** The CTS `SourceAlphaBlending` case on the panel: mid **G 209**
(encoded-space blending gives 164), unpremultiplied **B 160** (vs 90), semi-white over
black ≈ **156** (vs 84).
