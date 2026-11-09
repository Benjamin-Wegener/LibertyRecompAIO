#include "simple_frame_generation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

#include <rex/ui/vulkan/util.h>

namespace rex::graphics::gta4_native::temporal {
namespace {
constexpr VkFormat kFormat = ui::vulkan::VulkanPresenter::kGuestOutputFormat;
constexpr VkImageUsageFlags kSharedUsage = VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
// Mailboxes + display submissions in flight + the pair being encoded.
constexpr size_t kMaximumPairs = 8;
constexpr uint64_t kMaximumIntervalNs = 100'000'000;

bool WaitIdle(const ui::vulkan::VulkanDevice& device) {
  std::vector<ui::vulkan::VulkanDevice::Queue::Acquisition> locks;
  for (const auto& family : device.queue_families())
    for (const auto& queue : family.queues) locks.emplace_back(queue->Acquire());
  return device.functions().vkDeviceWaitIdle(device.device()) == VK_SUCCESS;
}

// A generated and a real image, shared with the presenter. The lease is the
// shared_ptr to this object: the pool is free to reuse it only when no one else
// (frame slot, presenter mailbox, display submission) still holds it.
struct ImagePair {
  const ui::vulkan::VulkanDevice* device = nullptr;
  std::array<VkImage, 2> image{};        // 0 generated, 1 real
  std::array<VkImageView, 2> view{};
  std::array<VkDeviceMemory, 2> memory{};
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
                                           VkExtent2D extent) {
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
      if (device.functions().vkCreateImageView(device.device(), &view, nullptr, &pair->view[i]) !=
          VK_SUCCESS) {
        return {};
      }
    }
    return pair;
  }
};
}  // namespace

struct SimpleFrameGeneration::Impl {
  const ui::vulkan::VulkanDevice* device = nullptr;
  VkExtent2D extent{};
  std::vector<std::shared_ptr<ImagePair>> pool;
  std::shared_ptr<ImagePair> pending;
  bool pending_generated = false;
  uint64_t pending_epoch = 0, pending_interval_ns = 0;
  // Private copy of the previous final frame, only used by the native queue.
  VkImage history = VK_NULL_HANDLE;
  VkDeviceMemory history_memory = VK_NULL_HANDLE;
  bool history_valid = false;
  std::chrono::steady_clock::time_point last_encode{};

  void DestroyAll() {
    pending.reset();
    pool.clear();
    if (device && history) {
      device->functions().vkDestroyImage(device->device(), history, nullptr);
      device->functions().vkFreeMemory(device->device(), history_memory, nullptr);
    }
    history = VK_NULL_HANDLE;
    history_memory = VK_NULL_HANDLE;
    history_valid = false;
  }

  bool CreateHistory() {
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = kFormat;
    create.extent = {extent.width, extent.height, 1};
    create.mipLevels = create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return ui::vulkan::util::CreateDedicatedAllocationImage(
        device, create, ui::vulkan::util::MemoryPurpose::kDeviceLocal, history, history_memory);
  }

  std::shared_ptr<ImagePair> Acquire() {
    for (const auto& pair : pool) {
      if (pair.use_count() == 1) return pair;
    }
    if (pool.size() >= kMaximumPairs) return {};
    auto pair = ImagePair::Create(*device, extent);
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
  if (!commands || !source || !extent.width || !extent.height) return false;
  if (s.device != &device || s.extent.width != extent.width || s.extent.height != extent.height) {
    if (s.device && !WaitIdle(*s.device)) return false;
    s.DestroyAll();
    s.device = &device;
    s.extent = extent;
    if (!s.CreateHistory()) {
      s.DestroyAll();
      return false;
    }
  }
  const auto now = std::chrono::steady_clock::now();
  uint64_t interval_ns = 0;
  if (s.last_encode.time_since_epoch().count()) {
    interval_ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               now - s.last_encode).count()) / 2;
  }
  s.last_encode = now;
  if (!interval_ns || interval_ns > 3 * kMaximumIntervalNs) {
    // First frame, or a long stall: start over without publishing this frame.
    s.history_valid = false;
  }
  if (fixed_interval_ns) interval_ns = fixed_interval_ns;
  interval_ns = std::min(interval_ns, kMaximumIntervalNs);
  if (!interval_ns) s.history_valid = false;
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
  const auto copy = [&](VkImage from, VkImageLayout from_layout, VkImage to) {
    VkImageCopy region{};
    region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {extent.width, extent.height, 1};
    fn.vkCmdCopyImage(commands, from, from_layout, to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                      &region);
  };
  constexpr auto kInternalLayout = ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout;
  constexpr auto kInternalStage = ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask;
  constexpr auto kInternalAccess = ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask;

  // The final frame becomes readable by transfers.
  barrier(source, source_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source_access,
          VK_ACCESS_TRANSFER_READ_BIT, source_stage, VK_PIPELINE_STAGE_TRANSFER_BIT);
  // real = the final frame
  barrier(pair->image[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
  copy(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pair->image[1]);
  barrier(pair->image[1], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kInternalLayout,
          VK_ACCESS_TRANSFER_WRITE_BIT, kInternalAccess, VK_PIPELINE_STAGE_TRANSFER_BIT,
          kInternalStage);
  // generated = the previous final frame (phase 1; a blend later)
  barrier(pair->image[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT);
  if (s.history_valid) {
    copy(s.history, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pair->image[0]);
  }
  barrier(pair->image[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kInternalLayout,
          VK_ACCESS_TRANSFER_WRITE_BIT, kInternalAccess, VK_PIPELINE_STAGE_TRANSFER_BIT,
          kInternalStage);
  // history = the final frame, for the next call
  barrier(s.history, s.history_valid ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  copy(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.history);
  barrier(s.history, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  // The presenter image goes back to the layout its release barrier expects.
  barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source_layout, 0, source_access,
          VK_PIPELINE_STAGE_TRANSFER_BIT, source_stage);

  s.pending = pair;
  s.pending_generated = s.history_valid;
  s.pending_epoch = epoch;
  s.pending_interval_ns = interval_ns;
  s.history_valid = true;
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
  impl_->history_valid = false;
}

void SimpleFrameGeneration::Shutdown() {
  Impl& s = *impl_;
  if (s.device && !WaitIdle(*s.device)) return;
  s.DestroyAll();
}

}  // namespace rex::graphics::gta4_native::temporal
