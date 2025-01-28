#include "gpu.h"

#include "log.h"
#include "oc.h"
#include "vk/swapchain.h"
#include "vks.h"

#include <vector>
#include <set>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);

void dump_available_validation_layers();
void dump_available_physical_devices(VkInstance instance);
void dump_available_physical_extensions(VkPhysicalDevice device);
void dump_available_instance_extensions();
bool is_device_suitable(VkPhysicalDevice device, slice<const char *> required_extensions);

template <usize N>
bool check_validation_layer_support(const char * (&validation_layers)[N]);

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

static logger_t gpu_log = logger_t("gpu");

void gpu_t::init(GLFWwindow *window) {
    this->window = window;

    bool requests_validation_layers = true;

    const char *validation_layers[] = {
        "VK_LAYER_KHRONOS_validation"
    };


    std::vector<const char *> required_device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };

    bool validation_layers_available = true;
    if (requests_validation_layers && !check_validation_layer_support(validation_layers)) {
        gpu_log.error("validation layers requested, but not available");
        dump_available_validation_layers();
        gpu_log.info("proceeding without validation layers");
        validation_layers_available = false;
    }

    if (validation_layers_available) {
        gpu_log.info("validation layers enabled");
    }

    {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        if (validation_layers_available) {
            createInfo.enabledLayerCount = array_size(validation_layers);
            createInfo.ppEnabledLayerNames = validation_layers;
        } else {
            createInfo.enabledLayerCount = 0;
        }

        std::vector<const char*> extensions;
        {
            uint32_t glfwExtensionCount = 0;
            const char** glfwExtensions;
            glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

            for (u32 i = 0; i < glfwExtensionCount; ++i) {
                extensions.push_back(glfwExtensions[i]);
            }

            if (validation_layers_available) {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }

            createInfo.enabledExtensionCount = extensions.size();
            createInfo.ppEnabledExtensionNames = extensions.data();
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
        if (result != VK_SUCCESS) {
            gpu_log.error("failed to create instance: {}", vk_result_to_cstr(result));
            dump_available_instance_extensions();

            return;
        }
        gpu_log.info("vulkan instance created");
    }

    // setup the debug logger
    if (validation_layers_available) {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = vk_debug_callback;
        createInfo.pUserData = nullptr; // Optional

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

        if(func(instance, &createInfo, nullptr, &debug_messager)) {
            g_log.error("failed to setup debug messenger");
        }
    }

    // select a physical device
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            gpu_log.error("failed to find GPUs with Vulkan support");
            return;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        auto req_exts = slice<const char *>(required_device_extensions.data(), required_device_extensions.size());
        for (const auto& device : devices) {
            if (is_device_suitable(device, req_exts)) {
                pdev = device;
                break;
            }
        }

        if (pdev == VK_NULL_HANDLE) {
            gpu_log.error("failed to find a suitable GPU");
            dump_available_physical_devices(instance);
            return;
        }

        const char *device_name = "\0";
        const char *api_version = "unknown";
        {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(pdev, &device_properties);
            device_name = device_properties.deviceName;
            api_version = fmt::format("{}.{}.{}",
                VK_VERSION_MAJOR(device_properties.apiVersion),
                VK_VERSION_MINOR(device_properties.apiVersion),
                VK_VERSION_PATCH(device_properties.apiVersion)).c_str();
        }
        (void)device_name;
        (void)api_version;
        // gpu_log.info("selected physical device: {}", device_name);
        // gpu_log.info("vukan version: {}", api_version);
    }

    {
        // create surface
        if(glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            gpu_log.error("failed to create window surface");
            return;
        }
        gpu_log.info("window surface created");
    }

    bool qfamily_graphics_found = false;
    bool qfamily_present_found = false;

    u32 qfamily_graphics;
    u32 qfamily_present;
    {
        // find queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (qfamily_graphics_found && qfamily_present_found) {
                break;
            }

            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                qfamily_graphics_found = true;
                qfamily_graphics = i;
            }

            if (qfamily_graphics_found) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(pdev, i, surface, &presentSupport);
                if (presentSupport) {
                    qfamily_present_found = true;
                    qfamily_present = i;
                }
            }

            i++;
        }

        if (!qfamily_graphics_found || !qfamily_present_found) {
            gpu_log.error("failed to find required queue families");
            return;
        }

        queue_families.graphics = qfamily_graphics;
        queue_families.present = qfamily_present;
    }

    support.timestamp_queries = true;
    {
        // query limits and support
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(pdev, &device_properties);

        limits.timestamp_period = device_properties.limits.timestampPeriod;
        if (limits.timestamp_period == 0) {
            support.timestamp_queries = false;
            gpu_log.warn("timestamp queries not supported");
        }

        if (!device_properties.limits.timestampComputeAndGraphics) {
            // get properties for the graphics queue
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, nullptr);

            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, queueFamilies.data());

            auto props = queueFamilies[queue_families.graphics];
            if (!props.timestampValidBits) {
                support.timestamp_queries = false;
                gpu_log.warn("timestamp queries not supported on graphics queue");
            }
        }
    }

    {
        // create logical device

        VkDeviceQueueCreateInfo queueCreateInfos[] = {
            {},
            {}
        };
        // sometimes, graphics == present.
        std::set<u32> uniqueQueueFamilies = {qfamily_graphics, qfamily_present};

        float queuePriority = 1.0f;
        for (auto qfamily : uniqueQueueFamilies) {
            queueCreateInfos[qfamily].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[qfamily].queueFamilyIndex = qfamily;
            queueCreateInfos[qfamily].queueCount = 1;

            queueCreateInfos[qfamily].pQueuePriorities = &queuePriority;
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.queueCreateInfoCount = uniqueQueueFamilies.size();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = required_device_extensions.size();
        createInfo.ppEnabledExtensionNames = required_device_extensions.data();

        VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_feature {};
        descriptor_indexing_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        descriptor_indexing_feature.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        descriptor_indexing_feature.runtimeDescriptorArray = VK_TRUE;
        descriptor_indexing_feature.descriptorBindingVariableDescriptorCount = VK_TRUE;
        descriptor_indexing_feature.descriptorBindingPartiallyBound = VK_TRUE;

        VkPhysicalDeviceSynchronization2Features synchronization2_feature {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = &descriptor_indexing_feature,
            .synchronization2 = VK_TRUE,
        };

        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_feature {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .pNext = &synchronization2_feature,
            .dynamicRendering = VK_TRUE,
        };

        createInfo.pNext = &dynamic_rendering_feature;

        auto result = vkCreateDevice(pdev, &createInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            gpu_log.error("failed to create logical device");
            return;
        }
        gpu_log.info("logical device created");
    }

    {
        // get queues
        vkGetDeviceQueue(device, qfamily_graphics, 0, &graphics_queue);
        vkGetDeviceQueue(device, qfamily_present, 0, &present_queue);
    }

    // create swapchain initially
    u32 width, height;
    glfwGetFramebufferSize(window, (int*)&width, (int*)&height);

    swapchain = swapchain_builder_t(pdev, device, surface, queue_families.graphics, queue_families.present)
        .set_desired_format({.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
        .set_desired_extent(width, height)
        .add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        .build().unwrap();

    gpu_log.info("swapchain created");
    gpu_log.info("    format: {}", swapchain.image_format);
    gpu_log.info("    image count: {}", swapchain.image_count);

    // setup the command pools
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        // can also be transient.

        poolInfo.queueFamilyIndex = queue_families.graphics;
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &command_pool));
    }
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        // can also be transient.

        poolInfo.queueFamilyIndex = queue_families.graphics;
        VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &transient_command_pool));
    }

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    // setup the allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pdev;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = 0;
    allocatorInfo.pAllocationCallbacks = nullptr;
    allocatorInfo.pDeviceMemoryCallbacks = nullptr;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    vmaCreateAllocator(&allocatorInfo, &allocator);

    // setup the frames in flight
    for (auto i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto &frame = frames[i];
        {
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.image_available));
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.render_finished));
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.in_flight));
        }

        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = command_pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &frame.cmds));
        }
    }

    // setup depth buffer
    // @todo: recreate when resized!
    create_image(width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depth_image);

    auto cmd = begin_single_use_command_buffer();
    transition_image(cmd, depth_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    end_single_use_command_buffer(cmd);

    {
        // create the view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = depth_image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &depth_image.view));
    }
}

void gpu_t::frame(std::function<void(frame_t &)> fn) {
    auto &current_frame = frames[frame_number];

    const auto timeout = 1000000000;
    VK_CHECK(vkWaitForFences(device, 1, &current_frame.in_flight, VK_TRUE, timeout));
    VK_CHECK(vkResetFences(device, 1, &current_frame.in_flight));

    u32 image_idx;
    auto swapchain_result = vkAcquireNextImageKHR(device, swapchain.handle, timeout,
        current_frame.image_available, VK_NULL_HANDLE, &image_idx);
    {
        // @note: we can also do || swapchain_result == VK_SUBOPTIMAL_KHR here,
        // but I'm not sure it has a big impact. On my machine, this causes swapchain recreation
        // every time i move ANY window.
        if (swapchain_result == VK_ERROR_OUT_OF_DATE_KHR) {
            u32 width, height;
            glfwGetFramebufferSize(window, (int*)&width, (int*)&height);

            vkDeviceWaitIdle(device);

            swapchain = swapchain_builder_t(pdev, device, surface, queue_families.graphics, queue_families.present)
                .set_desired_format({.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                .set_desired_present_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)
                .set_desired_extent(width, height)
                .add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                .set_old_swapchain(swapchain)
                .build().unwrap();

            // signal the fence.
            vkQueueSubmit(graphics_queue, 0, nullptr, current_frame.in_flight);
            return;
        } if (swapchain_result == VK_SUBOPTIMAL_KHR) {
            // we can do something here, but lets ignore it.
        } else VK_CHECK(swapchain_result);
    }

    auto &cmds = current_frame.cmds;
    VK_CHECK(vkResetCommandBuffer(cmds, 0));
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = 0; // Optional
        beginInfo.pInheritanceInfo = nullptr; // Optional

        VK_CHECK(vkBeginCommandBuffer(cmds, &beginInfo));
    }

    // @todo: please no, we should maybe not draw directly to the swapchain. I think it would
    // be cooler to draw to an image and then copy it to the swapchain. But what do i know?
    transition_image(cmds, swapchain.images[image_idx], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    current_frame.image_idx = image_idx;
    fn(current_frame);

    transition_image(cmds, swapchain.images[image_idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmds));

    // submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &current_frame.image_available;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmds;

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &current_frame.render_finished;

    VK_CHECK(vkQueueSubmit(graphics_queue, 1, &submitInfo, current_frame.in_flight));

    // present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &current_frame.render_finished;

    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain.handle;
    presentInfo.pImageIndices = &image_idx;
    presentInfo.pResults = nullptr;

    vkQueuePresentKHR(present_queue, &presentInfo);

    frame_number = (frame_number + 1) % MAX_FRAMES_IN_FLIGHT;
}

ref_t<gpu_pipeline_t> gpu_t::make_pipeline(const pipeline_config_t &config) {
    // making the pipeline does not neccessarily create it. It will be created
    // when
    auto exists_it = loaded_pipelines.find(config);
    if (exists_it != loaded_pipelines.end()) {
        return exists_it->second;
    }

    auto pipeline = make_ref<gpu_pipeline_t>();
    pipeline->pipeline = VK_NULL_HANDLE;

    {
        // create the pipeline layout.
        //
        // @todo: good api for pipeline creation where we can specify pushConstants
        // and descriptor sets.
        auto it = pipeline_layouts.find(config.layout);
        if (it != pipeline_layouts.end()) {
            pipeline->layout = it->second;
        } else {

            VkPipelineLayoutCreateInfo pipeline_layout_info{};
            pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_info.flags = config.layout.flags;
            pipeline_layout_info.setLayoutCount = config.layout.descriptor_set_layouts.len;
            pipeline_layout_info.pSetLayouts = config.layout.descriptor_set_layouts.data;
            pipeline_layout_info.pushConstantRangeCount = config.layout.push_constant_ranges.len;
            pipeline_layout_info.pPushConstantRanges = config.layout.push_constant_ranges.data;

            VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, &pipeline->layout));
            pipeline_layouts[config.layout] = pipeline->layout;
        }
    }

    // @note: the config can contain temporary pointers (like slices to descriptors).
    // these need to be copied into the pipeline struct.
    //
    // Usually these slices are declared inline, which really messes with the lifetime.
    // I am not actually sure how long they live, but I think the pointers are invalid
    // outside the function scope. Ideally, these things should be silently be declared
    // globally. But that is not possible in C++.
    pipeline->vertex_input_info.bindings = std::vector<VkVertexInputBindingDescription>(config.vertex_input_info.bindings.len);
    pipeline->vertex_input_info.attributes = std::vector<VkVertexInputAttributeDescription>(config.vertex_input_info.attributes.len);
    memcpy(pipeline->vertex_input_info.bindings.data(), config.vertex_input_info.bindings.data, config.vertex_input_info.bindings.len * sizeof(VkVertexInputBindingDescription));
    memcpy(pipeline->vertex_input_info.attributes.data(), config.vertex_input_info.attributes.data, config.vertex_input_info.attributes.len * sizeof(VkVertexInputAttributeDescription));

    pipeline->color_attachment_formats = std::vector<VkFormat>(config.color_attachment_formats.len);
    memcpy(pipeline->color_attachment_formats.data(), config.color_attachment_formats.data, config.color_attachment_formats.len * sizeof(VkFormat));

    gpu_log.info("pipeline :{:p} created", pipeline);
    loaded_pipelines[config] = pipeline;

    return pipeline;
}

void gpu_t::rebuild_pipelines() {
    for (auto &[config, pipeline] : loaded_pipelines) {
        auto has_shader_changed = config.shader->modified;
        if (!has_shader_changed) continue;

        gpu_log.info("rebuilding pipeline :{:p}", pipeline);

        if (pipeline->pipeline != VK_NULL_HANDLE) {
            // @todo: cleanup the old pipeline.
            pipeline->pipeline = VK_NULL_HANDLE;
        }

        {
            // create the pipeline itself.
            VkPipelineRenderingCreateInfoKHR rendering_info{};
            rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            rendering_info.colorAttachmentCount = pipeline->color_attachment_formats.size();
            rendering_info.pColorAttachmentFormats = pipeline->color_attachment_formats.data();
            rendering_info.depthAttachmentFormat = config.depth_attachment_format;
            rendering_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            // @todo: make this configurable:
            // it has to be here for now as we need to shuffle the pointers around.
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

            VkPipelineViewportStateCreateInfo viewport_state{};
            {
                viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewport_state.viewportCount = 1;
                viewport_state.scissorCount = 1;
            }

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            {
                rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
                rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizer.lineWidth = 1.0f;
                rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
                rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizer.depthBiasEnable = VK_FALSE;
                rasterizer.depthBiasConstantFactor = 0.0f; // Optional
                rasterizer.depthBiasClamp = 0.0f; // Optional
                rasterizer.depthBiasSlopeFactor = 0.0f; // Optional
            }

            VkPipelineColorBlendStateCreateInfo color_blending{};
            color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blending.logicOpEnable = VK_FALSE;
            color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
            color_blending.attachmentCount = 1;
            color_blending.pAttachments = &colorBlendAttachment;
            color_blending.blendConstants[0] = 0.0f; // Optional
            color_blending.blendConstants[1] = 0.0f; // Optional
            color_blending.blendConstants[2] = 0.0f; // Optional
            color_blending.blendConstants[3] = 0.0f; // Optional

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

            pipeline_info.stageCount = config.shader->stages.size();
            pipeline_info.pStages = config.shader->stages.data();

            pipeline_info.pNext = &rendering_info;

            VkPipelineVertexInputStateCreateInfo vertex_input_info{};
            {
                vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertex_input_info.vertexBindingDescriptionCount = pipeline->vertex_input_info.bindings.size();
                vertex_input_info.pVertexBindingDescriptions = pipeline->vertex_input_info.bindings.data();
                vertex_input_info.vertexAttributeDescriptionCount = pipeline->vertex_input_info.attributes.size();
                vertex_input_info.pVertexAttributeDescriptions = pipeline->vertex_input_info.attributes.data();

                pipeline_info.pVertexInputState = &vertex_input_info;
            }

            VkPipelineDynamicStateCreateInfo dynamic_state{};
            VkDynamicState dynamic_states[] = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            {
                dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic_state.dynamicStateCount = 2;
                dynamic_state.pDynamicStates = dynamic_states;
            }

            VkPipelineDepthStencilStateCreateInfo depth_stencil_state{};
            {
                depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depth_stencil_state.depthTestEnable = config.depth_stencil.depth_test;
                depth_stencil_state.depthWriteEnable = config.depth_stencil.depth_write;
                depth_stencil_state.depthCompareOp = config.depth_stencil.depth_compare_op;
                depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
                depth_stencil_state.minDepthBounds = 0.0f;
                depth_stencil_state.maxDepthBounds = 1.0f;
            }

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            input_assembly.primitiveRestartEnable = VK_FALSE;

            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &config.multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil_state;
            pipeline_info.pColorBlendState = &color_blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = pipeline->layout;
            pipeline_info.subpass = 0;
            pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
                gpu_log.error("failed to create graphics pipeline");
            }

        }
    }
}

void gpu_t::create_buffer_persistent(slice<u8> data, VkBufferUsageFlags usage, gpu_buffer_t &buffer) {

    VkBuffer staging_buffer;
    VmaAllocation staging_allocation;
    VmaAllocationInfo staging_allocation_info;

    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = data.len;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &staging_buffer, &staging_allocation, &staging_allocation_info);
    }

    {
        // do copy
        vmaCopyMemoryToAllocation(allocator, data.data, staging_allocation, 0, data.len);
    }

    {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = data.len;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr);
    }

    {
        // perform move from staging to real
        VkCommandBuffer cmd = begin_single_use_command_buffer();

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = data.len;

        vkCmdCopyBuffer(cmd, staging_buffer, buffer.handle, 1, &copyRegion);

        end_single_use_command_buffer(cmd);
    }
}

void gpu_t::create_buffer(usize size, VkBufferUsageFlags usage, gpu_buffer_t &buffer) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr);
}

void gpu_t::create_image(usize width, usize height, VkFormat format, VkImageUsageFlags usage, gpu_image_t &image) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // this should be configurable.
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VK_CHECK(vmaCreateImage(allocator, &imageInfo, &alloc_info, &image.image, &image.allocation, nullptr));
}

void gpu_t::create_image(slice<u8> data, usize width, usize height, VkFormat format, VkImageUsageFlags usage, bool mipmap, gpu_image_t &image) {
    // we need to create a staging buffer for the image data.
    gpu_buffer_t staging_buffer;
    create_buffer(data.len, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buffer);
    write_buffer(staging_buffer, data);

    create_image(width, height, format, VK_IMAGE_USAGE_TRANSFER_DST_BIT | usage, image);

    auto cmd = begin_single_use_command_buffer();
    {
        transition_image(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy_region = {};
		copy_region.bufferOffset = 0;
		copy_region.bufferRowLength = 0;
		copy_region.bufferImageHeight = 0;

		copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.imageSubresource.mipLevel = 0;
		copy_region.imageSubresource.baseArrayLayer = 0;
		copy_region.imageSubresource.layerCount = 1;
		copy_region.imageExtent = { .width = (u32)width, .height = (u32)height, .depth = 1 };

		vkCmdCopyBufferToImage(cmd, staging_buffer.handle, image.image,
		  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

		// @todo: this is kinda hard-coded (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		transition_image(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    end_single_use_command_buffer(cmd);
}

void gpu_t::write_buffer(gpu_buffer_t &buffer, slice<u8> data) {
    // @todo: handle if the buffer is not host visible.
    void *address;
    vmaMapMemory(allocator, buffer.allocation, &address);
    memcpy(address, data.data, data.len);
    vmaUnmapMemory(allocator, buffer.allocation);
}

VkCommandBuffer gpu_t::begin_single_use_command_buffer() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = transient_command_pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void gpu_t::end_single_use_command_buffer(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);

    vkFreeCommandBuffers(device, transient_command_pool, 1, &cmd);
}

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier2 imageBarrier {};
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;

    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    imageBarrier.oldLayout = old_layout;
    imageBarrier.newLayout = new_layout;

    // big hack! :^)
    VkImageAspectFlags aspect_mask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageSubresourceRange sub_image {};
    sub_image.aspectMask = aspect_mask;
    sub_image.baseMipLevel = 0;
    sub_image.levelCount = VK_REMAINING_MIP_LEVELS;
    sub_image.baseArrayLayer = 0;
    sub_image.layerCount = VK_REMAINING_ARRAY_LAYERS;

    imageBarrier.subresourceRange = sub_image;
    imageBarrier.image = image;

    VkDependencyInfo dep_info {};
    dep_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep_info.pNext = nullptr;

    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(cmd, &dep_info);
}

void dump_available_validation_layers() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    gpu_log.info("available validation layers ({}):", layer_count);
    for (const auto& layer_properties : available_layers) {
        gpu_log.info("\t{}", layer_properties.layerName);
    }
}

void dump_available_physical_devices(VkInstance instance) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    gpu_log.info("available physical devices ({}):", device_count);
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        gpu_log.info("\t{}", device_properties.deviceName);
    }
}

void dump_available_physical_extensions(VkPhysicalDevice device) {
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    gpu_log.info("available extensions ({}):", extension_count);
    for (const auto& extension : available_extensions) {
        gpu_log.info("\t{}", extension.extensionName);
    }
}

void dump_available_instance_extensions() {
    u32 extension_count;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

    gpu_log.info("available instance extensions ({}):", extension_count);
    for (const auto& extension : extensions) {
        gpu_log.info("\t{}", extension.extensionName);
    }
}

bool is_device_suitable(VkPhysicalDevice device, slice<const char *> required_extensions) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);

    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    bool supports_geometry_shader = device_features.geometryShader;

    bool supports_extensions = true;
    {
        uint32_t extension_count;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

        for (auto extension : required_extensions) {
            bool supported = false;

            for (const auto& ext : available_extensions) {
                if (strcmp(extension, ext.extensionName) == 0) {
                    supported = true;
                    break;
                }
            }

            if (!supported) {
                supports_extensions = false;
                // gpu_log.error("device {} does not support required extension {}", device_name, extension);
                // dump_available_extensions(device);
                break;
            }
        }
    }

    return supports_geometry_shader && supports_extensions;
}

// returns true if all N validation layers are supported, else false.
template <usize N>
bool check_validation_layer_support(const char * (&validation_layers)[N]) {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (usize i = 0; i < N; i++) {
        bool layer_found = false;
        const char *layer_name = validation_layers[i];

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layer_found = true;
                break;
            }
        }

        if (!layer_found) {
            gpu_log.error("validation layer {} not found", layer_name);
            return false;
        }
    }

    return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    (void)messageType;
    (void)pUserData;

    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        gpu_log.warn("validation warning: {}", pCallbackData->pMessage);

        panic("aborting on validation warning!");
    }
    else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        gpu_log.error("validation error: {}", pCallbackData->pMessage);
        panic("aborting on validation error!");
    }
    else {
        gpu_log.info("validation: {}", pCallbackData->pMessage);
    }

    return VK_FALSE;
}

const char * vk_result_to_cstr(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
#ifdef VK_ENABLE_BETA_EXTENSIONS
    case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR: return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
#endif
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
    case VK_ERROR_NOT_PERMITTED_KHR: return "VK_ERROR_NOT_PERMITTED_KHR";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
    case VK_ERROR_COMPRESSION_EXHAUSTED_EXT: return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
    case VK_RESULT_MAX_ENUM: return "VK_RESULT_MAX_ENUM";
    default: return "??????";
    }
}

void descriptor_writer_t::clear() {
    image_infos.clear();
    writes.clear();
}

void descriptor_writer_t::update_set(gpu_t &gpu, VkDescriptorSet set) {
    for (auto &write : writes) {
        write.dstSet = set;
    }

    vkUpdateDescriptorSets(gpu.device, writes.size(), writes.data(), 0, nullptr);
}


void descriptor_writer_t::write_combined_image_sampler(u32 binding, u32 array_index, VkImageView view, VkSampler sampler) {
    VkDescriptorImageInfo &image_info = image_infos.emplace_back(VkDescriptorImageInfo{
        .sampler = sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_info;

    writes.push_back(write);
}

void descriptor_writer_t::write_buffer(u32 binding, u32 array_index, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    VkDescriptorBufferInfo &buffer_info = buffer_infos.emplace_back(VkDescriptorBufferInfo{
        .buffer = buffer,
        .offset = offset,
        .range = range
    });

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = VK_NULL_HANDLE;
    write.dstBinding = binding;
    write.dstArrayElement = array_index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    writes.push_back(write);
}
