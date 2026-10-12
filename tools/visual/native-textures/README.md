# Optional visual tooling (prototype)

These tools are opt-in, outside the game build and not enabled by a release.
They originate in a local public 1.1.1 installation using its owner's Xbox dump.
The updated loader here is **1.1.0-prototype**: compiled and fixture-checked,
but not yet installed or qualified in a campaign. The older local 1.0.0 loader
was used in gameplay. Do not infer qualification of this revision from that.

## Texture dump and replacement

Build from an x64 MSVC developer shell using the official
[ReShade 6.8.0 headers](https://github.com/crosire/reshade/tree/v6.8.0/include):

```powershell
./Build.ps1 -ReShadeInclude 'D:\Tools\reshade-6.8.0\include'
```

The isolated output is under `artifacts/native-textures`; the default build
does not install anything. ReShade's build with add-on support is required.
Before installation, close the game and back up its ReShade files/configuration.
Place the built `NativeTextures.addon64` in your deliberately selected add-on
folder and set these environment variables for the game process only:

- `MERC_NATIVE_TEXTURE_ROOT`: local replacement DDS directory.
- `MERC_NATIVE_DUMP_ROOT`: optional directory for private DDS dumps.
- `MERC_NATIVE_LOG`: optional diagnostics filename (can contain local paths).
- `MERC_NATIVE_CAPTURE_DEPTH`: optional D24 readback diagnostic; can stall the
  GPU and supplies no DLSS input. Leave unset for normal play.

Launch through the normal mod loader. Original game files and GPU uploads are
retained. The add-on substitutes compatible full-resource SRVs at bind time.
The identifier is SHA-256 over four little-endian uint32 values
`[1, width, height, DXGI format]` and tightly packed mip 0 bytes. Allocation
address, row padding, path and frame number do not enter the key. It is a v1
content identifier, not a cross-version asset-name database.

Eligible resources are static, single-layer, single-sample D3D11 2D textures,
16–4096 pixels, supported RGBA/BGRA or BCn formats, excluding render/depth/UAV
targets. Partial updates invalidate substitution. Replacement files use
`<hash>.dds`, preserve aspect ratio, are at least the source size, and include
the complete mip chain. Array/cube, unknown, truncated or oversized data is
refused. Compatible color interpretation is required. CPU dump storage is
bounded to 128 MiB, replacement payloads to 512 MiB total, and tracking to 8,192
textures. The GPU bound is payload accounting, not total driver memory usage.

The prototype handles one D3D11 game device and ignores other devices. It skips
partial mip/layer/reinterpreted SRVs. Nested callbacks and exceptions are
guarded; failed callbacks disable substitution. **F4** toggles replacements
while the game has focus. The optional `texture-mode.txt` accepts `original`,
`enhanced`, or `reload` (reload retries previously missing replacements).
Remove the add-on on the next closed-game launch to undo its behavior.

Disk I/O is synchronous; dumping is a development operation, not a default
performance setting. GPU failure, unload/device-recreation behavior, long
sessions, asset coverage and interaction with other add-ons still require
interactive qualification. The fixture tests hashes, row-padding independence,
DDS parsing/rejection, nested guards and exception containment; it is not a
rendering/lifetime stress test.

## Building private texture derivatives

Install Pillow in a separate tool environment, and supply trusted official
[DirectXTex texconv](https://github.com/microsoft/DirectXTex/releases) binaries.
Optional 4x opaque enhancement uses separately supplied
[Real-ESRGAN NCNN Vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan)
binaries/models, retaining their licenses. Nothing is downloaded by this tool.

```powershell
python ./Build-Local-Pack.py --dumps 'D:\OwnDumps' --output 'D:\NewPrivatePack' `
  --texconv 'D:\Tools\texconv.exe' --method lanczos
```

`--method realesrgan --upscaler ... --models ...` enhances opaque textures.
Alpha textures are skipped unless `--include-alpha` is supplied; then they use
Lanczos and uncompressed RGBA to retain alpha. Supported linear-color DX10 DDSs
receive complete mips. sRGB, signed/HDR/channel formats and large textures need
another format-aware path and are not processed. Existing output packs are
refused. Intermediates and a provenance manifest stay in the private pack.
Review atlas padding, UI sharpness and foliage before using the results. This
does not rebuild fonts as vector glyphs or promise a complete HD remaster.
The local example used 44 private derivatives; none is included here.

## Sharpness and brightness

`NativeSharp.fx` is an optional bounded high-pass sharpener with color, gamma
and shadow controls. Defaults match the player's preferred brighter profile:
strength 0.24, gamma 0.84, shadow lift 0.035. Exact black remains black to preserve
letterboxing. Copy it to your ReShade shader folder and select it deliberately.
`Native-DLSS5.ini` is the local technique order example; selecting only
`NativeSharp` also works without the neural-rendering route.

## Community neural rendering: scope and evidence

The game remains D3D11. This code does **not** integrate native DLSS SR, DLAA,
Frame Generation or Streamline. The separate community route used
[DLSS5 Feeder 1.19.0](https://github.com/jlrouzies-fr/DLSS5-Feeder) and a
RenoDX DLSS5 consumer, with optical-flow estimated motion from
[Vort](https://github.com/vortigern11/vort_Shaders). Feeder forwards color,
estimated motion and generic depth to a private D3D12 consumer path. This is
distinct from renderer-authored object/camera vectors, jitter, exposure and a
HUD-free input. Other providers/consumer versions require separate checking.
The current Feeder guide recommends LumeniteFX Kernel and Deep Fried Chicken;
the Vort/RenoDX combination here records the locally observed route, not a claim
that it is the latest upstream recommendation.

`ReShade-NR-example.ini` contains relative paths and one pass by default. Supply
the third-party components separately, check their releases/licenses, and do
not stack multiple NR consumers/proxies. In the DLSS5 overlay, **Stack passes**
offers 1/2/3/4; one pass was cheaper than two in the local setup. The example
uses **F7** for NR, because Collectibles Tracker uses F6. Set generic depth
selection for your actual target; a guessed resolution filter is not proof of
alignment. DLL presence alone is not successful NR evaluation.

Dated local evidence (2026-10-12): RTX 5060 Ti, driver 617.42, ReShade 6.8.0,
Feeder 1.19.0, RenoDX RHI 8.5.0-rc10, NVIDIA NR 310.8.0 / DLSS 310.9.1. The
consumer logged successful feature-18 evaluations with two stacked passes,
1920×1080 model/output, ENGAGED state, ~0.998 success ratio and no reported
native SEH fault. Feeder motion probes were nonzero; depth probes were finite.
These demonstrate calls/input transport in that session, not motion accuracy,
correct depth alignment, absence of temporal artifacts or official game support.

The local internal setting was 2160p with horizontal AA (5760×2160 depth
canvas), presented at the monitor's 1920×1080. It was **not physical 4K output**.
The game cap was 60; observed two-pass performance was roughly 29–35 FPS and
earlier one-pass samples ~48 FPS. No stable-60 or Frame Generation claim is made.
Screenshots of a moving session illustrate the profile; they are not matched
A/B benchmarks. Review ghosting, UI distortion and depth after camera motion,
vehicles, effects and menus before accepting a community profile.

## Credits and provenance

Own contribution: DDS prototype, private-pack utility, bounded sharp/brightness
shader, examples, fixtures and this guide. Prepared with OpenAI Codex,
`gpt-6.1-sol`, reasoning effort `xhigh`. ReShade, Feeder, RenoDX, Vort, NVIDIA,
DirectXTex, Real-ESRGAN, xboxrecomp, the game and community mods retain their
authors' attribution/licenses. No third-party binary/model or game asset is
bundled. No rights to redistribute private texture/translation derivatives are
granted by this source contribution.
