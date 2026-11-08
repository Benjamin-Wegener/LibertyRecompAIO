# Supporting Vulkan 1.1 devices (analysis and plan)

Status: **analysis only, nothing implemented yet.** Tracking issue: #8.

**Today the minimum is Vulkan 1.2.** `glue/rexglue-sdk-main/src/ui/vulkan/vulkan_device.cpp`
refuses every device that reports an API version below 1.2 when the native renderer is used
(`with_native_shader_support`, line ~114: "Liberty native shaders require Vulkan 1.2").
The goal is to let devices that only report Vulkan 1.1 run the native renderer as well.

## What the code requires today

Hard requirements (the device is rejected without them):

| Requirement | Where | Why |
|---|---|---|
| API version >= 1.2 | `vulkan_device.cpp` ~114 | the 1.2 feature structs are read directly (`VkPhysicalDeviceVulkan12Features`) |
| `bufferDeviceAddress` | `vulkan_device.cpp` ~527-538 | shaders read constants and buffers through 64-bit pointers (see below) |
| `runtimeDescriptorArray`, `descriptorBindingPartiallyBound` | same check | indexed (bindless-style) texture/sampler arrays |
| `depthClamp`, `sampleRateShading` | ~125-140 | rendering features |

Not required (already optional or have a fallback):

| Feature | Evidence |
|---|---|
| Dynamic rendering (1.3) | only if the caller passes `with_dynamic_rendering`; the native path does not |
| Timeline semaphores | only enabled for the optional separate offscreen queue (`vulkan_device.cpp` ~655-669) |
| `shaderInt64` | lowered to 32-bit pairs when the shader loads (`include/rex/ui/vulkan/spirv_int64_lowering.h`) |
| `shaderClipDistance` | the log says "title clip planes are not applied" and continues |
| Descriptor indexing | `native_descriptor_backend.h` has a policy with a non-indexed backend; `graphics_system.cpp` ~16045 checks `indexed_features_available` |

So the descriptor-indexing rejection in `vulkan_device.cpp` is stricter than the renderer needs.

## Where buffer device addresses are used

- Two buffer allocations with `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`
  (`src/graphics/gta4_native/graphics_system.cpp` ~9075 upload buffer, ~9191 arena blocks) and the
  matching `vkGetBufferDeviceAddress` calls (~9117, ~9232).
- Five shader sources use `buffer_reference` / `PhysicalStorageBuffer`:
  `sun_shafts_ps.glsl`, `texture_detile_cs.glsl`, `native_spirv_ubo_constants.h`,
  `native_spirv_ubo_shared.h`, `native_color_output_spirv.h`.
- The 1.2 entry points the loader needs are listed in `include/rex/ui/vulkan/functions/device_1_2.inc`
  (`vkWaitSemaphores`, `vkGetBufferDeviceAddress`, ...).

## Plan

**Stage A - allow 1.1 devices that have the extensions (small).**
Accept API 1.1 if the device exposes `VK_KHR_buffer_device_address` (and optionally
`VK_EXT_descriptor_indexing`, `VK_KHR_timeline_semaphore`, `VK_KHR_driver_properties`,
`VK_KHR_shader_float_controls`). Read the features with the extension structs
(`VkPhysicalDeviceBufferDeviceAddressFeaturesKHR`, `VkPhysicalDeviceDescriptorIndexingFeaturesEXT`)
instead of `VkPhysicalDeviceVulkan12Features`, enable the extensions at device creation and load
`vkGetBufferDeviceAddressKHR` instead of the core name. Relax the descriptor-indexing check so the
renderer's non-indexed backend is used when the features are missing.
Result: every 1.1-reporting GPU/driver that has BDA runs.

**Stage B - no buffer device address (large).**
Devices without the extension (for example the Mali-G52 class of mid-range phones) need the
`buffer_reference` accesses replaced by ordinary storage-buffer bindings: the five shader sources,
the SPIR-V helper headers and the two allocations above. This changes shaders and the descriptor
layout, so it needs a per-device check that the picture is still identical.

**Stage C - verification.** For each stage: compare a frame capture of the apartment scene and a
driving scene with the Vulkan 1.2 path (pixel diff), and run the shader cache build.

## What is needed from the device owners

The launcher writes the driver status to `Android/data/com.libertyrecomp.aio/files/last_launch.txt`
(`driver status: ... "api_version" ... "features":{...bufferDeviceAddress,runtimeDescriptorArray...}`).
Please attach that file from every 1.1 device (it lists the api version and the three features that
decide between Stage A and Stage B). The reference device so far: Pixel 9a, Mali-G715, API 1.3.
