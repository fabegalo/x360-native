# Native Enhanced: timing and title layout

The original setting is the default. Native Enhanced is opt-in, remains
experimental, and does not establish title compatibility or a measured FPS gain.
Execution tests and visual comparisons were deferred by the maintainer during
this implementation. The Native/Vulkan launcher gate remains in place until
validation with a real title.

## Independent timelines

SDK patch 0172 puts the shared state in `rexruntime`, so a title executable and
the GPU plugin observe one timeline. The Native backend must activate it;
setting `native_enhanced` while using Xenos does not activate Native timing.

* `native_framerate`: simulation target, 30–240, including the 30/60/120/144 UI
  presets. It changes simulation only through an identity-checked title profile.
* `native_present_rate`: source presentation cap, 0–480; zero is uncapped.
  A steady-clock deadline paces actual source presents. It never invents frames.
* Guest vblank uses the original video-mode frequency unless a verified title
  selects another interrupt cadence. Disabling host vsync does not accelerate
  guest interrupts in Enhanced mode. `VdQueryVideoMode` is unchanged.
* The guest PPC/system/audio clocks retain their original elapsed-time rate.
  A late simulation frame receives elapsed time rather than running the clock
  faster to satisfy a selected target.

Source-frame notifications happen only after successful Native presentation.
Their recent interval allows a title to avoid generating simulation updates
faster than its renderer consumes them. An idle-render wake is distinct from a
source frame; it cannot inflate source FPS.

## Forza Horizon profile

The hooks belong only to Title ID `4D5309C9`, media `2DC7007B`, version `0.0.0.10`,
whose `default.xex` SHA-256 is
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`.
The host verifies that executable and both original facade modules before
enabling hooks or constructing the runtime. Update overlays require their own
identified AOT profile. Other titles and revisions do not inherit these hooks.

Above 30 FPS, the profile selects two guest vblanks per simulation target frame,
preserving the game's original two-vblank divider. Its main-loop delta uses
elapsed guest ticks without integer-millisecond snapping. Render backpressure
has a bounded wait and an explicit wake when the render loop found no frame.
Baked crowd animation remains 30 Hz. Garage auto-orbit and animated camera paths
use elapsed time at their identified callers; unrelated callers remain original.

The algorithms were adapted from
[ReImagined cb676f7](https://github.com/Dyotson/ForzaHorizon1-ReImagined/tree/cb676f7),
especially `docs/framerate.md`, `src/hooks/frame_time.cpp`,
`src/hooks/crowd_anim.cpp`, and `src/hooks/garage_camera.cpp`. The copied MIT
notice is `titles/forza_horizon/src/LICENSE-ReImagined`. Each hook address,
caller and accessed field was checked against this profile's original
instructions/AOT; the reference's measurements are not measurements of this
implementation. The local Pinyon/ShiftGlue timing references informed the
separation of source presentation and vblank cadence.

## Aspect and UI geometry

Patch 0173 exposes a generic `FrameLayout` aspect declaration. A renderer reads
only `{x,y}` and uses it when refreshing Presenter output. `{0,0}` preserves the
guest aspect. Both Native Enhanced and `native_ultrawide` must be enabled, and
an identified title host must publish geometry. No title identity, shader hash,
depth/pitch classifier or UI heuristic is added to the renderer.

Forza's physical window dimensions select an aspect wider than 16:9. Narrower
windows retain the original aspect and letterboxing. The original viewport
aspect getter `823E22C8` supplies both projection and frustum construction;
1280×720 views use the declared aspect, while smaller reflection/shadow views
retain their own aspect. The gameplay caller `829A48F4` and map caller
`8263AE18` were confirmed in this exact executable.

The guest UI producer `825E6E90` constructs an orthographic projection from
`{2/1280, 2/720, 0.0001, 0.5}` at `82028BA0` and submits it to the UI renderer
at `825E7020`. An AOT mid-assembly hook at `825E6F08` changes only its X scale
before submission. The UI keeps its centered 16:9 safe rectangle and original
vertical/depth behavior. No compiled shader is identified or rewritten to
classify HUD elements. This implements centered UI geometry; it does not move
individual elements to outer screen edges or reauthor prerecorded cinematics.
Source raster dimensions remain controlled by the independent resolution scale.

## Presentation controls

The settings UI persists and applies the following controls on the next launch:
integer internal scale 1–4, anisotropic filtering from the guest or 2/4/8/16×,
FXAA/FXAA Extreme through `swap_post_effect`, bilinear/CAS/FSR1 presentation,
and the existing presenter sharpness parameters. FSR1 is spatial upscaling;
these controls do not claim temporal reconstruction or generated frames.

Native Original explicitly selects the baseline values. Session settings remove
conflicting manifest flags, including hyphen and underscore aliases. Xenos keeps
its existing presentation arguments. Test sources cover persistence, conflicting
arguments, original-mode isolation, timeline invariants and declared-aspect
gating; they were prepared without execution in this stage.
