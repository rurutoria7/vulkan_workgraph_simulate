/*
* Vulkan Example - Persistent Thread Koch Snowflake Generator with Per-Frame Compute
* Koch snowflake fractal subdivision via persistent compute threads, recomputed every frame
*/

#include "vulkanexamplebase.h"

#define QUEUE_SIZE 16384
#define MAX_DEPTH 6
#define NUM_WORKGROUPS 96
#define NODE_C_START 72
#define EXPECTED_EDGES (3 * (1 << (2 * MAX_DEPTH)))
#define EXPECTED_VERTICES (EXPECTED_EDGES * 2)

struct QueueControl {
	uint32_t head;
	uint32_t tail;
	uint32_t count;
};

struct ControlBlock {
	QueueControl q1;
	QueueControl q2;
	uint32_t stopFlag;
	uint32_t totalProcessed;
	uint32_t vertexCount;
	uint32_t seedDone;
};

struct Task {
	uint32_t payload[6];
};

class VulkanExample : public VulkanExampleBase
{
public:
	struct {
		VkBuffer buffer;
		VkDeviceMemory memory;
	} vertexBuffer{};

	struct {
		VkBuffer controlBuf;
		VkDeviceMemory controlMem;
		VkBuffer q1Buf;
		VkDeviceMemory q1Mem;
		VkBuffer q2Buf;
		VkDeviceMemory q2Mem;
		VkDescriptorPool descriptorPool;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkPipelineLayout pipelineLayout;
		VkPipeline pipeline;
		VkShaderModule shaderModule;
	} compute{};

	struct {
		VkPipelineLayout pipelineLayout;
		VkPipeline pipeline;
	} graphics{};

	VulkanExample() : VulkanExampleBase()
	{
		title = "Koch Snowflake - Persistent Thread Generator (Per-Frame Compute)";
		settings.vsync = false;
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, graphics.pipeline, nullptr);
			vkDestroyPipelineLayout(device, graphics.pipelineLayout, nullptr);
			vkDestroyBuffer(device, vertexBuffer.buffer, nullptr);
			vkFreeMemory(device, vertexBuffer.memory, nullptr);

			vkDestroyPipeline(device, compute.pipeline, nullptr);
			vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
			vkDestroyDescriptorPool(device, compute.descriptorPool, nullptr);
			vkDestroyShaderModule(device, compute.shaderModule, nullptr);
			vkDestroyBuffer(device, compute.controlBuf, nullptr);
			vkFreeMemory(device, compute.controlMem, nullptr);
			vkDestroyBuffer(device, compute.q1Buf, nullptr);
			vkFreeMemory(device, compute.q1Mem, nullptr);
			vkDestroyBuffer(device, compute.q2Buf, nullptr);
			vkFreeMemory(device, compute.q2Mem, nullptr);
		}
	}

	uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties)
	{
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
			if ((typeBits & 1) == 1) {
				if ((memProps.memoryTypes[i].propertyFlags & properties) == properties)
					return i;
			}
			typeBits >>= 1;
		}
		return 0;
	}

	void prepareCompute()
	{
		auto createBuf = [&](VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize size, VkBufferUsageFlags usage) {
			VkBufferCreateInfo ci = vks::initializers::bufferCreateInfo(usage, size);
			VK_CHECK_RESULT(vkCreateBuffer(device, &ci, nullptr, buf));
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device, *buf, &memReqs);
			VkMemoryAllocateInfo ai = vks::initializers::memoryAllocateInfo();
			ai.allocationSize = memReqs.size;
			ai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &ai, nullptr, mem));
			VK_CHECK_RESULT(vkBindBufferMemory(device, *buf, *mem, 0));
		};

		VkBufferUsageFlags ssboTransfer = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		createBuf(&compute.controlBuf, &compute.controlMem, sizeof(ControlBlock), ssboTransfer);
		createBuf(&compute.q1Buf, &compute.q1Mem, sizeof(Task) * QUEUE_SIZE, ssboTransfer);
		createBuf(&compute.q2Buf, &compute.q2Mem, sizeof(Task) * QUEUE_SIZE, ssboTransfer);

		// Vertex buffer: storage + vertex + transfer dst (for potential clears)
		{
			VkDeviceSize size = EXPECTED_VERTICES * sizeof(float) * 2;
			VkBufferCreateInfo ci = vks::initializers::bufferCreateInfo(
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, size);
			VK_CHECK_RESULT(vkCreateBuffer(device, &ci, nullptr, &vertexBuffer.buffer));
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device, vertexBuffer.buffer, &memReqs);
			VkMemoryAllocateInfo ai = vks::initializers::memoryAllocateInfo();
			ai.allocationSize = memReqs.size;
			ai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &ai, nullptr, &vertexBuffer.memory));
			VK_CHECK_RESULT(vkBindBufferMemory(device, vertexBuffer.buffer, vertexBuffer.memory, 0));
		}

		// Descriptor pool, layout, set
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4)
		};
		VkDescriptorPoolCreateInfo poolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &compute.descriptorPool));

		std::vector<VkDescriptorSetLayoutBinding> bindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3),
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(bindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &compute.descriptorSetLayout));

		VkPipelineLayoutCreateInfo plInfo = vks::initializers::pipelineLayoutCreateInfo(&compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &plInfo, nullptr, &compute.pipelineLayout));

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(compute.descriptorPool, &compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));

		VkDescriptorBufferInfo controlDesc = { compute.controlBuf, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo q1Desc = { compute.q1Buf, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo q2Desc = { compute.q2Buf, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo outputDesc = { vertexBuffer.buffer, 0, VK_WHOLE_SIZE };
		std::vector<VkWriteDescriptorSet> writes = {
			vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &controlDesc),
			vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &q1Desc),
			vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &q2Desc),
			vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &outputDesc),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		// Compute pipeline with specialization constants
		compute.shaderModule = vks::tools::loadShader((getShadersPath() + "workgraph_poc/headless.comp.spv").c_str(), device);
		VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		shaderStage.module = compute.shaderModule;
		shaderStage.pName = "main";

		struct { uint32_t queueSize; uint32_t maxDepth; uint32_t nodeCStart; uint32_t maxDepthEdges; } specData = { QUEUE_SIZE, MAX_DEPTH, NODE_C_START, EXPECTED_EDGES };
		VkSpecializationMapEntry specEntries[4] = {
			{ 0, offsetof(decltype(specData), queueSize),     sizeof(uint32_t) },
			{ 1, offsetof(decltype(specData), maxDepth),      sizeof(uint32_t) },
			{ 2, offsetof(decltype(specData), nodeCStart),    sizeof(uint32_t) },
			{ 3, offsetof(decltype(specData), maxDepthEdges), sizeof(uint32_t) },
		};
		VkSpecializationInfo specInfo = {};
		specInfo.mapEntryCount = 4;
		specInfo.pMapEntries = specEntries;
		specInfo.dataSize = sizeof(specData);
		specInfo.pData = &specData;
		shaderStage.pSpecializationInfo = &specInfo;

		VkComputePipelineCreateInfo compCI = vks::initializers::computePipelineCreateInfo(compute.pipelineLayout, 0);
		compCI.stage = shaderStage;
		VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compCI, nullptr, &compute.pipeline));
	}

	void prepareGraphicsPipeline()
	{
		VkPipelineLayoutCreateInfo plCI = vks::initializers::pipelineLayoutCreateInfo(nullptr, 0);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &plCI, nullptr, &graphics.pipelineLayout));

		VkPipelineInputAssemblyStateCreateInfo inputAssembly =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterization =
			vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		rasterization.lineWidth = 1.0f;
		VkPipelineColorBlendAttachmentState blendAttachment =
			vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlend =
			vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachment);
		VkPipelineDepthStencilStateCreateInfo depthStencil =
			vks::initializers::pipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_ALWAYS);
		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisample =
			vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates);

		VkVertexInputBindingDescription vertexBinding = {};
		vertexBinding.binding = 0;
		vertexBinding.stride = sizeof(float) * 2;
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttribute = {};
		vertexAttribute.binding = 0;
		vertexAttribute.location = 0;
		vertexAttribute.format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttribute.offset = 0;

		VkPipelineVertexInputStateCreateInfo vertexInput = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &vertexBinding;
		vertexInput.vertexAttributeDescriptionCount = 1;
		vertexInput.pVertexAttributeDescriptions = &vertexAttribute;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShadersPath() + "workgraph_poc/koch.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "workgraph_poc/koch.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(graphics.pipelineLayout, renderPass, 0);
		pipelineCI.pVertexInputState = &vertexInput;
		pipelineCI.pInputAssemblyState = &inputAssembly;
		pipelineCI.pRasterizationState = &rasterization;
		pipelineCI.pColorBlendState = &colorBlend;
		pipelineCI.pMultisampleState = &multisample;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencil;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &graphics.pipeline));
	}

	void buildCommandBuffers()
	{
		VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &cmdBufInfo));

		// --- Phase 1: GPU reset of control and queue buffers ---
		vkCmdFillBuffer(cmd, compute.controlBuf, 0, sizeof(ControlBlock), 0);
		vkCmdFillBuffer(cmd, compute.q1Buf, 0, sizeof(Task) * QUEUE_SIZE, 0);
		vkCmdFillBuffer(cmd, compute.q2Buf, 0, sizeof(Task) * QUEUE_SIZE, 0);

		VkMemoryBarrier fillBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0, 1, &fillBarrier, 0, nullptr, 0, nullptr);

		// --- Phase 2: Compute dispatch (persistent threads) ---
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);
		vkCmdDispatch(cmd, NUM_WORKGROUPS, 1, 1);

		// --- Phase 3: Barrier compute write → vertex read ---
		VkBufferMemoryBarrier vtxBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		vtxBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		vtxBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
		vtxBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vtxBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vtxBarrier.buffer = vertexBuffer.buffer;
		vtxBarrier.offset = 0;
		vtxBarrier.size = VK_WHOLE_SIZE;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			0, 0, nullptr, 1, &vtxBarrier, 0, nullptr);

		// --- Phase 4: Render pass ---
		VkClearValue clearValues[2]{};
		clearValues[0].color = { { 0.05f, 0.05f, 0.05f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo rpBegin = vks::initializers::renderPassBeginInfo();
		rpBegin.renderPass = renderPass;
		rpBegin.renderArea.extent.width = width;
		rpBegin.renderArea.extent.height = height;
		rpBegin.clearValueCount = 2;
		rpBegin.pClearValues = clearValues;
		rpBegin.framebuffer = frameBuffers[currentImageIndex];

		vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, offsets);
		vkCmdDraw(cmd, EXPECTED_VERTICES, 1, 0, 0);

		drawUI(cmd);

		vkCmdEndRenderPass(cmd);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		prepareCompute();
		prepareGraphicsPipeline();
		prepared = true;
	}

	void render()
	{
		if (!prepared)
			return;
		prepareFrame();
		buildCommandBuffers();
		submitFrame();
	}
};

VULKAN_EXAMPLE_MAIN()
