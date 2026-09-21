# refresh_rate_repro

Minimal reproducer for the null `request_display_refresh_rate` call fixed in the
refresh-rate-null-guard PR.

A single ~200-line OpenXR + Vulkan program, no dependencies beyond Vulkan and an
OpenXR loader. It creates a Vulkan (`XR_KHR_vulkan_enable`) session, enumerates
the display refresh rates, and requests the one the runtime reports.

The point: the rate list and the request go through **different compositors**.
`xrEnumerateDisplayRefreshRatesFB` reads `xsysc->info.refresh_rate_count`, which
the **null-based system compositor** fills (= 1, the panel rate). So a request
for that rate passes the check in `oxr_xrRequestDisplayRefreshRateFB`, then
`oxr_session_request_display_refresh_rate` dispatches to `sess->xcn` — the
**`vk_native` client** compositor, whose `request_display_refresh_rate` is
unassigned.

## Build

```bash
cmake -B build -DOPENXR_INCLUDE_DIR=<sdk>/include -DOPENXR_LOADER_DIR=<sdk>/lib
cmake --build build
```

## Run

```bash
XR_RUNTIME_JSON=<displayxr-runtime>.json ./build/refresh_rate_repro
```

## Expected output

Against a build with the unassigned slot (before the guard):

```
xrEnumerateDisplayRefreshRatesFB -> 1 rate(s): 60.0
xrRequestDisplayRefreshRateFB(60.0) ...
<SIGSEGV — call through null pointer inside the runtime>
```

Against a guarded build:

```
xrEnumerateDisplayRefreshRatesFB -> 1 rate(s): 60.0
xrRequestDisplayRefreshRateFB(60.0) ...
xrRequestDisplayRefreshRateFB returned -8 (no crash)   # XR_ERROR_FEATURE_UNSUPPORTED
```

Portable — the one macOS-specific bit (MoltenVK `VK_KHR_portability_enumeration`)
is guarded and no-ops elsewhere.
