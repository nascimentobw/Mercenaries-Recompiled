# Experimental D3D11 visual tooling

This directory is an **opt-in RFC/prototype** for Mercenaries Recompiled. It is
outside the supported game build and is not enabled by a release. Removing the
ReShade add-on restores the normal rendering path.

The contribution is intentionally narrow: a D3D11 texture dump/replacement
add-on, its native fixture, and a local build script. It does **not** implement
native DLSS, DLAA, Frame Generation or Streamline, and it does not bundle game
assets, neural models, ReShade, NVIDIA binaries or other third-party software.

## What this adds

`NativeTextures.cpp` uses the ReShade 6.8 add-on API to observe eligible static
D3D11 shader resources and identify them by SHA-256 over dimensions, DXGI format
and tightly packed mip-0 content. Resource addresses, row padding, paths and
frame numbers are excluded from the key.

The add-on can:

- dump bounded DDS resources for private inspection;
- substitute a compatible, complete-mip DDS at SRV bind time;
- invalidate a substitution when the source receives a partial update;
- toggle replacement with F4 or `texture-mode.txt`;
- record bounded frame/depth diagnostics for compatibility investigation;
- contain callback exceptions and fall back to the original resource path.

The implementation keeps the application's original upload/resource alive. It
limits tracked textures to 8,192, CPU dump storage to 128 MiB and replacement
payload accounting to 512 MiB. Render/depth/UAV targets, arrays/cubes, multisample
resources and unsupported formats are refused.

This prototype assumes one D3D11 game device. Dump I/O is synchronous and should
be treated as a development operation, not a normal-play performance setting.

## Build and focused fixture

Use an x64 Visual Studio 2022 developer shell and separately obtain the official
ReShade 6.8.0 headers. No ReShade binary is vendored here.

```powershell
./Build.ps1 -ReShadeInclude 'D:\Tools\reshade-6.8.0\include'
```

The script builds `NativeTextures.addon64`, builds the fixture and runs it. The
fixture checks:

- stable SHA-256 identity across resource handles and row padding;
- changed source bytes produce a different key;
- RGBA/BGRA/BC1 DDS parsing;
- rejection of truncated/extra payloads, arrays/cubes and incomplete mip chains;
- nested callback guard behavior and exception containment.

These are focused host-side checks, not a rendering/lifetime stress test.

## Local qualification recorded 2026-10-12

The prototype was developed against a local public 1.1.1 installation using the
owner's retail Xbox dump. The current add-on revision was compiled and the native
fixture passed. The contributor also reports gameplay smoke testing across
multiple maps with the visual stack active and no blocking failure observed.
This is **not** a full-campaign, long-session or independent qualification.

The same investigation also exercised an external community DLSS 5 neural-
rendering route. That route is separate from this add-on: DLSS5-Feeder supplied a
synthetic DLAA/NGX contract from ReShade color/depth plus estimated optical-flow
motion vectors, and a neural-rendering consumer evaluated it. The recorded local
session reported successful feature-18 evaluations, finite depth probes, non-zero
motion probes and an approximately 0.998 evaluation-success ratio. Earlier
one-pass samples were around 48 FPS; two stacked passes were around 29-35 FPS on
the recorded setup. Those numbers are observations, not a controlled benchmark.

Recorded test environment for that session: RTX 5060 Ti, driver 617.42, ReShade
6.8.0, DLSS5-Feeder 1.19.0, RenoDX RHI 8.5.0-rc10 and NVIDIA NR 310.8.0 / DLSS
310.9.1. Model/output was 1920x1080; the game was capped at 60. No stable-60 or
Frame Generation claim is made.

No screenshots are committed with this RFC. Visual A/B captures should be added
only when they are matched, reproducible and tied to an exact executable/profile.

## DLSS 5 boundary

Mercenaries Recompiled does not currently make native NGX/DLSS calls. Its Plugin
Interface 1 also deliberately does not expose graphics-device objects or render-
frame/vblank callbacks, so this work cannot simply be expressed through the
current game plugin ABI.

For current community experimentation, use DLSS5-Feeder's own installer and
verification tools as the source of truth instead of copying a version-pinned
preset into this repository:

https://github.com/jlrouzies-fr/DLSS5-Feeder

As checked on 2026-10-12, the feeder recommends LumeniteFX Kernel
(`DLSS5_MV_PROVIDER=3`). The earlier local experiment used VORT (`=2`), which is
still supported but is no longer presented here as the default. Estimated optical
flow is not equivalent to engine-authored motion vectors.

A maintained native DLSS 5 integration would need renderer-owned temporal inputs
and is a separate architectural project. This RFC does not claim to provide it.

## Remaining qualification

Before treating this as release-ready, validate at least:

- device recreation, alt-tab and resolution changes;
- long sessions and repeated mod/vanilla launches;
- texture coverage, atlas edges, alpha/UI resources and memory behavior;
- interaction with other ReShade add-ons;
- moving-camera/vehicle scenes, particles, smoke, explosions and disocclusions;
- matched A/B captures and an independently reproducible test procedure.

## Development provenance

The initial implementation and investigation were AI-assisted with OpenAI Codex
(`gpt-6.1-sol`, reasoning effort `xhigh`). Human input selected the target,
iterated the visual setup and performed the reported gameplay smoke testing.
Before proposing this RFC upstream, ChatGPT GPT-5.6 Sol red-teamed the change
against the current Mercenaries plugin boundary, ReShade 6.8 event signatures and
current DLSS5-Feeder provider semantics. Volatile neural-rendering INIs, the
subjective post-process shader and private texture-pack builder were deliberately
left out of this RFC to keep the upstream diff focused and reduce dependency
and configuration drift.
