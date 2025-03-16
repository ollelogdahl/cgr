#include "gpu.h"
#include "gpu_impl.h"

#include "vks.h"

#include <set>
#include <vulkan/vulkan_core.h>

#include <tracy/Tracy.hpp>

std::vector<VkPresentModeKHR> present_modes_in_order_of_preference = {
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_IMMEDIATE_KHR,
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
};

void dump_available_validation_layers();
void dump_available_physical_devices(VkInstance instance);
void dump_available_physical_extensions(VkPhysicalDevice device);
void dump_available_instance_extensions();
bool is_device_suitable(VkPhysicalDevice device, slice<const char *> required_extensions);

bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface, u32 &graphics, u32 &present, u32 &compute);

template <usize N>
bool check_validation_layer_support(const char * (&validation_layers)[N]);

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

static const char *VK_LAYER_KHRONOS_validation = "VK_LAYER_KHRONOS_validation";

void gpu_t::init(GLFWwindow *window, const gpu_create_options_t &options) {
    this->window = window;


    const char *validation_layers[] = {
        VK_LAYER_KHRONOS_validation
    };


    std::vector<const char *> required_device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        // VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, @note: core in 1.3
        // VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, @note: core in 1.3
    };

    // @todo: we should probably do better.
    std::vector<const char *> optional_device_extensions = {
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, // used by vma for memory stats.
    };

    bool validation_layers_available = false;
    if (options.request_validation_layers) {
        if (check_validation_layer_support(validation_layers)) {
            validation_layers_available = true;
        } else  {
            gpu_log.error("validation layers requested, but not available");
            dump_available_validation_layers();
            gpu_log.info("proceeding without validation layers");
        }
    } else {
        gpu_log.info("validation layers not requested");
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

            // optional
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

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

        {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(pdev, &device_properties);
            auto device_name = device_properties.deviceName;
            auto api_version = fmt::format("{}.{}.{}",
                VK_VERSION_MAJOR(device_properties.apiVersion),
                VK_VERSION_MINOR(device_properties.apiVersion),
                VK_VERSION_PATCH(device_properties.apiVersion));

            gpu_log.info("selected physical device: {}", device_name);
            gpu_log.info("vulkan version: {}", api_version);
        }
    }

    {
        // create surface
        if(glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            gpu_log.error("failed to create window surface");
            return;
        }
        gpu_log.info("window surface created");
    }

    bool all_found = find_queue_families(pdev, surface, queue_families.graphics, queue_families.present, queue_families.compute);
    if (!all_found) {
        gpu_log.error("failed to find all queue families");
        return;
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

        // query available features
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        vkGetPhysicalDeviceFeatures2(pdev, &features2);

        support.pipeline_statistics = features2.features.pipelineStatisticsQuery;
        support.depth_bias_clamp = features2.features.depthBiasClamp;
    }

    {
        // create logical device

        VkDeviceQueueCreateInfo queueCreateInfos[] = {
            {},
            {},
            {},
        };
        // sometimes, graphics == present.
        std::set<u32> uniqueQueueFamilies = {
            queue_families.graphics,
            queue_families.present,
            queue_families.compute
        };

        float queuePriority = 1.0f;
        for (auto qfamily : uniqueQueueFamilies) {
            queueCreateInfos[qfamily].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfos[qfamily].queueFamilyIndex = qfamily;
            queueCreateInfos[qfamily].queueCount = 1;

            queueCreateInfos[qfamily].pQueuePriorities = &queuePriority;
        }

        // @todo: try to use optional, else retry without.
        auto enabled_extensions = std::vector<const char *>(required_device_extensions);
        enabled_extensions.insert(enabled_extensions.end(), optional_device_extensions.begin(), optional_device_extensions.end());

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.queueCreateInfoCount = uniqueQueueFamilies.size();
        createInfo.enabledExtensionCount = enabled_extensions.size();
        createInfo.ppEnabledExtensionNames = enabled_extensions.data();

        VkPhysicalDeviceFeatures enabled_features{};

        if (support.pipeline_statistics) {
            gpu_log.info("pipeline statistics supported");
            enabled_features.pipelineStatisticsQuery = VK_TRUE;
        }

        if (support.depth_bias_clamp) {
            gpu_log.info("depth bias clamp supported");
            enabled_features.depthBiasClamp = VK_TRUE;
        }

        enabled_features.drawIndirectFirstInstance = VK_TRUE;

        createInfo.pEnabledFeatures = &enabled_features;

        VkPhysicalDeviceSynchronization2Features synchronization2_feature {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = nullptr,
            .synchronization2 = VK_TRUE,
        };

        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering_feature {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
            .pNext = &synchronization2_feature,
            .dynamicRendering = VK_TRUE,
        };

        VkPhysicalDeviceVulkan12Features vulkan12_feature{};
        vulkan12_feature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan12_feature.drawIndirectCount = VK_TRUE;
        vulkan12_feature.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vulkan12_feature.runtimeDescriptorArray = VK_TRUE;
        vulkan12_feature.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vulkan12_feature.descriptorBindingPartiallyBound = VK_TRUE;
        vulkan12_feature.pNext = &dynamic_rendering_feature;

        createInfo.pNext = &vulkan12_feature;

        auto result = vkCreateDevice(pdev, &createInfo, nullptr, &device);
        if (result != VK_SUCCESS) {
            gpu_log.error("failed to create logical device: {}", vk_result_to_cstr(result));
            return;
        }
        gpu_log.info("logical device created");
    }

    {
        // get queues
        vkGetDeviceQueue(device, queue_families.graphics, 0, &graphics_queue);
        vkGetDeviceQueue(device, queue_families.present, 0, &present_queue);
        vkGetDeviceQueue(device, queue_families.compute, 0, &compute_queue);
    }

    // create swapchain initially
    u32 width, height;
    glfwGetFramebufferSize(window, (int*)&width, (int*)&height);

    swapchain = swapchain_builder_t(pdev, device, surface, queue_families.graphics, queue_families.present)
        .set_desired_format({.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_modes(present_modes_in_order_of_preference)
        .set_desired_extent(width, height)
        .add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build().unwrap();

    gpu_log.info("swapchain created");
    gpu_log.info("    present_mode: {}", swapchain.present_mode);
    gpu_log.info("    format:       {}", swapchain.image_format);
    gpu_log.info("    image count:  {}", swapchain.image_count);

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
    {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_families.compute;
        VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr, &compute_command_pool));
    }

    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    static const char *tracy_mempool_name = "vma";

    auto cbs = new VmaDeviceMemoryCallbacks;
    cbs->pfnAllocate = [](VmaAllocator allocator, u32 memory_type, VkDeviceMemory memory, VkDeviceSize size, void *) {
        TracyAllocN(memory, size, tracy_mempool_name);
    };
    cbs->pfnFree = [](VmaAllocator allocator, u32 memory_type, VkDeviceMemory memory, VkDeviceSize size, void *) {
        TracyFreeN(memory, tracy_mempool_name);
    };

    // setup the allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = pdev;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    allocatorInfo.pAllocationCallbacks = nullptr;
    allocatorInfo.pDeviceMemoryCallbacks = cbs;
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
            VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.compute_finished));
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.in_flight));
            VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frame.compute_in_flight));
        }

        VkCommandBuffer cmd, compute_cmd;
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = command_pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &cmd));
        }
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = compute_command_pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;

            VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &compute_cmd));
        }

        auto make_perm_name = [](std::string &&name) {
            char *cstr = (char *)malloc(name.length() + 1);
            memcpy(cstr, name.c_str(), name.length());
            cstr[name.length()] = '\0';
            return cstr;
        };

        auto gfx_name = make_perm_name(fmt::format("graphics {}", i + 1));
        auto compute_name = make_perm_name(fmt::format("compute {}", i + 1));

        frame.cmd = CommandBuffer(*this, graphics_queue, command_pool, gfx_name);
        frame.compute_cmd = CommandBuffer(*this, compute_queue, compute_command_pool, compute_name);
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

bool find_queue_families(VkPhysicalDevice pdev, VkSurfaceKHR surface, u32 &graphics, u32 &present, u32 &compute) {
    bool graphics_found = false;
    bool present_found = false;
    bool compute_found = false;

    auto all_found = [&]() {
        return graphics_found && present_found && compute_found;
    };

    // find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(pdev, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (all_found()) {
            return true;
        }

        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_found = true;
            graphics = i;
        }

        // @todo: preferences. is it better to have a separate compute queue?
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            compute_found = true;
            compute = i;
        }

        if (graphics_found) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(pdev, i, surface, &presentSupport);
            if (presentSupport) {
                present_found = true;
                present = i;
            }
        }

        i++;
    }

    return all_found();
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

        // panic("aborting on validation warning!");
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
