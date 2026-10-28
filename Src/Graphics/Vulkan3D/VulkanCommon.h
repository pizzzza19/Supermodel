/**
 ** Supermodel-VK
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright 2026 The Supermodel-VK authors
 **
 ** This file is part of Supermodel-VK, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
#ifndef INCLUDED_VULKAN3D_COMMON_H
#define INCLUDED_VULKAN3D_COMMON_H

/*
 * Shared Vulkan helpers and the Real3D vertex / push-constant layouts used by
 * CVulkan3D. Push constants are 208 bytes, matching the std140 block below.
 */

#define VK_NO_PROTOTYPES
#include "volk/volk.h"

#include "Graphics/R3DShared.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Vulkan3D {

constexpr uint32_t kMaxFramesInFlight = 2;
constexpr uint32_t kMaxTextureSlots = 4096;
constexpr uint32_t kMaxMaterials = 16384;
constexpr VkDeviceSize kRingSliceBytes = 64ull * 1024ull * 1024ull;

/*
 * How many ROM (static, VROM-backed) vertices the geometry cache may accumulate before it is flushed
 * and rebuilt.
 *
 * This number has to be visible to both the cache that fills it and the code that sizes the GPU buffer
 * that receives it. They were separate before - the cache used a local 1500000 while the buffer was
 * allocated as a flat 64 MB - and the two disagreed by a third, so part way through a level the buffer
 * filled and every mesh beyond it silently stopped being drawn. Whole models disappeared and the
 * symptom read as a texture fault, which is why it took so long to find.
 *
 * Defined once, here, so the pair cannot drift apart again.
 *
 * Sized deliberately larger than a single track needs rather than merely larger than the old, broken
 * figure. Reaching the limit costs a flush, and a flush drops the accumulated ROM models for one frame
 * - a visible pop if it lands mid-race. Measured accumulation on Scud Race is roughly 6500 vertices a
 * second, so a five-minute track builds about two million; six million leaves comfortable headroom for
 * a long session without a flush ever occurring during normal play.
 *
 * The cost is real and linear: 64 bytes per vertex on the GPU and 60 in the CPU-side cache, so six
 * million holds 384 MB of device memory and about 360 MB of system memory once filled. On a modern card
 * that is a small fraction of what is available. On a card with 2 GB it is worth knowing about, which is
 * why the allocation logs its size and why a failure to allocate is reported rather than silently
 * falling back.
 */
// kMaxRomVerts is declared in Graphics/R3DShared.h.


/*
 * The four entry points that differ between Vulkan 1.2 and 1.3, routed so that one build works on
 * either.
 *
 * dynamic rendering and synchronization2 became core in 1.3, but both exist as VK_KHR_ extensions
 * that are widely supported on 1.2 devices - which is most of the hardware that predates the 1.3
 * requirement and no longer needs to be shut out. Each wrapper prefers the core name and falls back
 * to the extension one, so a 1.3 device calls exactly what it did before this existed and a 1.2 device
 * calls the extension.
 *
 * The branch is the same one that used to be here, and its cost is nothing: roughly fifteen tests a
 * frame against a million-plus vertices.
 *
 * What went wrong the first time is worth recording, because restoring the branch without fixing it
 * would repeat it. The path could not work as written: the shaders were compiled to SPIR-V 1.6, which
 * needs Vulkan 1.3 or VK_KHR_maintenance4, and maintenance4 was never requested - so any device that
 * reached the 1.2 path failed later, at shader-module creation, with a message pointing at the shader
 * rather than at the real problem. The shaders are now built for SPIR-V 1.5 (see
 * Scripts/CompileVulkanShaders.cmd), which both 1.2 and 1.3 accept, so the 1.2 path has nothing left
 * to trip over.
 *
 * VK_KHR_push_descriptor remains optional and is still checked at use: without it the texture decode
 * runs on the CPU instead of the GPU, which is a degradation rather than a failure.
 */

inline void CmdBeginRendering(VkCommandBuffer cmd, const VkRenderingInfo* info)
{
	if (vkCmdBeginRendering)
		vkCmdBeginRendering(cmd, info);
	else if (vkCmdBeginRenderingKHR)
		vkCmdBeginRenderingKHR(cmd, info);
}

inline void CmdEndRendering(VkCommandBuffer cmd)
{
	if (vkCmdEndRendering)
		vkCmdEndRendering(cmd);
	else if (vkCmdEndRenderingKHR)
		vkCmdEndRenderingKHR(cmd);
}

inline void CmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo* dep)
{
	if (vkCmdPipelineBarrier2)
		vkCmdPipelineBarrier2(cmd, dep);
	else if (vkCmdPipelineBarrier2KHR)
		vkCmdPipelineBarrier2KHR(cmd, dep);
}

inline void QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits, VkFence fence)
{
	if (vkQueueSubmit2)
		vkQueueSubmit2(queue, count, submits, fence);
	else if (vkQueueSubmit2KHR)
		vkQueueSubmit2KHR(queue, count, submits, fence);
}

/*
 * True when at least one of the two names for each of the three families above is present.
 *
 * Checked once, at device creation, so that a device where neither is loaded is rejected there with a
 * clear reason rather than drawing nothing later on. Every wrapper above silently does nothing when
 * both pointers are null, which is the right behaviour for a call that must not crash but the wrong
 * thing to leave undetected.
 */
inline bool Rendering2EntryPointsAvailable()
{
	return (vkCmdBeginRendering != nullptr || vkCmdBeginRenderingKHR != nullptr) &&
		(vkCmdEndRendering != nullptr || vkCmdEndRenderingKHR != nullptr) &&
		(vkCmdPipelineBarrier2 != nullptr || vkCmdPipelineBarrier2KHR != nullptr) &&
		(vkQueueSubmit2 != nullptr || vkQueueSubmit2KHR != nullptr);
}

inline void CmdTransition(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspect, uint32_t baseMip = 0, uint32_t mipCount = 1)
{
	if (oldLayout == newLayout || image == VK_NULL_HANDLE)
		return;

	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = baseMip;
	barrier.subresourceRange.levelCount = mipCount;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	switch (oldLayout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		barrier.srcAccessMask = 0;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	default:
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}

	switch (newLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		barrier.dstAccessMask = 0;
		break;
	default:
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}

	VkDependencyInfo dep = {};
	dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;
	CmdPipelineBarrier2(cmd, &dep);
}

inline float UintToFloatBits(uint32_t u)
{
	float f;
	std::memcpy(&f, &u, sizeof(f));
	return f;
}

} // Vulkan3D

#endif
