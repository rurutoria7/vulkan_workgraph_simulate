/*
* Vulkan Example - Persistent Thread Koch Snowflake Generator
* Koch snowflake fractal subdivision via persistent compute threads
*/

#if defined(_WIN32)
#pragma comment(linker, "/subsystem:console")
#include <conio.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>

#include <vulkan/vulkan.h>
#include "VulkanTools.h"
#include "CommandLineParser.hpp"

#define DEBUG (!NDEBUG)

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

#define QUEUE_SIZE 4096
#define MAX_DEPTH 4
#define NUM_WORKGROUPS 8
#define NODE_C_START 6   // workgroups >= this do Node C
#define EXPECTED_EDGES (3 * (1 << (2 * MAX_DEPTH)))  // 3 * 4^MAX_DEPTH = 768
#define EXPECTED_VERTICES (EXPECTED_EDGES * 2)        // 1536

#define LOG(...) printf(__VA_ARGS__)

class VulkanExampleWorkgraphPOC
{
public:
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	uint32_t queueFamilyIndex;
	VkQueue queue;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkFence fence;
	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkShaderModule shaderModule;

	struct {
		VkBuffer control;
		VkDeviceMemory controlMem;
		VkBuffer queue1;
		VkDeviceMemory queue1Mem;
		VkBuffer queue2;
		VkDeviceMemory queue2Mem;
		VkBuffer output;
		VkDeviceMemory outputMem;
	} buffers;

	VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data = nullptr)
	{
		VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
		VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, buffer));
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
		VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);
		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		bool memTypeFound = false;
		for (uint32_t i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
			if ((memReqs.memoryTypeBits & 1) == 1) {
				if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & memoryPropertyFlags) == memoryPropertyFlags) {
					memAlloc.memoryTypeIndex = i;
					memTypeFound = true;
					break;
				}
			}
			memReqs.memoryTypeBits >>= 1;
		}
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, memory));
		if (data != nullptr) {
			void *mapped;
			VK_CHECK_RESULT(vkMapMemory(device, *memory, 0, size, 0, &mapped));
			memcpy(mapped, data, size);
			vkUnmapMemory(device, *memory);
		}
		VK_CHECK_RESULT(vkBindBufferMemory(device, *buffer, *memory, 0));
		return VK_SUCCESS;
	}

	VulkanExampleWorkgraphPOC()
	{
		LOG("Koch Snowflake: Initializing Persistent Thread Generator\n");
		LOG("  MAX_DEPTH=%d, Workgroups=%d (Node B: 0-%d, Node C: %d-%d)\n", MAX_DEPTH, NUM_WORKGROUPS, NODE_C_START-1, NODE_C_START, NUM_WORKGROUPS-1);
		LOG("  Expected edges=%d, Expected vertices=%d\n", EXPECTED_EDGES, EXPECTED_VERTICES);

		VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
		appInfo.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
		instanceCreateInfo.pApplicationInfo = &appInfo;
		VK_CHECK_RESULT(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));

		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
		physicalDevice = physicalDevices[0];

		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
		LOG("  GPU: %s\n", deviceProperties.deviceName);

		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
		for (uint32_t i = 0; i < queueFamilyCount; i++) {
			if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				queueFamilyIndex = i;
				break;
			}
		}

		const float defaultQueuePriority(0.0f);
		VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
		queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &defaultQueuePriority;
		VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
		deviceCreateInfo.queueCreateInfoCount = 1;
		deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
		VK_CHECK_RESULT(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

		vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

		VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

		// Buffers
		ControlBlock initControl = {};
		std::vector<Task> zeroTasks(QUEUE_SIZE, Task{});
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffers.control, &buffers.controlMem, sizeof(ControlBlock), &initControl);
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffers.queue1, &buffers.queue1Mem, sizeof(Task) * QUEUE_SIZE, zeroTasks.data());
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffers.queue2, &buffers.queue2Mem, sizeof(Task) * QUEUE_SIZE, zeroTasks.data());
		// Output vertex buffer: EXPECTED_VERTICES * sizeof(vec2) = 1536 * 8 = 12288 bytes
		createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffers.output, &buffers.outputMem, EXPECTED_VERTICES * sizeof(float) * 2);

		// Pipeline
		{
			std::vector<VkDescriptorPoolSize> poolSizes = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4) };
			VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 1);
			VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
				vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3),
			};
			VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

			VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

			VkDescriptorBufferInfo controlDescriptor = { buffers.control, 0, VK_WHOLE_SIZE };
			VkDescriptorBufferInfo queue1Descriptor = { buffers.queue1, 0, VK_WHOLE_SIZE };
			VkDescriptorBufferInfo queue2Descriptor = { buffers.queue2, 0, VK_WHOLE_SIZE };
			VkDescriptorBufferInfo outputDescriptor = { buffers.output, 0, VK_WHOLE_SIZE };
			std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &controlDescriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &queue1Descriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &queue2Descriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &outputDescriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, NULL);

			const std::string shaderPath = getShaderBasePath() + "workgraph_poc/headless.comp.spv";
			shaderModule = vks::tools::loadShader(shaderPath.c_str(), device);
			VkPipelineShaderStageCreateInfo shaderStage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
			shaderStage.module = shaderModule;
			shaderStage.pName = "main";

			// Specialization constants: QUEUE_SIZE, MAX_DEPTH, NODE_C_START
			struct {
				uint32_t queueSize;
				uint32_t maxDepth;
				uint32_t nodeCStart;
			} specData = { QUEUE_SIZE, MAX_DEPTH, NODE_C_START };
			VkSpecializationMapEntry specEntries[3] = {
				{ 0, offsetof(decltype(specData), queueSize),  sizeof(uint32_t) },
				{ 1, offsetof(decltype(specData), maxDepth),   sizeof(uint32_t) },
				{ 2, offsetof(decltype(specData), nodeCStart), sizeof(uint32_t) },
			};
			VkSpecializationInfo specInfo = {};
			specInfo.mapEntryCount = 3;
			specInfo.pMapEntries = specEntries;
			specInfo.dataSize = sizeof(specData);
			specInfo.pData = &specData;
			shaderStage.pSpecializationInfo = &specInfo;

			VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayout, 0);
			computePipelineCreateInfo.stage = shaderStage;
			VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &pipeline));

			VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &commandBuffer));
			VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(0);
			VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
		}

		// Dispatch
		{
			VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &cmdBufInfo));
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, 0);
			vkCmdDispatch(commandBuffer, NUM_WORKGROUPS, 1, 1);
			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;
			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		}

		// Monitoring
		LOG("Koch Snowflake generation running (Node A -> Node B -> Node C)\n");
		LOG("Waiting for %d vertices (%d edges)...\n", EXPECTED_VERTICES, EXPECTED_EDGES);
		ControlBlock* mappedControl;
		VK_CHECK_RESULT(vkMapMemory(device, buffers.controlMem, 0, sizeof(ControlBlock), 0, (void**)&mappedControl));

		auto startTime = std::chrono::high_resolution_clock::now();
		bool completed = false;
		while (!completed) {
			uint32_t vc = mappedControl->vertexCount;
			uint32_t processed = mappedControl->totalProcessed;
			LOG("\rVertices: %u/%u | Processed: %u/%u | Q1: %u | Q2: %u",
				vc, EXPECTED_VERTICES, processed, EXPECTED_EDGES,
				mappedControl->q1.count, mappedControl->q2.count);

			if (vc >= EXPECTED_VERTICES) {
				completed = true;
				mappedControl->stopFlag = 1;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		auto endTime = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

		VK_CHECK_RESULT(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
		LOG("\nGeneration complete in %lld us\n", duration);
		LOG("Final: %u vertices, %u edges processed\n", mappedControl->vertexCount, mappedControl->totalProcessed);
		vkUnmapMemory(device, buffers.controlMem);

		// Read back and print first few vertices for verification
		float* mappedOutput;
		VK_CHECK_RESULT(vkMapMemory(device, buffers.outputMem, 0, EXPECTED_VERTICES * sizeof(float) * 2, 0, (void**)&mappedOutput));
		LOG("\nFirst 10 line segments (of %d):\n", EXPECTED_EDGES);
		for (int i = 0; i < 10 && i < EXPECTED_EDGES; i++) {
			int base = i * 4;  // 2 vertices * 2 floats per vertex
			LOG("  [%d] (%.4f, %.4f) -> (%.4f, %.4f)\n", i,
				mappedOutput[base], mappedOutput[base+1],
				mappedOutput[base+2], mappedOutput[base+3]);
		}
		vkUnmapMemory(device, buffers.outputMem);
	}

	~VulkanExampleWorkgraphPOC()
	{
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyFence(device, fence, nullptr);
		vkDestroyCommandPool(device, commandPool, nullptr);
		vkDestroyShaderModule(device, shaderModule, nullptr);
		vkDestroyBuffer(device, buffers.control, nullptr);
		vkFreeMemory(device, buffers.controlMem, nullptr);
		vkDestroyBuffer(device, buffers.queue1, nullptr);
		vkFreeMemory(device, buffers.queue1Mem, nullptr);
		vkDestroyBuffer(device, buffers.queue2, nullptr);
		vkFreeMemory(device, buffers.queue2Mem, nullptr);
		vkDestroyBuffer(device, buffers.output, nullptr);
		vkFreeMemory(device, buffers.outputMem, nullptr);
		vkDestroyDevice(device, nullptr);
		vkDestroyInstance(instance, nullptr);
	}
};

int main(int argc, char* argv[]) {
	VulkanExampleWorkgraphPOC *vulkanExample = new VulkanExampleWorkgraphPOC();
	delete(vulkanExample);
	return 0;
}
