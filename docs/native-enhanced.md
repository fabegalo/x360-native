# Native Enhanced: timing and title layout

The original setting is the default. Native Enhanced is opt-in, remains
experimental, and does not establish title compatibility or a measured FPS gain.
Execution tests and visual comparisons were deferred by the maintainer during
this implementation. Native/Vulkan is selectable in the launcher under
development access.

## Independent timelines

SDK patch 0172 puts the shared state in `rexruntime`, so a title executable and
the GPU plugin observe one timeline. The Native backend must activate it;
setting `native_enhanced` while using Xenos does not activate Native timing.

* `native_framerate`: target, 30–240, including the 30/60/120/144 UI presets.
  Above 30, every title gets guest vblank at twice the target (a title that
  presents every second vblank reaches it). Titles that advance a fixed step
  per frame run faster; an identity-checked title profile may replace this.
* `native_present_rate`: source presentation cap, 0–480; zero is uncapped.
  A steady-clock deadline paces actual source presents. It never invents frames.
* Guest vblank uses the original video-mode frequency at 30 FPS; above it,
  the generic cadence or a verified title profile selects the interrupt rate. Disabling host vsync does not accelerate
  guest interrupts in Enhanced mode. `VdQueryVideoMode` is unchanged.
* The guest PPC/system/audio clocks retain their original elapsed-time rate.
  A late simulation frame receives elapsed time rather than running the clock
  faster to satisfy a selected target.

Source-frame notifications happen only after successful Native presentation.
Their recent interval allows a title to avoid generating simulation updates
faster than its renderer consumes them. An idle-render wake is distinct from a
source frame; it cannot inflate source FPS.

## Forza Horizon

The dedicated `titles/forza_horizon` profile and its ReImagined-derived hooks
have been removed. Imported content selects the existing generic AOT project,
using the original guest implementations and Xenos by default.
Generic presentation controls remain available; this removal
does not establish correct simulation timing above the original frame rate.
The retained MIT attribution is in `licenses/forza-reimagined.txt`.

## Aspect and UI geometry

Patch 0173 exposes a generic `FrameLayout` aspect declaration. A renderer reads
only `{x,y}` and uses it when refreshing Presenter output. `{0,0}` preserves the
guest aspect. Both Native Enhanced and `native_ultrawide` must be enabled, and
an identified title host must publish geometry. No title identity, shader hash,
depth/pitch classifier or UI heuristic is added to the renderer.

## Presentation controls

The settings UI persists and applies the following controls on the next launch:
integer internal scale 1–4, anisotropic filtering from the guest or 2/4/8/16×,
FXAA/FXAA Extreme through `swap_post_effect`, bilinear/CAS/FSR1 presentation,
and the existing presenter sharpness parameters. FSR1 is spatial upscaling;
these controls do not claim temporal reconstruction or generated frames.

Internal scale above 1× keeps resolve output in GPU-resident scaled memory
(guest address × scale area, the layout of the pinned `*_scaled` kernels),
approved by the maintainer. D3D12 uses reserved buffers with 16 MB tiles,
Vulkan one sparse buffer (full allocation without sparse residency). The CPU
tracks only which 4 KB pages hold current subpixels; CPU or memexport writes
retire a page, and a shader replicates the 1× bytes into retired pages before
use. Textures load through the SDK `texture_load_*_scaled` shaders and the
scanout gathers the scaled front buffer on the GPU; there is no scaled readback.
Only pages a scaled resolve wrote make a texture scaled; replicated pages do not.
Fetch offsets stay in guest texels (`draw_resolution_scaled_texture_offsets`
off, both translators via SDK patch 0267), as on the console, so GPU texture
encoders such as Motocross Madness's DXT1 terrain keep their footprint.

Native Original explicitly selects the baseline values. Session settings remove
conflicting manifest flags, including hyphen and underscore aliases. Xenos keeps
its existing presentation arguments. Test sources cover persistence, conflicting
arguments, original-mode isolation, timeline invariants and declared-aspect
gating; they were prepared without execution in this stage.
