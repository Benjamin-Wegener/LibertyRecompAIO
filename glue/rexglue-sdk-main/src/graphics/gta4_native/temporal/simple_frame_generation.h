#pragma once

#include <cstdint>
#include <memory>

#include <rex/ui/vulkan/presenter.h>

namespace rex::graphics::gta4_native::temporal {

// Android frame generation that needs no motion vectors from the title: the
// presenter shows a "generated" image half a frame before the "real" one
// (VulkanPresenter::SetGeneratedFrame). Phase 1 (this file): the generated image
// is a copy of the previous final frame, which validates the image contract,
// the leases and the pacing. A later phase replaces the copy with a motion
// compensated blend of the previous and the current frame.
class SimpleFrameGeneration {
 public:
  SimpleFrameGeneration();
  ~SimpleFrameGeneration();
  SimpleFrameGeneration(const SimpleFrameGeneration&) = delete;
  SimpleFrameGeneration& operator=(const SimpleFrameGeneration&) = delete;

  // Records, before the presenter image's release barrier, a copy of the final
  // frame into a retained `real` image, a copy of the previous frame into the
  // `generated` image and the update of the private history. `source` is the
  // presenter's image in `source_layout`, last written by `source_stage` /
  // `source_access`; it is returned to that layout. False: nothing recorded.
  bool Encode(const ui::vulkan::VulkanDevice& device, VkCommandBuffer commands, VkImage source,
              VkExtent2D extent, VkImageLayout source_layout, VkPipelineStageFlags source_stage,
              VkAccessFlags source_access, uint64_t epoch);

  // The lease of the pair recorded by the last Encode (empty when none): keep it
  // until the submission that contains the copies has completed.
  std::shared_ptr<void> pending_lease() const;

  // After the submission succeeded and the presenter image access was marked.
  bool Publish(ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext& context);
  // The submission failed: forget the recorded pair.
  void Discard();
  // Waits for the GPU and frees everything.
  void Shutdown();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rex::graphics::gta4_native::temporal
