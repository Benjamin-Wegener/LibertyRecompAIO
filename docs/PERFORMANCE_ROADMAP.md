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

1. **Dead resolves** (`gta4-native-resolves` log). Count resolves per frame
   whose destination texture no later draw of the frame samples. If many are
   dead and also unused next frame, skip them behind a switch.
   *Accept:* fewer passes per frame, no visual change over a 10 minute drive.
2. **`VK_KHR_dynamic_rendering_local_read`** (Vulkan 1.4 / extension). Read the
   G-buffer on chip instead of loading it (`LLL` x6 per frame) and merge the
   passes that only exist because the G-buffer is sampled. Check first that the
   bundled Turnip and the Mali driver expose it; keep the current path as the
   fallback (rule: newest Vulkan the device has, else 1.2, else 1.1).
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
