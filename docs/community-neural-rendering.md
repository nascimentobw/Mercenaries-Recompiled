# Community DLSS 5 neural-rendering notes

This page documents an **external, unsupported community path** for experimenting
with NVIDIA DLSS 5 neural rendering in Mercenaries Recompiled. It does not add
native DLSS, DLAA, frame generation or Streamline to the recomp, and no third-
party binary, model or shader is bundled by this repository.

## Why a feeder is required

Mercenaries Recompiled presents through D3D11, but the game does not make native
NGX/DLSS calls. A neural-rendering add-on that only hooks an existing DLSS call
therefore has nothing to attach to. The community DLSS5-Feeder project solves
that different problem: it creates a synthetic DLAA request from ReShade color,
depth and motion-vector inputs, evaluates it through NGX, and lets a separate
neural-rendering consumer process that evaluation.

Use the upstream DLSS5-Feeder project and its installer/verification scripts as
the source of truth rather than copying binaries or version-specific settings
into this repository:

- https://github.com/jlrouzies-fr/DLSS5-Feeder

As checked on 2026-10-12, the feeder requires exactly one neural-rendering
consumer. Re-check its current instructions before installation and do not load
multiple consumers together.

## Motion vectors and depth

DLSS 5 is most reliable with engine-authored temporal inputs. This recomp does
not currently expose render-frame motion vectors, jitter, exposure or a HUD-free
color buffer through its plugin API. Community feeder setups therefore depend on
ReShade depth plus estimated optical-flow motion vectors. That is useful for
experimentation, but it is not equivalent to a native engine integration.

Do not hard-code the motion-vector provider globally. `DLSS5_MV_PROVIDER` is an
effect preprocessor definition owned by `DLSS5_Feed.fx`. Let the feeder installer
select its supported default unless there is a specific reason to override it.
As checked on 2026-10-12, its installer defaults to LumeniteFX Kernel
(`DLSS5_MV_PROVIDER=3`); VORT remains supported as provider `2`.

If the RenoDX neural-rendering consumer is used, the feeder already manages its
required compatibility flags when they are unset (`EnableHooks=2`,
`NeuralUplift=1`, `NREnableUpscaling=0`). Do not duplicate those values in the
recomp unless the feeder's current documentation explicitly requires it.

## Validation boundaries

A successful NGX feature evaluation only proves that the synthetic request was
accepted and transported. It does **not** prove correct motion, depth alignment,
absence of ghosting, stable performance, UI preservation or official support.
Validate at least:

- camera pans and fast vehicle motion;
- particles, explosions, smoke and flickering light;
- HUD, menus and subtitles;
- disocclusions and thin geometry;
- repeated alt-tab/resolution changes;
- a representative gameplay session rather than a static scene.

Start with one neural-rendering pass. Stacking passes can materially increase
GPU cost and should be treated as an optional image-quality experiment, not a
default profile.

NVIDIA's DLSS 5 developer guidance describes native 3D-guided neural rendering as
using engine-provided motion vectors and other engine context. That remains the
higher-quality architectural target if this port ever gains a maintained native
integration:

- https://developer.nvidia.com/blog/whats-new-for-game-developers-dlss-5-with-3d-guided-neural-rendering-nvidia-ace-updates-and-new-rtx-kit-capabilities

## Contribution provenance

The compatibility investigation that led to this note used an OpenAI Codex
agent (`gpt-6.1-sol`, reasoning effort `xhigh`) for repository inspection and
prototype work. The final upstream scope was red-teamed and reduced to this
documentation-only change with ChatGPT GPT-5.6 Sol. External projects and NVIDIA
components retain their own authorship, licenses and support boundaries.
