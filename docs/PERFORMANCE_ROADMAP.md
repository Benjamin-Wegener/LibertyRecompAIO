# Performance roadmap

Status of 2026-11-09. Numbers are from `gta4_native_callback_phase_log`
(`gta4-native-callback-phases`, `-passes`, `-pass-kinds`, `-resolves`) read from
logcat, same scene, three or more alternating switch runs (the phones throttle
with heat, so single runs are not comparable).

## Where the time goes

| Device | Callback | GPU wait (`fin-acquire-fence`) | CPU (`recording`) |
|---|---|---|---|
| Redmi 13 5G (Adreno 613, Turnip r6), 360p low-end profile | 61-106 ms (heat) | 38-78 ms | 14-23 ms |
| Pixel 9a (Mali), 360p / 720p / default | 12.7 / 22 / 27 ms | 0 ms | 10-20 ms |

* The Redmi is GPU bound; the Pixel is CPU bound.
* 71-80 passes per frame, 45 of them full resolution. 24 passes end because a
  resolve command ends them, 24 because the render target changes
  (`rendering-ends/frame`, `split-reasons/frame`).
* Measured and not worth more work: colour clear folding (~1 ms on gmem, none
  on sysmem), light cap (no gain, car lights go missing), draw distance 0.3
  (GPU -9 ms but CPU +15 ms), lights as separate passes (more passes, slower).

## Done (menu switches)

Resolution, anti-aliasing, frame limit, draw distance, population, shadow
quality and update rate, water and environment reflection rate, reflection AA,
run-ahead, local light limit, colour clear folding, and a LOW-END DEVICE
PROFILE button that sets them together.

## Next, in order

1. **Dead resolves** (`gta4-native-resolves` log). Measured on the Redmi:
   32 resolves per frame, 27 are read later in the frame, 5 are not (the
   present image, needed, and four tiny targets under 0.4 Mpix in total).
   Nothing worth skipping. Closed.
2. **`VK_KHR_dynamic_rendering_local_read`** (Vulkan 1.4 / extension). Read the
   G-buffer on chip instead of loading it (`LLL` x6 per frame) and merge the
   passes that only exist because the G-buffer is sampled. Check first that the
   bundled Turnip and the Mali driver expose it; keep the current path as the
   fallback (rule: newest Vulkan the device has, else 1.2, else 1.1).
   The bundled Turnip (Mesa 26.3, Vulkan 1.4.359) lists
   dynamic_rendering_local_read, attachment_feedback_loop_layout,
   descriptor_buffer, shader_object, graphics_pipeline_library and
   push_descriptor. Confirmed at runtime on the Redmi (`device-extensions`
   log): local_read, feedback_loop_layout, descriptor_buffer,
   graphics_pipeline_library, push_descriptor are present, shader_object is not.
   **Does not fit the measured splits:** local_read lets a draw read an
   attachment its own pass writes. The 24 splits per frame are real render
   target changes (G-buffer, HDR/light, back) and the 24 resolve ends are
   needed (27 of 32 resolves are read), so they stay pass ends. Only worth it
   if a log shows passes sampling what they just wrote. Not started.
   **Measured (`gta4-native-resolve-reopens`, Redmi):** 18-21 passes per frame
   end on a resolve, only 1.8-2.4 of them reopen on the same target (about 3 %
   of 71.5 passes); the other 16-19 switch to a different target. At most two
   passes per frame could ever merge. Closed.
   *Accept:* pass count and fence wait drop on the Redmi; lights, minimap and
   car lights unchanged.
3. **Transient attachments** for depth/colour buffers whose store op is
   DONT_CARE (lazily allocated memory). Mostly a Mali and RAM win.
4. **LTO / PGO for `libmain.so`.** `LIBERTY_LTO=thin ./tools/local_build.sh`
   (option added, off by default). Measure link time, memory, and the CPU
   `recording` time on the Pixel.
5. **Descriptor buffer / push descriptors, shader objects.** CPU cost per
   draw and pipeline hitches; helps the CPU bound Pixel.
6. **Half resolution lights.** Blocked: lights are drawn inside the G-buffer
   and light passes, against the scene depth buffer; there is no separate light
   buffer. Needs a depth downsample and an upsample, only after 2.
7. **R11G11B10 instead of RGBA16F** for intermediate HDR buffers. About 20
   places test for RGBA16F (light colour delta, resolve, HDR packing): audit
   all of them first, then switch behind a cvar.

## Not performance, but required

* Test the USA disc with Title Update 8 on the Nothing Phone 3a (Adreno 810).
* One APK with PAL and USA libraries (`libmain.so`, `libmain_usa.so`) and the
  Turnip bundle; CI builds both libraries (`build-android-apk.yml`).
* Vulkan 1.1 stage A.2 (render-pass fallback without dynamic rendering) and
  stage B (no buffer device address).

## Frame generation on Android (design, not started in code)

Goal: the game renders 30 FPS, the screen shows 60 (or 24 -> 48/120) with less
power than rendering every frame.

What exists:
* `VulkanPresenter::SetGeneratedFrame(generated, real, ...)` presents two images
  per game frame with pacing (`interval_ns`) on a FIFO swapchain. Platform
  independent, so it should work on Android.
* `temporal/vulkan_frame_generation.*` (FSR 3 and DLSS frame generation with
  motion vectors, depth and camera matrices). Desktop only: the Android build
  defines only `REX_HAS_FIDELITYFX_FSR1`, has no FSR 3 SDK and no
  `gta4_native_frame_generation` cvar, and the temporal motion/depth inputs are
  only produced by the temporal upscaler path, which Android does not use.

What is missing: a producer of the generated image for Android.

Contract of the presenter (read from `generated_frame.h`): both images are
immutable FP16 presentation images in `kGuestOutputFormat`, created for the
native/display queue families (concurrent sharing when they differ), submitted
in the same submission as the real image, with a lease kept until the producer
is done, and `epoch`/`interval_ns` (at most 100 ms) set.

Phases, each behind `gta4_native_frame_generation_simple` (default off):
1. Pair presentation with a plain 50 % blend of the previous and the current
   final frame (HUD included). Validates the image contract, the lease and the
   pacing on the Pixel. Expected: visible double images on fast motion.
2. Motion compensation: block matching on half-resolution luma (8x8 blocks, a
   small search window) in one compute pass, then a warp of both frames to the
   middle. The Pixel GPU is idle (fence wait 0 ms), a Mali-G715 should afford
   about 1-2 ms. Fallback to the blend (or a duplicate) when the match is bad.
3. HUD and minimap: either interpolate before the HUD (needs the scene/HUD split
   the desktop path has) or mask the HUD region; a plain interpolation smears
   the rotating minimap.
4. Menu: a switch next to the frame limit, offered only when the screen refresh
   rate is a multiple of twice the game rate (60 Hz screen: 30 -> 60; 120 Hz:
   30 -> 60 or 24 -> 48 on VRR).

Costs and risks: one frame (33 ms at 30 FPS) of extra input delay; extra GPU
time and power for the extra images and the compute pass (the saving only
exists compared with rendering every frame, not compared with 30 FPS); wrong
interpolation on transparent effects, particles and the HUD. Not useful on the
Redmi: it is GPU bound.

### Phase 1 result (v63, Pixel 9a, `gta4_native_frame_generation_simple`)
The pair path works: no crash or validation error, the display receives two
images per game frame (21-23 presents/s at 11-12 game frames/s), and the copies
cost nothing (callback 26-28 ms, same as off). But it throttles the game from
24-30 to 10-12 frames per second: `pipeline: handoff-wait` rises from 24.5 to
66.7 ms per frame, i.e. the presenter only takes the next guest output image
after a pair has been shown (generated, hold `interval_ns`, real). The interval
is measured from the time between two encodes, so a slow frame lengthens the
next interval (feedback).
Next: read the presenter's paint cadence (`vulkan_presenter.cpp` around the
generated-frame delivery stages) and try a fixed interval of half a refresh
period instead of the measured one, a pair per display cycle at 60 Hz, and
whether the refresher callback can run ahead while a pair is still shown.
Not usable yet; the cvar stays off.

Follow-up measurements (same build, generation on): game 10-12 frames/s and
18-24 display presents/s at frame limits 30, 60 and 0, so the limit is not the
cause. The presenter plans every present (generated and real) on the frame
pacer with `max(1 s / limit, interval)`, and it admits the next scene only after
the generated image was presented (`AdmitPacedPublication`); the next guest
frame (about 25 ms) plus its render callback (about 28 ms) then run one after
the other and are quantised to display slots, which gives about 90 ms per pair.
The remedy is on the presenter side: admit the next scene earlier or let the
refresher run ahead while a pair is still shown. Needs presenter timing logs
(`diagnostics` category `presenter` prints `delivery=generated|real` with the
interval) before changing it.

### Phase 1 fixed (v68): steady pairs
`presenter_delivery_log` (new, off) showed the cause: the hold time between the
generated and the real image was half the measured time between two encodes,
and the encodes arrived 130 ms / 70 ms alternately, so the interval flipped
between 33 and 65 ms (about 98 ms per pair on average). A hold time that follows
the measured frame time cannot work (a frame time contains the hold time of its
own pair, so the estimate only drifts upwards; tried a smoothed and a decaying
version, both held the game below its pace). The hold time is now fixed from the
frame limit (`1 s / limit`: the limit is the rate of the screen, one slot per
image; 60 Hz when unlimited). Measured on the Pixel 9a from the millisecond
timestamps of the presents (an earlier table was rounded to whole seconds by awk
in a German locale; use `LC_ALL=C`):

| generation | limit | game | shown |
|---|---|---|---|
| on | 30 | 15.0/s | 30.1/s |
| on | 60 | 25.1/s | 50.0/s |
| on | 0 | 24.8/s | 49.6/s |
| off | 60 | 41.9/s | 41.9/s |
| off | 30 | 30.1/s | 30.1/s |

The 15 -> 30 case is exact. At limit 60 the game only reaches 25 although it
runs 42 without generation: a pair needs at least two display slots and the next
scene is admitted only after the generated image was shown, so guest frame and
render still run one after the other. The generated image is still a copy of the
previous frame; phase 2 (motion compensated interpolation) makes it smooth.

### Outcome: plain blend only (release decision)
Phase 2 (v69-v74: three, later four compute passes of block matching and warp,
an adaptive 16/8 px block size, a per-pixel check against standing still) was
built and measured, then dropped:
* Looks: block-matched warps turned thin structures (girders, fences, wires) and
  the HUD into wobbling smears ("video effect"); the per-pixel check removed the
  HUD artefacts but not the smearing of structures. Good results need real object
  motion vectors, which Android does not produce (see below).
* Power: Pixel 9a, frame limit 30, battery current with generation on and off,
  alternating, 3 x 15 samples in a street scene: about 5.0 A and 5.0 A (no
  difference; earlier pairs in other scenes were equal or worse with generation).
  The Pixel is not at its limit at 30 FPS, so nothing is gained.
* Not measured: the GPU time of the passes, and a phone that cannot reach 30 FPS.
Kept: a one-pass blend (`simple_frame_generation.comp`, 50 % of the previous and
the current frame) behind `gta4_native_frame_generation_simple` (off), shown in
the launcher as "Frame generation (alpha, not finished)" on the Experimental tab.
The motion-compensated code and the "generated only" debug switch are gone from
the tree (history: commits 8983e2e2 and later on this branch).

Where real motion vectors would come from: the native renderer already writes
per-object motion and depth (`geometry_history.h`, `vulkan_scene.cpp`, extra
colour attachments) for the desktop FSR 3 / DLSS path, but only after the guest
sends a temporal query (`graphics_system.cpp` near `temporal_query_selections_`).
Nobody sends it on Android, and the Android build has FSR 1 only (no FSR 3 SDK).
Enabling that path and feeding the vectors to the compute pass (or the FSR 3 SDK)
is the way to a good result; it costs extra render targets on a tile-based GPU.
Not started.
