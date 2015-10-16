/*-------------------------------------------------------------------------
 * Vulkan Conformance Tests
 * ------------------------
 *
 * Copyright (c) 2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and/or associated documentation files (the
 * "Materials"), to deal in the Materials without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Materials, and to
 * permit persons to whom the Materials are furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice(s) and this permission notice shall be
 * included in all copies or substantial portions of the Materials.
 *
 * The Materials are Confidential Information as defined by the
 * Khronos Membership Agreement until designated non-confidential by
 * Khronos, at which point this condition clause shall be removed.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.
 *
 *//*!
 * \file
 * \brief Test Case Skeleton Based on Compute Shaders
 *//*--------------------------------------------------------------------*/

#include "vktSpvAsmComputeShaderCase.hpp"

#include "deSharedPtr.hpp"

#include "vkBuilderUtil.hpp"
#include "vkMemUtil.hpp"
#include "vkRefUtil.hpp"
#include "vkQueryUtil.hpp"

namespace
{

using namespace vk;
using std::vector;

typedef de::MovePtr<Allocation>			AllocationMp;
typedef de::SharedPtr<Allocation>		AllocationSp;
typedef Unique<VkBuffer>				BufferHandleUp;
typedef de::SharedPtr<BufferHandleUp>	BufferHandleSp;

/*--------------------------------------------------------------------*//*!
 * \brief Create storage buffer, allocate and bind memory for the buffer
 *
 * The memory is created as host visible and passed back as a vk::Allocation
 * instance via outMemory.
 *//*--------------------------------------------------------------------*/
Move<VkBuffer> createBufferAndBindMemory (const DeviceInterface& vkdi, const VkDevice& device, Allocator& allocator, size_t numBytes, AllocationMp* outMemory)
{
	const VkBufferCreateInfo bufferCreateInfo =
	{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,	// sType
		DE_NULL,								// pNext
		numBytes,								// size
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,		// usage
		0u,										// flags
		VK_SHARING_MODE_EXCLUSIVE,				// sharingMode
		0u,										// queueFamilyCount
		DE_NULL,								// pQueueFamilyIndices
	};

	Move<VkBuffer>				buffer			(createBuffer(vkdi, device, &bufferCreateInfo));
	const VkMemoryRequirements	requirements	= getBufferMemoryRequirements(vkdi, device, *buffer);
	AllocationMp				bufferMemory	= allocator.allocate(requirements, MemoryRequirement::HostVisible);

	VK_CHECK(vkdi.bindBufferMemory(device, *buffer, bufferMemory->getMemory(), bufferMemory->getOffset()));
	*outMemory = bufferMemory;

	return buffer;
}

void setMemory (const DeviceInterface& vkdi, const VkDevice& device, Allocation* destAlloc, size_t numBytes, const void* data)
{
	void* const hostPtr = destAlloc->getHostPtr();

	deMemcpy((deUint8*)hostPtr, data, numBytes);
	flushMappedMemoryRange(vkdi, device, destAlloc->getMemory(), destAlloc->getOffset(), numBytes);
}

void clearMemory (const DeviceInterface& vkdi, const VkDevice& device, Allocation* destAlloc, size_t numBytes)
{
	void* const hostPtr = destAlloc->getHostPtr();

	deMemset((deUint8*)hostPtr, 0, numBytes);
	flushMappedMemoryRange(vkdi, device, destAlloc->getMemory(), destAlloc->getOffset(), numBytes);
}

VkDescriptorInfo createDescriptorInfo (VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
{
	const VkDescriptorInfo info =
	{
		0,							// bufferView
		0,							// sampler
		0,							// imageView
		(VkImageLayout)0,			// imageLayout
		{ buffer, offset, range },	// bufferInfo
	};

	return info;
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a descriptor set layout with numBindings descriptors
 *
 * All descriptors are created for shader storage buffer objects and
 * compute pipeline.
 *//*--------------------------------------------------------------------*/
Move<VkDescriptorSetLayout> createDescriptorSetLayout (const DeviceInterface& vkdi, const VkDevice& device, size_t numBindings)
{
	DescriptorSetLayoutBuilder builder;

	for (size_t bindingNdx = 0; bindingNdx < numBindings; ++bindingNdx)
		builder.addSingleBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT);

	return builder.build(vkdi, device);
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a pipeline layout with one descriptor set
 *//*--------------------------------------------------------------------*/
Move<VkPipelineLayout> createPipelineLayout (const DeviceInterface& vkdi, const VkDevice& device, VkDescriptorSetLayout descriptorSetLayout)
{
	const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo =
	{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,	// sType
		DE_NULL,										// pNext
		1u,												// descriptorSetCount
		&descriptorSetLayout,							// pSetLayouts
		0u,												// pushConstantRangeCount
		DE_NULL,										// pPushConstantRanges
	};

	return createPipelineLayout(vkdi, device, &pipelineLayoutCreateInfo);
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a one-time descriptor pool for one descriptor set
 *
 * The pool supports numDescriptors storage buffer descriptors.
 *//*--------------------------------------------------------------------*/
inline Move<VkDescriptorPool> createDescriptorPool (const DeviceInterface& vkdi, const VkDevice& device, deUint32 numDescriptors)
{
	return DescriptorPoolBuilder()
		.addType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, numDescriptors)
		.build(vkdi, device, VK_DESCRIPTOR_POOL_USAGE_ONE_SHOT, /* maxSets = */ 1);
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a descriptor set
 *
 * The descriptor set's layout should contain numViews descriptors.
 * All the descriptors represent buffer views, and they are sequentially
 * binded to binding point starting from 0.
 *//*--------------------------------------------------------------------*/
Move<VkDescriptorSet> createDescriptorSet (const DeviceInterface& vkdi, const VkDevice& device, VkDescriptorPool pool, VkDescriptorSetLayout layout, size_t numViews, const vector<VkDescriptorInfo>& descriptorInfos)
{
	Move<VkDescriptorSet>		descriptorSet	= allocDescriptorSet(vkdi, device, pool, VK_DESCRIPTOR_SET_USAGE_ONE_SHOT, layout);
	DescriptorSetUpdateBuilder	builder;

	for (deUint32 descriptorNdx = 0; descriptorNdx < numViews; ++descriptorNdx)
		builder.writeSingle(*descriptorSet, DescriptorSetUpdateBuilder::Location::binding(descriptorNdx), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &descriptorInfos[descriptorNdx]);
	builder.update(vkdi, device);

	return descriptorSet;
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a shader from the given shader module
 *
 * The entry point of the shader is assumed to be "main".
 *//*--------------------------------------------------------------------*/
Move<VkShader> createShader (const DeviceInterface& vkdi, const VkDevice& device, VkShaderModule module)
{
	const VkShaderCreateInfo shaderCreateInfo =
	{
		VK_STRUCTURE_TYPE_SHADER_CREATE_INFO,	// sType
		DE_NULL,								// pNext
		module,									// module
		"main",									// pName
		0u,										// flags
		VK_SHADER_STAGE_COMPUTE,				// stage
	};

	return createShader(vkdi, device, &shaderCreateInfo);
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a compute pipeline based on the given shader
 *//*--------------------------------------------------------------------*/
Move<VkPipeline> createComputePipeline (const DeviceInterface& vkdi, const VkDevice& device, VkPipelineLayout pipelineLayout, VkShader shader)
{
	const VkPipelineShaderStageCreateInfo	pipelineShaderStageCreateInfo	=
	{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,	// sType
		DE_NULL,												// pNext
		VK_SHADER_STAGE_COMPUTE,								// stage
		shader,													// shader
		DE_NULL,												// pSpecializationInfo
	};
	const VkComputePipelineCreateInfo		pipelineCreateInfo				=
	{
		VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,			// sType
		DE_NULL,												// pNext
		pipelineShaderStageCreateInfo,							// cs
		0u,														// flags
		pipelineLayout,											// layout
		(VkPipeline)0,											// basePipelineHandle
		0u,														// basePipelineIndex
	};

	return createComputePipeline(vkdi, device, (VkPipelineCache)0u, &pipelineCreateInfo);
}

/*--------------------------------------------------------------------*//*!
 * \brief Create a command pool
 *
 * The created command pool is designated for use on the queue type
 * represented by the given queueFamilyIndex.
 *//*--------------------------------------------------------------------*/
Move<VkCmdPool> createCommandPool (const DeviceInterface& vkdi, VkDevice device, deUint32 queueFamilyIndex)
{
	const VkCmdPoolCreateInfo cmdPoolCreateInfo =
	{
		VK_STRUCTURE_TYPE_CMD_POOL_CREATE_INFO,	// sType
		DE_NULL,								// pNext
		queueFamilyIndex,						// queueFamilyIndex
		0u										// flags
	};

	return createCommandPool(vkdi, device, &cmdPoolCreateInfo);
}

} // anonymous

namespace vkt
{
namespace SpirVAssembly
{

/*--------------------------------------------------------------------*//*!
 * \brief Test instance for compute pipeline
 *
 * The compute shader is specified in the format of SPIR-V assembly, which
 * is allowed to access MAX_NUM_INPUT_BUFFERS input storage buffers and
 * MAX_NUM_OUTPUT_BUFFERS output storage buffers maximally. The shader
 * source and input/output data are given in a ComputeShaderSpec object.
 *
 * This instance runs the given compute shader by feeding the data from input
 * buffers and compares the data in the output buffers with the expected.
 *//*--------------------------------------------------------------------*/
class SpvAsmComputeShaderInstance : public TestInstance
{
public:
								SpvAsmComputeShaderInstance	(Context& ctx, const ComputeShaderSpec& spec);
	tcu::TestStatus				iterate						(void);

private:
	const ComputeShaderSpec&	m_shaderSpec;
};

// ComputeShaderTestCase implementations

SpvAsmComputeShaderCase::SpvAsmComputeShaderCase (tcu::TestContext& testCtx, const char* name, const char* description, const ComputeShaderSpec& spec)
	: TestCase		(testCtx, name, description)
	, m_shaderSpec	(spec)
{
}

void SpvAsmComputeShaderCase::initPrograms (SourceCollections& programCollection) const
{
	programCollection.spirvAsmSources.add("compute") << m_shaderSpec.assembly.c_str();
}

TestInstance* SpvAsmComputeShaderCase::createInstance (Context& ctx) const
{
	return new SpvAsmComputeShaderInstance(ctx, m_shaderSpec);
}

// ComputeShaderTestInstance implementations

SpvAsmComputeShaderInstance::SpvAsmComputeShaderInstance (Context& ctx, const ComputeShaderSpec& spec)
	: TestInstance	(ctx)
	, m_shaderSpec	(spec)
{
}

tcu::TestStatus SpvAsmComputeShaderInstance::iterate (void)
{
	const DeviceInterface&			vkdi				= m_context.getDeviceInterface();
	const VkDevice&					device				= m_context.getDevice();
	Allocator&						allocator			= m_context.getDefaultAllocator();

	vector<AllocationSp>			inputAllocs;
	vector<AllocationSp>			outputAllocs;
	vector<BufferHandleSp>			inputBuffers;
	vector<BufferHandleSp>			outputBuffers;
	vector<VkDescriptorInfo>		descriptorInfos;

	DE_ASSERT(!m_shaderSpec.outputs.empty());
	const size_t					numBuffers			= m_shaderSpec.inputs.size() + m_shaderSpec.outputs.size();

	// Create buffer object, allocate storage, and create view for all input/output buffers.

	for (size_t inputNdx = 0; inputNdx < m_shaderSpec.inputs.size(); ++inputNdx)
	{
		AllocationMp				alloc;
		const BufferSp&				input				= m_shaderSpec.inputs[inputNdx];
		const size_t				numBytes			= input->getNumBytes();
		BufferHandleUp*				buffer				= new BufferHandleUp(createBufferAndBindMemory(vkdi, device, allocator, numBytes, &alloc));

		setMemory(vkdi, device, &*alloc, numBytes, input->data());
		descriptorInfos.push_back(createDescriptorInfo(**buffer, 0u, numBytes));
		inputBuffers.push_back(BufferHandleSp(buffer));
		inputAllocs.push_back(de::SharedPtr<Allocation>(alloc.release()));
	}

	for (size_t outputNdx = 0; outputNdx < m_shaderSpec.outputs.size(); ++outputNdx)
	{
		AllocationMp				alloc;
		const BufferSp&				output				= m_shaderSpec.outputs[outputNdx];
		const size_t				numBytes			= output->getNumBytes();
		BufferHandleUp*				buffer				= new BufferHandleUp(createBufferAndBindMemory(vkdi, device, allocator, numBytes, &alloc));

		clearMemory(vkdi, device, &*alloc, numBytes);
		descriptorInfos.push_back(createDescriptorInfo(**buffer, 0u, numBytes));
		outputBuffers.push_back(BufferHandleSp(buffer));
		outputAllocs.push_back(de::SharedPtr<Allocation>(alloc.release()));
	}

	// Create layouts and descriptor set.

	Unique<VkDescriptorSetLayout>	descriptorSetLayout	(createDescriptorSetLayout(vkdi, device, numBuffers));
	Unique<VkPipelineLayout>		pipelineLayout		(createPipelineLayout(vkdi, device, *descriptorSetLayout));
	Unique<VkDescriptorPool>		descriptorPool		(createDescriptorPool(vkdi, device, (deUint32)numBuffers));
	Unique<VkDescriptorSet>			descriptorSet		(createDescriptorSet(vkdi, device, *descriptorPool, *descriptorSetLayout, numBuffers, descriptorInfos));

	// Create compute shader and pipeline.

	const ProgramBinary&			binary				= m_context.getBinaryCollection().get("compute");
	Unique<VkShaderModule>			module				(createShaderModule(vkdi, device, binary, (VkShaderModuleCreateFlags)0u));
	Unique<VkShader>				shader				(createShader(vkdi, device, *module));

	Unique<VkPipeline>				computePipeline		(createComputePipeline(vkdi, device, *pipelineLayout, *shader));

	// Create command buffer and record commands

	const Unique<VkCmdPool>			cmdPool				(createCommandPool(vkdi, device, m_context.getUniversalQueueFamilyIndex()));
	const VkCmdBufferCreateInfo		cmdBufferCreateInfo	=
	{
		VK_STRUCTURE_TYPE_CMD_BUFFER_CREATE_INFO,	// sType
		NULL,										// pNext
		*cmdPool,									// cmdPool
		VK_CMD_BUFFER_LEVEL_PRIMARY,				// level
		0u											// flags
	};

	Unique<VkCmdBuffer>				cmdBuffer			(createCommandBuffer(vkdi, device, &cmdBufferCreateInfo));

	const VkCmdBufferBeginInfo		cmdBufferBeginInfo	=
	{
		VK_STRUCTURE_TYPE_CMD_BUFFER_BEGIN_INFO,	// sType
		DE_NULL,									// pNext
		VK_CMD_BUFFER_OPTIMIZE_ONE_TIME_SUBMIT_BIT,	// flags
		(VkRenderPass)0u,							// renderPass
		0u,											// subpass
		(VkFramebuffer)0u,							// framebuffer
	};

	const tcu::IVec3&				numWorkGroups		= m_shaderSpec.numWorkGroups;

	VK_CHECK(vkdi.beginCommandBuffer(*cmdBuffer, &cmdBufferBeginInfo));
	vkdi.cmdBindPipeline(*cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, *computePipeline);
	vkdi.cmdBindDescriptorSets(*cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, *pipelineLayout, 0, 1, &descriptorSet.get(), 0, DE_NULL);
	vkdi.cmdDispatch(*cmdBuffer, numWorkGroups.x(), numWorkGroups.y(), numWorkGroups.z());
	VK_CHECK(vkdi.endCommandBuffer(*cmdBuffer));

	// Create fence and run.

	const VkFenceCreateInfo			fenceCreateInfo		=
	{
		 VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,		// sType
		 NULL,										// pNext
		 0											// flags
    };
	const Unique<VkFence>			cmdCompleteFence	(createFence(vkdi, device, &fenceCreateInfo));
	const deUint64					infiniteTimeout		= ~(deUint64)0u;

	VK_CHECK(vkdi.queueSubmit(m_context.getUniversalQueue(), 1, &cmdBuffer.get(), *cmdCompleteFence));
	VK_CHECK(vkdi.waitForFences(device, 1, &cmdCompleteFence.get(), 0u, infiniteTimeout)); // \note: timeout is failure

	// Check output.

	for (size_t outputNdx = 0; outputNdx < m_shaderSpec.outputs.size(); ++outputNdx)
	{
		const BufferSp& expectedOutput = m_shaderSpec.outputs[outputNdx];
		if (deMemCmp(expectedOutput->data(), outputAllocs[outputNdx]->getHostPtr(), expectedOutput->getNumBytes()))
			return tcu::TestStatus::fail("Output doesn't match with expected");
	}

	return tcu::TestStatus::pass("Ouput match with expected");
}

} // SpirVAssembly
} // vkt