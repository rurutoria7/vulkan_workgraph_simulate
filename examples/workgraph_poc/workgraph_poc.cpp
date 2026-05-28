/*
* Vulkan Example - Persistent Thread Koch Snowflake Generator with Per-Frame Compute
* Koch snowflake fractal subdivision via persistent compute threads, recomputed every frame
*/

#include "vulkanexamplebase.h"

#include <algorithm>
#include <fstream>

namespace {

struct ScopedCmdLabel {
	ScopedCmdLabel(VkCommandBuffer commandBuffer, const char* name, glm::vec4 color)
		: commandBuffer(commandBuffer)
	{
		vks::debugutils::cmdBeginLabel(commandBuffer, name, color);
	}

	~ScopedCmdLabel()
	{
		vks::debugutils::cmdEndLabel(commandBuffer);
	}

	VkCommandBuffer commandBuffer;
};

}

// Koch edge count is 3 * 4^depth. The shader receives the same values via
// specialization constants; keep these defines in sync with headless.comp.
#define MAX_DEPTH 8
#define EXPECTED_EDGES (3u * (1u << (2u * MAX_DEPTH)))
#define EXPECTED_VERTICES (EXPECTED_EDGES * 2u)
#define QUEUE_SIZE EXPECTED_EDGES
#define NUM_WORKGROUPS 96
#define NODE_C_START 72
#define MAX_QUEUE_SHARDS 256
#define Q1_QUEUE_SHARDS_DEFAULT 256
#define Q2_QUEUE_SHARDS_DEFAULT 256

enum TimestampQuery : uint32_t {
	TimestampFrameStart = 0,
	TimestampAfterReset,
	TimestampAfterResetBarrier,
	TimestampAfterCompute,
	TimestampAfterMetricsCopy,
	TimestampAfterRender,
	TimestampCount
};

struct QueueControl {
	uint32_t head;
	uint32_t tail;
	uint32_t count;
	uint32_t pad;
};

struct QueueMetrics {
	uint32_t enqueueAttempts;
	uint32_t enqueueSuccess;
	uint32_t enqueueCasFail;
	uint32_t enqueueFull;
	uint32_t dequeueAttempts;
	uint32_t dequeueSuccess;
	uint32_t dequeueCasFail;
	uint32_t dequeueEmpty;
	uint32_t readyExchange;
	uint32_t readyCasAttempts;
	uint32_t readyCasSuccess;
	uint32_t readyCasFail;
	uint32_t readyMaxSpin;
	uint32_t highWater;
};

struct DebugCounters {
	QueueMetrics q1Shards[MAX_QUEUE_SHARDS];
	QueueMetrics q2Shards[MAX_QUEUE_SHARDS];
	uint32_t q2DequeueBatches;
	uint32_t q2DequeueBatchSlots;
	uint32_t q2DequeueBatchCasAttempts;
	uint32_t q2DequeueBatchCasFail;
	uint32_t q2DequeueBatchEmpty;
	uint32_t q2DequeueBatchPartial;
	uint32_t nodeASeedEdges;
	uint32_t nodeBTasks;
	uint32_t nodeBSubdivideTasks;
	uint32_t nodeBFinalTasks;
	uint32_t nodeCOutputEdges;
	uint32_t vertexWriteCalls;
	uint32_t stopFlagWrites;
	uint32_t pushStopExits;
};

struct ControlBlock {
	QueueControl q1[MAX_QUEUE_SHARDS];
	QueueControl q2[MAX_QUEUE_SHARDS];
	uint32_t stopFlag;
	uint32_t totalProcessed;
	uint32_t vertexCount;
	uint32_t seedDone;
	DebugCounters metrics;
};

struct Task {
	// Shared ABI with GLSL: [0..3]=edge endpoints, [4]=depth, [5]=ready flag.
	// The shader must access payload[5] atomically for cross-CU visibility.
	uint32_t payload[6];
};

static_assert(sizeof(QueueControl) == 4u * sizeof(uint32_t));
static_assert(sizeof(QueueMetrics) == 14u * sizeof(uint32_t));
static_assert(sizeof(DebugCounters) == (MAX_QUEUE_SHARDS * 14u * 2u + 14u) * sizeof(uint32_t));
static_assert(sizeof(ControlBlock) == (MAX_QUEUE_SHARDS * 4u * 2u + 4u + MAX_QUEUE_SHARDS * 14u * 2u + 14u) * sizeof(uint32_t));

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

	struct {
		uint32_t q1QueueShards{ Q1_QUEUE_SHARDS_DEFAULT };
		uint32_t q2QueueShards{ Q2_QUEUE_SHARDS_DEFAULT };
		uint32_t q1ShardCapacity{ QUEUE_SIZE };
		uint32_t q2ShardCapacity{ QUEUE_SIZE };
		uint32_t nodeCStart{ NODE_C_START };
		bool q1LanePop{ true };
		bool q2DequeueBatch{ false };
		uint32_t q2DequeueBatchLimit{ 32 };
	} experiment{};

	struct {
		VkQueryPool queryPool{ VK_NULL_HANDLE };
		bool enabled{ false };
		bool stdoutEnabled{ false };
		bool shaderCountersEnabled{ false };
		bool timestampsSupported{ false };
		bool stdoutHeaderPrinted{ false };
		bool fileHeaderPrinted{ false };
		uint32_t stdoutInterval{ 60 };
		std::ofstream fileStream{};
		uint64_t completedFrames{ 0 };
		std::array<bool, maxConcurrentFrames> submitted{};
		std::array<VkBuffer, maxConcurrentFrames> readbackBuf{};
		std::array<VkDeviceMemory, maxConcurrentFrames> readbackMem{};
		std::array<void*, maxConcurrentFrames> readbackMapped{};

		struct Snapshot {
			ControlBlock control{};
			std::array<uint64_t, TimestampCount> timestamps{};
			bool valid{ false };
			bool timestampsValid{ false };
			float resetMs{ 0.0f };
			float resetBarrierMs{ 0.0f };
			float computeMs{ 0.0f };
			float metricsCopyMs{ 0.0f };
			float renderMs{ 0.0f };
			float gpuFrameMs{ 0.0f };
			float edgesPerMs{ 0.0f };
			float verticesPerMs{ 0.0f };
		} latest{};
	} metrics{};

	VulkanExample() : VulkanExampleBase()
	{
		title = "Koch Snowflake - Persistent Thread Generator (Per-Frame Compute)";
		settings.vsync = false;
		configureExperimentFromArgs();
		configureMetricsFromArgs();
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

			if (metrics.queryPool != VK_NULL_HANDLE) {
				vkDestroyQueryPool(device, metrics.queryPool, nullptr);
			}
			for (uint32_t i = 0; i < maxConcurrentFrames; i++) {
				if (metrics.readbackMapped[i]) {
					vkUnmapMemory(device, metrics.readbackMem[i]);
				}
				vkDestroyBuffer(device, metrics.readbackBuf[i], nullptr);
				vkFreeMemory(device, metrics.readbackMem[i], nullptr);
			}
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

	bool hasArg(const char* name) const
	{
		for (const char* arg : args) {
			if (strcmp(arg, name) == 0) {
				return true;
			}
		}
		return false;
	}

	uint32_t getArgValue(const char* name, uint32_t fallback) const
	{
		for (size_t i = 0; i + 1 < args.size(); i++) {
			if (strcmp(args[i], name) == 0) {
				const int value = atoi(args[i + 1]);
				return value > 0 ? static_cast<uint32_t>(value) : fallback;
			}
		}
		return fallback;
	}

	const char* getArgString(const char* name) const
	{
		for (size_t i = 0; i + 1 < args.size(); i++) {
			if (strcmp(args[i], name) == 0) {
				return args[i + 1];
			}
		}
		return nullptr;
	}

	void configureExperimentFromArgs()
	{
		experiment.q1QueueShards = getArgValue("--wg-queue-shards", experiment.q1QueueShards);
		if (experiment.q1QueueShards > MAX_QUEUE_SHARDS) {
			experiment.q1QueueShards = MAX_QUEUE_SHARDS;
		}
		if ((QUEUE_SIZE % experiment.q1QueueShards) != 0u) {
			experiment.q1QueueShards = Q1_QUEUE_SHARDS_DEFAULT;
		}
		experiment.q1ShardCapacity = QUEUE_SIZE / experiment.q1QueueShards;
		experiment.q2QueueShards = getArgValue("--wg-q2-shards", experiment.q2QueueShards);
		if (experiment.q2QueueShards > MAX_QUEUE_SHARDS) {
			experiment.q2QueueShards = MAX_QUEUE_SHARDS;
		}
		if ((QUEUE_SIZE % experiment.q2QueueShards) != 0u) {
			experiment.q2QueueShards = Q2_QUEUE_SHARDS_DEFAULT;
		}
		experiment.q2ShardCapacity = QUEUE_SIZE / experiment.q2QueueShards;
		experiment.nodeCStart = getArgValue("--wg-node-c-start", experiment.nodeCStart);
		if (experiment.nodeCStart >= NUM_WORKGROUPS) {
			experiment.nodeCStart = NUM_WORKGROUPS - 1;
		}
		if (hasArg("--wg-q1-lane-pop")) {
			experiment.q1LanePop = true;
		}
		if (hasArg("--wg-no-q1-lane-pop")) {
			experiment.q1LanePop = false;
		}
		if (hasArg("--wg-q2-deq-batch")) {
			experiment.q2DequeueBatch = true;
		}
		if (hasArg("--wg-no-q2-deq-batch")) {
			experiment.q2DequeueBatch = false;
		}
		experiment.q2DequeueBatchLimit = getArgValue("--wg-q2-deq-batch-limit", experiment.q2DequeueBatchLimit);
	}

	void configureMetricsFromArgs()
	{
		const bool timestampsOnly = hasArg("--wg-timestamps-only");
		metrics.stdoutEnabled = hasArg("--wg-metrics-stdout") || timestampsOnly;
		if (const char* fileName = getArgString("--wg-metrics-file")) {
			metrics.fileStream.open(fileName, std::ios::out | std::ios::trunc);
		}
		metrics.enabled = hasArg("--wg-metrics") || metrics.stdoutEnabled || metrics.fileStream.is_open();
		metrics.shaderCountersEnabled = metrics.enabled && !timestampsOnly && !hasArg("--wg-no-shader-metrics");
		if (hasArg("--wg-no-metrics")) {
			metrics.enabled = false;
			metrics.stdoutEnabled = false;
			metrics.shaderCountersEnabled = false;
			if (metrics.fileStream.is_open()) {
				metrics.fileStream.close();
			}
		}
		metrics.stdoutInterval = getArgValue("--wg-metrics-interval", metrics.stdoutInterval);

#if defined(_WIN32)
		if (metrics.stdoutEnabled) {
			FILE* stream;
			if (AttachConsole(ATTACH_PARENT_PROCESS) || AttachConsole(GetCurrentProcessId())) {
				freopen_s(&stream, "CONOUT$", "w+", stdout);
				freopen_s(&stream, "CONOUT$", "w+", stderr);
			}
		}
#endif
	}

	void createBuffer(VkBuffer* buf, VkDeviceMemory* mem, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
	{
		VkBufferCreateInfo ci = vks::initializers::bufferCreateInfo(usage, size);
		VK_CHECK_RESULT(vkCreateBuffer(device, &ci, nullptr, buf));
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device, *buf, &memReqs);
		VkMemoryAllocateInfo ai = vks::initializers::memoryAllocateInfo();
		ai.allocationSize = memReqs.size;
		ai.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);
		VK_CHECK_RESULT(vkAllocateMemory(device, &ai, nullptr, mem));
		VK_CHECK_RESULT(vkBindBufferMemory(device, *buf, *mem, 0));
	}

	void prepareCompute()
	{
		VkBufferUsageFlags controlUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBufferUsageFlags queueUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		createBuffer(&compute.controlBuf, &compute.controlMem, sizeof(ControlBlock), controlUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(&compute.q1Buf, &compute.q1Mem, sizeof(Task) * QUEUE_SIZE, queueUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		createBuffer(&compute.q2Buf, &compute.q2Mem, sizeof(Task) * QUEUE_SIZE, queueUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (metrics.enabled) {
			for (uint32_t i = 0; i < maxConcurrentFrames; i++) {
				createBuffer(&metrics.readbackBuf[i], &metrics.readbackMem[i], sizeof(ControlBlock),
					VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
				VK_CHECK_RESULT(vkMapMemory(device, metrics.readbackMem[i], 0, sizeof(ControlBlock), 0, &metrics.readbackMapped[i]));
			}

			const VkQueueFamilyProperties& queueProps = vulkanDevice->queueFamilyProperties[vulkanDevice->queueFamilyIndices.graphics];
			metrics.timestampsSupported = (queueProps.timestampValidBits > 0) && (deviceProperties.limits.timestampPeriod > 0.0f);
			if (metrics.timestampsSupported) {
				VkQueryPoolCreateInfo queryPoolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
				queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
				queryPoolInfo.queryCount = TimestampCount * maxConcurrentFrames;
				VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, nullptr, &metrics.queryPool));
			}
		}

		// Output is deterministic, so graphics consumes the full vertex buffer
		// without CPU readback or indirect draw setup.
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

		// Constant IDs match headless.comp.
		struct {
			uint32_t queueSize;
			uint32_t maxDepth;
			uint32_t nodeCStart;
			uint32_t maxDepthEdges;
			uint32_t enableShaderMetrics;
			uint32_t q1QueueShards;
			uint32_t q2QueueShards;
			uint32_t enableQ1LanePop;
			uint32_t enableQ2DequeueBatch;
			uint32_t q2DequeueBatchLimit;
		} specData = {
			QUEUE_SIZE,
			MAX_DEPTH,
			experiment.nodeCStart,
			EXPECTED_EDGES,
			metrics.shaderCountersEnabled ? 1u : 0u,
			experiment.q1QueueShards,
			experiment.q2QueueShards,
			experiment.q1LanePop ? 1u : 0u,
			experiment.q2DequeueBatch ? 1u : 0u,
			experiment.q2DequeueBatchLimit
		};
		VkSpecializationMapEntry specEntries[10] = {
			{ 0, offsetof(decltype(specData), queueSize),     sizeof(uint32_t) },
			{ 1, offsetof(decltype(specData), maxDepth),      sizeof(uint32_t) },
			{ 2, offsetof(decltype(specData), nodeCStart),    sizeof(uint32_t) },
			{ 3, offsetof(decltype(specData), maxDepthEdges), sizeof(uint32_t) },
			{ 4, offsetof(decltype(specData), enableShaderMetrics), sizeof(uint32_t) },
			{ 5, offsetof(decltype(specData), q1QueueShards), sizeof(uint32_t) },
			{ 6, offsetof(decltype(specData), q2QueueShards), sizeof(uint32_t) },
			{ 7, offsetof(decltype(specData), enableQ1LanePop), sizeof(uint32_t) },
			{ 8, offsetof(decltype(specData), enableQ2DequeueBatch), sizeof(uint32_t) },
			{ 9, offsetof(decltype(specData), q2DequeueBatchLimit), sizeof(uint32_t) }
		};
		VkSpecializationInfo specInfo = {};
		specInfo.mapEntryCount = 10;
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

	void writeTimestamp(VkCommandBuffer cmd, TimestampQuery query, VkPipelineStageFlagBits stage)
	{
		if (metrics.enabled && metrics.timestampsSupported) {
			vkCmdWriteTimestamp(cmd, stage, metrics.queryPool, currentBuffer * TimestampCount + query);
		}
	}

	float timestampDeltaMs(uint64_t begin, uint64_t end) const
	{
		return static_cast<float>(end - begin) * deviceProperties.limits.timestampPeriod / 1000000.0f;
	}

	float ratio(uint32_t numerator, uint32_t denominator) const
	{
		return denominator == 0 ? 0.0f : static_cast<float>(numerator) / static_cast<float>(denominator);
	}

	float ratio(uint64_t numerator, uint64_t denominator) const
	{
		return denominator == 0 ? 0.0f : static_cast<float>(static_cast<double>(numerator) / static_cast<double>(denominator));
	}

	QueueMetrics aggregateQ1Metrics(const ControlBlock& control) const
	{
		QueueMetrics total{};
		for (uint32_t i = 0; i < experiment.q1QueueShards; i++) {
			const QueueMetrics& shard = control.metrics.q1Shards[i];
			total.enqueueAttempts += shard.enqueueAttempts;
			total.enqueueSuccess += shard.enqueueSuccess;
			total.enqueueCasFail += shard.enqueueCasFail;
			total.enqueueFull += shard.enqueueFull;
			total.dequeueAttempts += shard.dequeueAttempts;
			total.dequeueSuccess += shard.dequeueSuccess;
			total.dequeueCasFail += shard.dequeueCasFail;
			total.dequeueEmpty += shard.dequeueEmpty;
			total.readyExchange += shard.readyExchange;
			total.readyCasAttempts += shard.readyCasAttempts;
			total.readyCasSuccess += shard.readyCasSuccess;
			total.readyCasFail += shard.readyCasFail;
			total.readyMaxSpin = std::max(total.readyMaxSpin, shard.readyMaxSpin);
			total.highWater += shard.highWater;
		}
		return total;
	}

	QueueMetrics aggregateQ2Metrics(const ControlBlock& control) const
	{
		QueueMetrics total{};
		for (uint32_t i = 0; i < experiment.q2QueueShards; i++) {
			const QueueMetrics& shard = control.metrics.q2Shards[i];
			total.enqueueAttempts += shard.enqueueAttempts;
			total.enqueueSuccess += shard.enqueueSuccess;
			total.enqueueCasFail += shard.enqueueCasFail;
			total.enqueueFull += shard.enqueueFull;
			total.dequeueAttempts += shard.dequeueAttempts;
			total.dequeueSuccess += shard.dequeueSuccess;
			total.dequeueCasFail += shard.dequeueCasFail;
			total.dequeueEmpty += shard.dequeueEmpty;
			total.readyExchange += shard.readyExchange;
			total.readyCasAttempts += shard.readyCasAttempts;
			total.readyCasSuccess += shard.readyCasSuccess;
			total.readyCasFail += shard.readyCasFail;
			total.readyMaxSpin = std::max(total.readyMaxSpin, shard.readyMaxSpin);
			total.highWater += shard.highWater;
		}
		return total;
	}

	uint32_t maxQ1Metric(const ControlBlock& control, uint32_t QueueMetrics::*field) const
	{
		uint32_t value = 0;
		for (uint32_t i = 0; i < experiment.q1QueueShards; i++) {
			value = std::max(value, control.metrics.q1Shards[i].*field);
		}
		return value;
	}

	float meanQ1Metric(const ControlBlock& control, uint32_t QueueMetrics::*field) const
	{
		uint64_t total = 0;
		for (uint32_t i = 0; i < experiment.q1QueueShards; i++) {
			total += control.metrics.q1Shards[i].*field;
		}
		return static_cast<float>(static_cast<double>(total) / static_cast<double>(experiment.q1QueueShards));
	}

	uint32_t maxQ1MetricSum(const ControlBlock& control, uint32_t QueueMetrics::*first, uint32_t QueueMetrics::*second) const
	{
		uint32_t value = 0;
		for (uint32_t i = 0; i < experiment.q1QueueShards; i++) {
			value = std::max(value, control.metrics.q1Shards[i].*first + control.metrics.q1Shards[i].*second);
		}
		return value;
	}

	float meanQ1MetricSum(const ControlBlock& control, uint32_t QueueMetrics::*first, uint32_t QueueMetrics::*second) const
	{
		uint64_t total = 0;
		for (uint32_t i = 0; i < experiment.q1QueueShards; i++) {
			total += static_cast<uint64_t>(control.metrics.q1Shards[i].*first) + static_cast<uint64_t>(control.metrics.q1Shards[i].*second);
		}
		return static_cast<float>(static_cast<double>(total) / static_cast<double>(experiment.q1QueueShards));
	}

	void collectCompletedMetrics(uint32_t bufferIndex)
	{
		if (!metrics.enabled || !metrics.submitted[bufferIndex]) {
			return;
		}

		memcpy(&metrics.latest.control, metrics.readbackMapped[bufferIndex], sizeof(ControlBlock));
		metrics.latest.valid = true;
		metrics.latest.timestampsValid = false;
		metrics.latest.resetMs = 0.0f;
		metrics.latest.resetBarrierMs = 0.0f;
		metrics.latest.computeMs = 0.0f;
		metrics.latest.metricsCopyMs = 0.0f;
		metrics.latest.renderMs = 0.0f;
		metrics.latest.gpuFrameMs = 0.0f;

		if (metrics.timestampsSupported) {
			std::array<uint64_t, TimestampCount> timestamps{};
			VkResult result = vkGetQueryPoolResults(device, metrics.queryPool, bufferIndex * TimestampCount, TimestampCount,
				sizeof(uint64_t) * timestamps.size(), timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
			if (result == VK_SUCCESS) {
				metrics.latest.timestamps = timestamps;
				metrics.latest.timestampsValid = true;
				metrics.latest.resetMs = timestampDeltaMs(timestamps[TimestampFrameStart], timestamps[TimestampAfterReset]);
				metrics.latest.resetBarrierMs = timestampDeltaMs(timestamps[TimestampAfterReset], timestamps[TimestampAfterResetBarrier]);
				metrics.latest.computeMs = timestampDeltaMs(timestamps[TimestampAfterResetBarrier], timestamps[TimestampAfterCompute]);
				metrics.latest.metricsCopyMs = timestampDeltaMs(timestamps[TimestampAfterCompute], timestamps[TimestampAfterMetricsCopy]);
				metrics.latest.renderMs = timestampDeltaMs(timestamps[TimestampAfterMetricsCopy], timestamps[TimestampAfterRender]);
				metrics.latest.gpuFrameMs = timestampDeltaMs(timestamps[TimestampFrameStart], timestamps[TimestampAfterRender]);
			}
		}

		if (metrics.latest.computeMs > 0.0f) {
			metrics.latest.edgesPerMs = static_cast<float>(metrics.latest.control.totalProcessed) / metrics.latest.computeMs;
			metrics.latest.verticesPerMs = static_cast<float>(metrics.latest.control.vertexCount) / metrics.latest.computeMs;
		} else {
			metrics.latest.edgesPerMs = 0.0f;
			metrics.latest.verticesPerMs = 0.0f;
		}

		metrics.completedFrames++;
		printMetricsToStdout();
		metrics.submitted[bufferIndex] = false;
	}

	void writeMetrics(std::ostream& out, bool& headerPrinted)
	{
		const ControlBlock& control = metrics.latest.control;
		const DebugCounters& c = control.metrics;
		const QueueMetrics q1 = aggregateQ1Metrics(control);
		const QueueMetrics q2 = aggregateQ2Metrics(control);
		const float q1DeqAttemptsMean = meanQ1Metric(control, &QueueMetrics::dequeueAttempts);
		const uint32_t q1DeqAttemptsMax = maxQ1Metric(control, &QueueMetrics::dequeueAttempts);
		const float q1EnqAttemptsMean = meanQ1Metric(control, &QueueMetrics::enqueueAttempts);
		const uint32_t q1EnqAttemptsMax = maxQ1Metric(control, &QueueMetrics::enqueueAttempts);
		const float q1DeqCasFailMean = meanQ1Metric(control, &QueueMetrics::dequeueCasFail);
		const uint32_t q1DeqCasFailMax = maxQ1Metric(control, &QueueMetrics::dequeueCasFail);
		const float q1DeqProbeMean = meanQ1MetricSum(control, &QueueMetrics::dequeueAttempts, &QueueMetrics::dequeueEmpty);
		const uint32_t q1DeqProbeMax = maxQ1MetricSum(control, &QueueMetrics::dequeueAttempts, &QueueMetrics::dequeueEmpty);
		const float q1HighWaterMean = meanQ1Metric(control, &QueueMetrics::highWater);
		const uint32_t q1HighWaterMax = maxQ1Metric(control, &QueueMetrics::highWater);
		const float q1DeqAttemptsImbalance = q1DeqAttemptsMean > 0.0f ? static_cast<float>(q1DeqAttemptsMax) / q1DeqAttemptsMean : 0.0f;
		const float q1EnqAttemptsImbalance = q1EnqAttemptsMean > 0.0f ? static_cast<float>(q1EnqAttemptsMax) / q1EnqAttemptsMean : 0.0f;
		const float q1DeqProbeImbalance = q1DeqProbeMean > 0.0f ? static_cast<float>(q1DeqProbeMax) / q1DeqProbeMean : 0.0f;

		if (!headerPrinted) {
			out
				<< "WG_METRICS_HEADER frame,q1_shards,q1_shard_capacity,gpu_frame_ms,reset_ms,reset_barrier_ms,compute_ms,metrics_copy_ms,render_ms,"
				<< "q2_shards,q2_shard_capacity,"
				<< "edges,vertices,edges_per_ms,vertices_per_ms,"
				<< "q1_deq_attempt_mean,q1_deq_attempt_max,q1_deq_attempt_imbalance,q1_deq_cas_fail_mean,q1_deq_cas_fail_max,"
				<< "q1_deq_probe_mean,q1_deq_probe_max,q1_deq_probe_imbalance,"
				<< "q1_enq_attempt_mean,q1_enq_attempt_max,q1_enq_attempt_imbalance,q1_high_water_mean,q1_high_water_max,"
				<< "q1_enq_ok,q1_enq_cas_fail,q1_enq_full,q1_deq_ok,q1_deq_cas_fail,q1_deq_empty,q1_ready_cas_fail,q1_ready_max_spin,q1_high_water,"
				<< "q2_enq_ok,q2_enq_cas_fail,q2_enq_full,q2_deq_ok,q2_deq_cas_fail,q2_deq_empty,q2_ready_cas_fail,q2_ready_max_spin,q2_high_water,"
				<< "q2_deq_batches,q2_deq_batch_slots,q2_deq_batch_cas_attempts,q2_deq_batch_cas_fail,q2_deq_batch_empty,q2_deq_batch_partial,"
				<< "q2_deq_slots_per_batch,q2_deq_batch_enabled,q2_deq_batch_limit,"
				<< "node_a_seed,node_b_tasks,node_b_subdivide,node_b_final,node_c_output,stop_writes\n";
			headerPrinted = true;
		}

		out << std::fixed << std::setprecision(4)
			<< "WG_METRICS "
			<< metrics.completedFrames << ","
			<< experiment.q1QueueShards << ","
			<< experiment.q1ShardCapacity << ","
			<< metrics.latest.gpuFrameMs << ","
			<< metrics.latest.resetMs << ","
			<< metrics.latest.resetBarrierMs << ","
			<< metrics.latest.computeMs << ","
			<< metrics.latest.metricsCopyMs << ","
			<< metrics.latest.renderMs << ","
			<< experiment.q2QueueShards << ","
			<< experiment.q2ShardCapacity << ","
			<< control.totalProcessed << ","
			<< control.vertexCount << ","
			<< metrics.latest.edgesPerMs << ","
			<< metrics.latest.verticesPerMs << ","
			<< q1DeqAttemptsMean << ","
			<< q1DeqAttemptsMax << ","
			<< q1DeqAttemptsImbalance << ","
			<< q1DeqCasFailMean << ","
			<< q1DeqCasFailMax << ","
			<< q1DeqProbeMean << ","
			<< q1DeqProbeMax << ","
			<< q1DeqProbeImbalance << ","
			<< q1EnqAttemptsMean << ","
			<< q1EnqAttemptsMax << ","
			<< q1EnqAttemptsImbalance << ","
			<< q1HighWaterMean << ","
			<< q1HighWaterMax << ","
			<< q1.enqueueSuccess << ","
			<< q1.enqueueCasFail << ","
			<< q1.enqueueFull << ","
			<< q1.dequeueSuccess << ","
			<< q1.dequeueCasFail << ","
			<< q1.dequeueEmpty << ","
			<< q1.readyCasFail << ","
			<< q1.readyMaxSpin << ","
			<< q1.highWater << ","
			<< q2.enqueueSuccess << ","
			<< q2.enqueueCasFail << ","
			<< q2.enqueueFull << ","
			<< q2.dequeueSuccess << ","
			<< q2.dequeueCasFail << ","
			<< q2.dequeueEmpty << ","
			<< q2.readyCasFail << ","
			<< q2.readyMaxSpin << ","
			<< q2.highWater << ","
			<< c.q2DequeueBatches << ","
			<< c.q2DequeueBatchSlots << ","
			<< c.q2DequeueBatchCasAttempts << ","
			<< c.q2DequeueBatchCasFail << ","
			<< c.q2DequeueBatchEmpty << ","
			<< c.q2DequeueBatchPartial << ","
			<< ratio(c.q2DequeueBatchSlots, c.q2DequeueBatches) << ","
			<< (experiment.q2DequeueBatch ? 1u : 0u) << ","
			<< experiment.q2DequeueBatchLimit << ","
			<< c.nodeASeedEdges << ","
			<< c.nodeBTasks << ","
			<< c.nodeBSubdivideTasks << ","
			<< c.nodeBFinalTasks << ","
			<< c.nodeCOutputEdges << ","
			<< c.stopFlagWrites << "\n";
	}

	void printMetricsToStdout()
	{
		if ((metrics.completedFrames % metrics.stdoutInterval) != 0) {
			return;
		}
		if (metrics.stdoutEnabled) {
			writeMetrics(std::cout, metrics.stdoutHeaderPrinted);
		}
		if (metrics.fileStream.is_open()) {
			writeMetrics(metrics.fileStream, metrics.fileHeaderPrinted);
			metrics.fileStream.flush();
		}
	}

	void buildCommandBuffers()
	{
		VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &cmdBufInfo));

		if (metrics.enabled && metrics.timestampsSupported) {
			vkCmdResetQueryPool(cmd, metrics.queryPool, currentBuffer * TimestampCount, TimestampCount);
		}
		writeTimestamp(cmd, TimestampFrameStart, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

		{
			ScopedCmdLabel label(cmd, "WG GPU reset", glm::vec4(0.95f, 0.66f, 0.18f, 1.0f));
			vkCmdFillBuffer(cmd, compute.controlBuf, 0, sizeof(ControlBlock), 0);
			vkCmdFillBuffer(cmd, compute.q1Buf, 0, sizeof(Task) * QUEUE_SIZE, 0);
			vkCmdFillBuffer(cmd, compute.q2Buf, 0, sizeof(Task) * QUEUE_SIZE, 0);
		}
		writeTimestamp(cmd, TimestampAfterReset, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

		{
			ScopedCmdLabel label(cmd, "WG reset-to-compute barrier", glm::vec4(0.52f, 0.74f, 0.95f, 1.0f));
			VkMemoryBarrier fillBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
			fillBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			fillBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 1, &fillBarrier, 0, nullptr, 0, nullptr);
		}
		writeTimestamp(cmd, TimestampAfterResetBarrier, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

		{
			ScopedCmdLabel label(cmd, "WG compute dispatch", glm::vec4(0.27f, 0.82f, 0.55f, 1.0f));
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);
			vkCmdDispatch(cmd, NUM_WORKGROUPS, 1, 1);
		}
		writeTimestamp(cmd, TimestampAfterCompute, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

		{
			ScopedCmdLabel label(cmd, "WG post-compute barrier", glm::vec4(0.46f, 0.62f, 0.96f, 1.0f));
			std::array<VkBufferMemoryBarrier, 2> postComputeBarriers{};
			postComputeBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			postComputeBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			postComputeBarriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			postComputeBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postComputeBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			postComputeBarriers[0].buffer = vertexBuffer.buffer;
			postComputeBarriers[0].offset = 0;
			postComputeBarriers[0].size = VK_WHOLE_SIZE;
			uint32_t postComputeBarrierCount = 1;
			if (metrics.enabled) {
				postComputeBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
				postComputeBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				postComputeBarriers[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				postComputeBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				postComputeBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				postComputeBarriers[1].buffer = compute.controlBuf;
				postComputeBarriers[1].offset = 0;
				postComputeBarriers[1].size = sizeof(ControlBlock);
				postComputeBarrierCount = 2;
			}
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				metrics.enabled ? (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT) : VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
				0, 0, nullptr, postComputeBarrierCount, postComputeBarriers.data(), 0, nullptr);
		}

		if (metrics.enabled) {
			ScopedCmdLabel label(cmd, "WG metrics copy", glm::vec4(0.84f, 0.53f, 0.94f, 1.0f));
			VkBufferCopy metricsCopy = {};
			metricsCopy.size = sizeof(ControlBlock);
			vkCmdCopyBuffer(cmd, compute.controlBuf, metrics.readbackBuf[currentBuffer], 1, &metricsCopy);

			VkBufferMemoryBarrier readbackBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
			readbackBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			readbackBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
			readbackBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			readbackBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			readbackBarrier.buffer = metrics.readbackBuf[currentBuffer];
			readbackBarrier.offset = 0;
			readbackBarrier.size = sizeof(ControlBlock);
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_HOST_BIT,
				0, 0, nullptr, 1, &readbackBarrier, 0, nullptr);
		}
		writeTimestamp(cmd, TimestampAfterMetricsCopy, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

		{
			ScopedCmdLabel label(cmd, "WG render", glm::vec4(0.95f, 0.46f, 0.36f, 1.0f));
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
			// Koch output count is deterministic for MAX_DEPTH, so no hot-path
			// readback is needed before drawing.
			vkCmdDraw(cmd, EXPECTED_VERTICES, 1, 0, 0);

			drawUI(cmd);

			vkCmdEndRenderPass(cmd);
		}
		writeTimestamp(cmd, TimestampAfterRender, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		prepareCompute();
		prepareGraphicsPipeline();
		prepared = true;
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (!metrics.enabled) {
			overlay->text("Metrics disabled. Use --wg-metrics or --wg-metrics-stdout.");
			return;
		}
		if (!metrics.latest.valid) {
			overlay->text("Metrics: waiting for completed GPU frame");
			return;
		}

		const ControlBlock& control = metrics.latest.control;
		const DebugCounters& c = control.metrics;
		const QueueMetrics q1 = aggregateQ1Metrics(control);
		const QueueMetrics q2 = aggregateQ2Metrics(control);

		if (overlay->header("GPU metrics")) {
			if (metrics.latest.timestampsValid) {
				overlay->text("GPU frame: %.3f ms", metrics.latest.gpuFrameMs);
				overlay->text("Reset: %.3f ms, reset barrier: %.3f ms", metrics.latest.resetMs, metrics.latest.resetBarrierMs);
				overlay->text("Compute: %.3f ms, metrics copy: %.3f ms", metrics.latest.computeMs, metrics.latest.metricsCopyMs);
				overlay->text("Render: %.3f ms", metrics.latest.renderMs);
			} else {
				overlay->text("Timestamps unavailable");
			}
			overlay->text("Edges: %u / %u", control.totalProcessed, EXPECTED_EDGES);
			overlay->text("Vertices: %u / %u", control.vertexCount, EXPECTED_VERTICES);
			overlay->text("Throughput: %.1f edges/ms, %.1f vertices/ms", metrics.latest.edgesPerMs, metrics.latest.verticesPerMs);
		}

		if (overlay->header("Work throughput")) {
			overlay->text("Node A seed edges: %u", c.nodeASeedEdges);
			overlay->text("Node B tasks: %u", c.nodeBTasks);
			overlay->text("Node B subdivide/final: %u / %u", c.nodeBSubdivideTasks, c.nodeBFinalTasks);
			overlay->text("Node C output edges: %u", c.nodeCOutputEdges);
		}

		if (overlay->header("Queue pressure")) {
			overlay->text("Q1 shards/capacity: %u / %u", experiment.q1QueueShards, experiment.q1ShardCapacity);
			overlay->text("Q2 shards/capacity: %u / %u", experiment.q2QueueShards, experiment.q2ShardCapacity);
			overlay->text("Q1 enq ok/CAS fail/full: %u / %u / %u", q1.enqueueSuccess, q1.enqueueCasFail, q1.enqueueFull);
			overlay->text("Q1 deq ok/CAS fail/empty: %u / %u / %u", q1.dequeueSuccess, q1.dequeueCasFail, q1.dequeueEmpty);
			overlay->text("Q1 high-water sum/max: %u / %u", q1.highWater, maxQ1Metric(control, &QueueMetrics::highWater));
			overlay->text("Q2 enq ok/CAS fail/full: %u / %u / %u", q2.enqueueSuccess, q2.enqueueCasFail, q2.enqueueFull);
			overlay->text("Q2 deq ok/CAS fail/empty: %u / %u / %u", q2.dequeueSuccess, q2.dequeueCasFail, q2.dequeueEmpty);
			overlay->text("Q2 high-water: %u", q2.highWater);
			overlay->text("Q2 deq batches/slots/slots per batch: %u / %u / %.2f",
				c.q2DequeueBatches, c.q2DequeueBatchSlots, ratio(c.q2DequeueBatchSlots, c.q2DequeueBatches));
		}

		if (overlay->header("Atomic pressure")) {
			overlay->text("Q1 CAS fail/success: %.2f", ratio(q1.enqueueCasFail + q1.dequeueCasFail + q1.readyCasFail,
				q1.enqueueSuccess + q1.dequeueSuccess + q1.readyCasSuccess));
			overlay->text("Q2 CAS fail/success: %.2f", ratio(q2.enqueueCasFail + q2.dequeueCasFail + q2.readyCasFail,
				q2.enqueueSuccess + q2.dequeueSuccess + q2.readyCasSuccess));
			overlay->text("Q1 ready CAS attempts/fail/max spin: %u / %u / %u", q1.readyCasAttempts, q1.readyCasFail, q1.readyMaxSpin);
			overlay->text("Q2 ready CAS attempts/fail/max spin: %u / %u / %u", q2.readyCasAttempts, q2.readyCasFail, q2.readyMaxSpin);
			overlay->text("Workgroups B/C: %u / %u", experiment.nodeCStart, NUM_WORKGROUPS - experiment.nodeCStart);
			overlay->text("Q1 lane pop: %s", experiment.q1LanePop ? "on" : "off");
			overlay->text("Q2 dequeue batching: %s", experiment.q2DequeueBatch ? "on" : "off");
			overlay->text("Q2 dequeue batch limit: %u", experiment.q2DequeueBatchLimit);
			overlay->text("Shader metrics: %s", metrics.shaderCountersEnabled ? "on" : "off");
		}
	}

	void render() override
	{
		if (!prepared)
			return;
		VK_CHECK_RESULT(vkWaitForFences(device, 1, &waitFences[currentBuffer], VK_TRUE, UINT64_MAX));
		VK_CHECK_RESULT(vkResetFences(device, 1, &waitFences[currentBuffer]));
		collectCompletedMetrics(currentBuffer);
		prepareFrame(false);
		buildCommandBuffers();
		const uint32_t submittedBuffer = currentBuffer;
		submitFrame();
		if (metrics.enabled) {
			metrics.submitted[submittedBuffer] = true;
		}
	}
};

VULKAN_EXAMPLE_MAIN()
