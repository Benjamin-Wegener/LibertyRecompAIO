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
