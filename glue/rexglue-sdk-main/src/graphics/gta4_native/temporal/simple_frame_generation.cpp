#include "simple_frame_generation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

#include <rex/logging.h>
#include <rex/ui/vulkan/util.h>

#include "simple_frame_generation_spv.h"

namespace rex::graphics::gta4_native::temporal {
namespace {
constexpr VkFormat kFormat = ui::vulkan::VulkanPresenter::kGuestOutputFormat;
// The pair is shared with the presenter (sampled there) and written by compute.
constexpr VkImageUsageFlags kSharedUsage = VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
// Mailboxes + display submissions in flight + the pair being encoded.
constexpr size_t kMaximumPairs = 8;
constexpr uint64_t kMaximumIntervalNs = 100'000'000;

bool WaitIdle(const ui::vulkan::VulkanDevice& device) {
  std::vector<ui::vulkan::VulkanDevice::Queue::Acquisition> locks;
  for (const auto& family : device.queue_families())
    for (const auto& queue : family.queues) locks.emplace_back(queue->Acquire());
  return device.functions().vkDeviceWaitIdle(device.device()) == VK_SUCCESS;
}

struct PrivateImage {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

bool CreatePrivateImage(const ui::vulkan::VulkanDevice& device, VkExtent2D extent,
                        VkImageUsageFlags usage, PrivateImage& out) {
  VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  create.imageType = VK_IMAGE_TYPE_2D;
  create.format = kFormat;
  create.extent = {extent.width, extent.height, 1};
  create.mipLevels = create.arrayLayers = 1;
  create.samples = VK_SAMPLE_COUNT_1_BIT;
  create.tiling = VK_IMAGE_TILING_OPTIMAL;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          &device, create, ui::vulkan::util::MemoryPurpose::kDeviceLocal, out.image, out.memory)) {
    return false;
  }
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = out.image;
  view.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view.format = kFormat;
  view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  return device.functions().vkCreateImageView(device.device(), &view, nullptr, &out.view) ==
         VK_SUCCESS;
}

void DestroyPrivateImage(const ui::vulkan::VulkanDevice& device, PrivateImage& image) {
  const auto& fn = device.functions();
  if (image.view) fn.vkDestroyImageView(device.device(), image.view, nullptr);
  if (image.image) fn.vkDestroyImage(device.device(), image.image, nullptr);
  if (image.memory) fn.vkFreeMemory(device.device(), image.memory, nullptr);
  image = {};
}

// Everything the pairs' descriptor sets point at besides the pair itself.
struct ComputeInputs {
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  std::array<VkImageView, 2> cur{};   // the two private frame copies, in turn
};

// A generated and a real image, shared with the presenter, and the two
// descriptor sets (one per parity of the private frame copies) for the compute
// passes that write the generated image. The lease is the shared_ptr to this
// object: the pool reuses it only when no one else (frame slot, presenter
// mailbox, display submission) still holds it.
struct ImagePair {
  const ui::vulkan::VulkanDevice* device = nullptr;
  std::array<VkImage, 2> image{};        // 0 generated, 1 real
  std::array<VkImageView, 2> view{};
  std::array<VkDeviceMemory, 2> memory{};
  std::array<VkDescriptorSet, 2> sets{};
  VkExtent2D extent{};

  ~ImagePair() {
    if (!device) return;
    const auto& fn = device->functions();
    for (size_t i = 0; i < image.size(); ++i) {
      if (view[i]) fn.vkDestroyImageView(device->device(), view[i], nullptr);
      if (image[i]) fn.vkDestroyImage(device->device(), image[i], nullptr);
      if (memory[i]) fn.vkFreeMemory(device->device(), memory[i], nullptr);
    }
  }

  static std::shared_ptr<ImagePair> Create(const ui::vulkan::VulkanDevice& device,
                                           VkExtent2D extent, const ComputeInputs& inputs) {
    auto pair = std::make_shared<ImagePair>();
    pair->device = &device;
    pair->extent = extent;
    const std::array families{device.queue_family_graphics_compute(),
                              device.queue_family_native_offscreen()};
    const bool concurrent = families.front() != families.back();
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = kFormat;
    create.extent = {extent.width, extent.height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = kSharedUsage;
    create.sharingMode = concurrent ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    create.queueFamilyIndexCount = concurrent ? uint32_t(families.size()) : 0;
    create.pQueueFamilyIndices = concurrent ? families.data() : nullptr;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    const auto& fn = device.functions();
    for (size_t i = 0; i < pair->image.size(); ++i) {
      if (!ui::vulkan::util::CreateDedicatedAllocationImage(
              &device, create, ui::vulkan::util::MemoryPurpose::kDeviceLocal, pair->image[i],
              pair->memory[i])) {
        return {};
      }
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = pair->image[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = kFormat;
      view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if (fn.vkCreateImageView(device.device(), &view, nullptr, &pair->view[i]) != VK_SUCCESS) {
        return {};
      }
    }
    // Two sets: with parity p the current frame is private copy p, the previous
    // one copy 1-p.
    const std::array<VkDescriptorSetLayout, 2> layouts{inputs.set_layout, inputs.set_layout};
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = inputs.pool;
    allocate.descriptorSetCount = 2;
    allocate.pSetLayouts = layouts.data();
    if (fn.vkAllocateDescriptorSets(device.device(), &allocate, pair->sets.data()) != VK_SUCCESS) {
      return {};
    }
    for (uint32_t parity = 0; parity < 2; ++parity) {
      const VkDescriptorImageInfo sampled_prev{inputs.sampler, inputs.cur[1 - parity],
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      const VkDescriptorImageInfo sampled_cur{inputs.sampler, inputs.cur[parity],
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      const VkDescriptorImageInfo store_generated{VK_NULL_HANDLE, pair->view[0],
                                                  VK_IMAGE_LAYOUT_GENERAL};
      const std::array<const VkDescriptorImageInfo*, 3> infos{&sampled_prev, &sampled_cur,
                                                              &store_generated};
      std::array<VkWriteDescriptorSet, 3> writes{};
      for (uint32_t b = 0; b < 3; ++b) {
        writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[b].dstSet = pair->sets[parity];
        writes[b].dstBinding = b;
        writes[b].descriptorCount = 1;
        writes[b].descriptorType = b < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                         : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[b].pImageInfo = infos[b];
      }
      fn.vkUpdateDescriptorSets(device.device(), uint32_t(writes.size()), writes.data(), 0,
                                nullptr);
    }
    return pair;
  }
};

struct PushConstants {
  int32_t full[2];
};
}  // namespace

struct SimpleFrameGeneration::Impl {
  const ui::vulkan::VulkanDevice* device = nullptr;
  VkExtent2D extent{};
  bool failed = false;
  // Compute objects
  VkShaderModule module = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  ComputeInputs inputs;
  // Private images: two copies of the final frame in turn, small work images.
  std::array<PrivateImage, 2> cur;
  std::array<VkImageLayout, 2> cur_layout{VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
  uint32_t parity = 0;
  bool have_prev = false;

  std::vector<std::shared_ptr<ImagePair>> pool;
  std::shared_ptr<ImagePair> pending;
  bool pending_generated = false;
  uint64_t pending_epoch = 0, pending_interval_ns = 0;
  std::chrono::steady_clock::time_point last_encode{};

  void DestroyAll() {
    pending.reset();
    pool.clear();  // the pairs go first: their descriptor sets live in inputs.pool
    if (device) {
      const auto& fn = device->functions();
      DestroyPrivateImage(*device, cur[0]);
      DestroyPrivateImage(*device, cur[1]);
      if (inputs.pool) fn.vkDestroyDescriptorPool(device->device(), inputs.pool, nullptr);
      if (inputs.sampler) fn.vkDestroySampler(device->device(), inputs.sampler, nullptr);
      if (inputs.set_layout)
        fn.vkDestroyDescriptorSetLayout(device->device(), inputs.set_layout, nullptr);
      if (pipeline) fn.vkDestroyPipeline(device->device(), pipeline, nullptr);
      if (pipeline_layout) fn.vkDestroyPipelineLayout(device->device(), pipeline_layout, nullptr);
      if (module) fn.vkDestroyShaderModule(device->device(), module, nullptr);
    }
    inputs = {};
    pipeline = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    module = VK_NULL_HANDLE;
    cur_layout = {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED};
    have_prev = false;
    parity = 0;
  }

  bool Initialize() {
    const auto& fn = device->functions();
    if (!CreatePrivateImage(*device, extent, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cur[0]) ||
        !CreatePrivateImage(*device, extent, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, cur[1])) {
      return false;
    }
    VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    module_info.codeSize = sizeof(kSimpleFrameGenerationSpirv);
    module_info.pCode = kSimpleFrameGenerationSpirv;
    if (fn.vkCreateShaderModule(device->device(), &module_info, nullptr, &module) != VK_SUCCESS)
      return false;
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t b = 0; b < 3; ++b) {
      bindings[b].binding = b;
      bindings[b].descriptorCount = 1;
      bindings[b].descriptorType = b < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                         : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      bindings[b].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = uint32_t(bindings.size());
    layout_info.pBindings = bindings.data();
    if (fn.vkCreateDescriptorSetLayout(device->device(), &layout_info, nullptr,
                                       &inputs.set_layout) != VK_SUCCESS)
      return false;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo pipeline_layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &inputs.set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push;
    if (fn.vkCreatePipelineLayout(device->device(), &pipeline_layout_info, nullptr,
                                  &pipeline_layout) != VK_SUCCESS)
      return false;
    VkComputePipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline_info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = module;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout;
    if (fn.vkCreateComputePipelines(device->device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                    &pipeline) != VK_SUCCESS)
      return false;
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = sampler_info.addressModeV = sampler_info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (fn.vkCreateSampler(device->device(), &sampler_info, nullptr, &inputs.sampler) != VK_SUCCESS)
      return false;
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(kMaximumPairs * 2 * 2)},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, uint32_t(kMaximumPairs * 2)}}};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = uint32_t(kMaximumPairs * 2);
    pool_info.poolSizeCount = uint32_t(sizes.size());
    pool_info.pPoolSizes = sizes.data();
    if (fn.vkCreateDescriptorPool(device->device(), &pool_info, nullptr, &inputs.pool) != VK_SUCCESS)
      return false;
    inputs.cur = {cur[0].view, cur[1].view};
    return true;
  }

  std::shared_ptr<ImagePair> Acquire() {
    for (const auto& pair : pool) {
      if (pair.use_count() == 1) return pair;
    }
    if (pool.size() >= kMaximumPairs) return {};
    auto pair = ImagePair::Create(*device, extent, inputs);
    if (pair) pool.push_back(pair);
    return pair;
  }
};

SimpleFrameGeneration::SimpleFrameGeneration() : impl_(std::make_unique<Impl>()) {}
SimpleFrameGeneration::~SimpleFrameGeneration() = default;

bool SimpleFrameGeneration::Encode(const ui::vulkan::VulkanDevice& device, VkCommandBuffer commands,
                                   VkImage source, VkExtent2D extent, VkImageLayout source_layout,
                                   VkPipelineStageFlags source_stage, VkAccessFlags source_access,
                                   uint64_t epoch, uint64_t fixed_interval_ns) {
  Impl& s = *impl_;
  s.pending.reset();
  s.pending_generated = false;
  if (!commands || !source || !extent.width || !extent.height || s.failed) return false;
  if (s.device != &device || s.extent.width != extent.width || s.extent.height != extent.height) {
    if (s.device && !WaitIdle(*s.device)) return false;
    s.DestroyAll();
    s.device = &device;
    s.extent = extent;
    if (!s.Initialize()) {
      REXLOG_WARN("simple frame generation: could not create its compute objects; disabled");
      s.DestroyAll();
      s.failed = true;
      return false;
    }
  }
  const auto now = std::chrono::steady_clock::now();
  uint64_t gap_half_ns = 0;
  if (s.last_encode.time_since_epoch().count()) {
    gap_half_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               now - s.last_encode).count()) / 2;
  }
  s.last_encode = now;
  // A long stall or the first frame: start over, this frame is shown alone.
  if (!gap_half_ns || gap_half_ns > 3 * kMaximumIntervalNs) s.have_prev = false;
  // The hold time of the generated image is fixed: the frame limit is the rate of
  // the screen, one display slot per image (60 Hz when unlimited). A hold time
  // derived from the measured frame time does not work: a frame time contains the
  // hold time of its own pair, so every estimate drifts upwards and the game is
  // held below the pace it can keep.
  const uint64_t interval_ns =
      std::min<uint64_t>(fixed_interval_ns ? fixed_interval_ns : 16'666'667, kMaximumIntervalNs);
  auto pair = s.Acquire();
  if (!pair) return false;

  const auto& fn = device.functions();
  const auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to,
                           VkAccessFlags src_access, VkAccessFlags dst_access,
                           VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    fn.vkCmdPipelineBarrier(commands, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
  };
  const auto copy = [&](VkImage from, VkImage to) {
    VkImageCopy region{};
    region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {extent.width, extent.height, 1};
    fn.vkCmdCopyImage(commands, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  };
  constexpr auto kInternalLayout = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
  constexpr auto kInternalStage = ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask;
  constexpr auto kInternalAccess = ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;
  const uint32_t p = s.parity;

  // The final frame becomes readable by transfers.
  barrier(source, source_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source_access,
          VK_ACCESS_TRANSFER_READ_BIT, source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT);
  // real = the final frame
  barrier(pair->image[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
  copy(source, pair->image[1]);
  barrier(pair->image[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kInternalLayout,
          VK_ACCESS_TRANSFER_WRITE_BIT, kInternalAccess, VK_PIPELINE_STAGE_TRANSFER_BIT,
          kInternalStage);
  // The private copy of the final frame (the "current" frame of this call and the
  // "previous" frame of the next one).
  if (s.cur_layout[p] == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier(s.cur[p].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
  } else {
    barrier(s.cur[p].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
  }
  copy(source, s.cur[p].image);
  barrier(s.cur[p].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  s.cur_layout[p] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  if (s.have_prev) {
    barrier(pair->image[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0,
            VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    fn.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline);
    fn.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipeline_layout, 0, 1,
                               &pair->sets[p], 0, nullptr);
    const PushConstants push{{int32_t(extent.width), int32_t(extent.height)}};
    fn.vkCmdPushConstants(commands, s.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(push), &push);
    fn.vkCmdDispatch(commands, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
    barrier(pair->image[0], VK_IMAGE_LAYOUT_GENERAL, kInternalLayout,
            VK_ACCESS_SHADER_WRITE_BIT, kInternalAccess, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            kInternalStage);
  }
  // The presenter image goes back to the layout its release barrier expects.
  barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source_layout, 0, source_access,
          VK_PIPELINE_STAGE_TRANSFER_BIT, source_stage);

  s.pending = pair;
  s.pending_generated = s.have_prev;
  s.pending_epoch = epoch;
  s.pending_interval_ns = interval_ns;
  s.have_prev = true;
  s.parity ^= 1u;
  return true;
}

std::shared_ptr<void> SimpleFrameGeneration::pending_lease() const { return impl_->pending; }

bool SimpleFrameGeneration::Publish(
    ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext& context) {
  Impl& s = *impl_;
  if (!s.pending || !s.pending_generated || !context.image_access_submitted()) return false;
  const auto& p = *s.pending;
  const bool ok = context.SetGeneratedFrame(p.image[0], p.view[0], p.image[1], p.view[1], p.extent,
                                            s.pending, s.pending_epoch, s.pending_interval_ns);
  s.pending.reset();
  s.pending_generated = false;
  return ok;
}

void SimpleFrameGeneration::Discard() {
  impl_->pending.reset();
  impl_->pending_generated = false;
  impl_->have_prev = false;
}

void SimpleFrameGeneration::Shutdown() {
  Impl& s = *impl_;
  if (s.device && !WaitIdle(*s.device)) return;
  s.DestroyAll();
}

}  // namespace rex::graphics::gta4_native::temporal
