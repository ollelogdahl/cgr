#include "vulkan_glfw_render_context.h"
#include "log.h"
#include "oc.h"
#include <set>
#include <span>
#include <algorithm>

#include "vku.h"

static void dump_available_validation_layers();
static void dump_available_physical_devices(VkInstance instance);
static void dump_available_physical_extensions(VkPhysicalDevice device);
static void dump_available_instance_extensions();

// @todo: there are more things interesting to check here.
static VkPhysicalDevice pick_physical_device(VkInstance instance, std::span<const char *> required_extensions);

static std::vector<VkExtensionProperties> get_available_instance_extensions();
static std::vector<VkExtensionProperties> get_available_physical_extensions(VkPhysicalDevice device);

static bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface, u32 &graphics, u32 &present, u32 &compute);

static bool check_validation_layer_support(std::span<const char *> validation_layers);

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

static logger_t logger = logger_t("vulkan-glfw-render-context");

VulkanGlfwRenderContext::VulkanGlfwRenderContext(GLFWwindow *window, const CreateOptions &options)
: m_window(window) {
    const char *validation_layers[] = {
        "VK_LAYER_KHRONOS_validation"
    };

    std::vector<const char *> required_device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    };

    std::vector<const char *> optional_device_extensions = {
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME, // used by vma for memory stats.
    };

    bool validation_layers_available = false;
    if (options.request_validation_layers) {
        if (check_validation_layer_support(validation_layers)) {
            validation_layers_available = true;
        } else  {
            logger.error("validation layers requested, but not available");
            dump_available_validation_layers();
            logger.info("proceeding without validation layers");
        }
    } else {
        logger.info("validation layers not requested");
    }

    if (validation_layers_available) {
        logger.info("validation layers enabled");
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

            createInfo.enabledExtensionCount = extensions.size();
            createInfo.ppEnabledExtensionNames = extensions.data();
        }

        VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
        if (result != VK_SUCCESS) {
            logger.error("failed to create instance: {}", result);
            dump_available_instance_extensions();

            return;
        }
        logger.info("vulkan instance created");
    }

    if (validation_layers_available) {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = vk_debug_callback;
        createInfo.pUserData = nullptr; // Optional

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");

        if(func(m_instance, &createInfo, nullptr, &m_debug_messenger)) {
            g_log.error("failed to setup debug messenger");
        }
    }

    // select a physical device
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
            logger.error("failed to find GPUs with Vulkan support");
            return;
        }

        m_pdev = pick_physical_device(m_instance, required_device_extensions);
        if (m_pdev == VK_NULL_HANDLE) {
            logger.error("failed to find a suitable GPU");
            dump_available_physical_devices(m_instance);
            return;
        }

        {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(m_pdev, &device_properties);
            auto device_name = device_properties.deviceName;
            auto api_version = fmt::format("{}.{}.{}",
                VK_VERSION_MAJOR(device_properties.apiVersion),
                VK_VERSION_MINOR(device_properties.apiVersion),
                VK_VERSION_PATCH(device_properties.apiVersion));

            logger.info("selected physical device: {}", device_name);
            logger.info("vukan version: {}", api_version);
        }
    }

    {
        // create surface
        if(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) != VK_SUCCESS) {
            logger.error("failed to create window surface");
            return;
        }
        logger.info("window surface created");
    }

    bool all_found = find_queue_families(m_pdev, m_surface, m_queue_families.graphics, m_queue_families.present, m_queue_families.compute);
    if (!all_found) {
        logger.error("failed to find all queue families");
        return;
    }

    // query support and stuff.
    m_device_props.timestamp_queries = true;
    {
        // query limits and support
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(m_pdev, &device_properties);

        m_device_props.timestamp_period = device_properties.limits.timestampPeriod;
        if (m_device_props.timestamp_period == 0) {
            m_device_props.timestamp_queries = false;
            logger.warn("timestamp queries not supported");
        }

        if (!device_properties.limits.timestampComputeAndGraphics) {
            // get properties for the graphics queue
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &queueFamilyCount, nullptr);

            std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &queueFamilyCount, queueFamilies.data());

            auto props = queueFamilies[m_queue_families.graphics];
            if (!props.timestampValidBits) {
                m_device_props.timestamp_queries = false;
                logger.warn("timestamp queries not supported on graphics queue");
            }
        }

        VkPhysicalDeviceSubgroupProperties subgroup_properties;
        subgroup_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

        // query available features
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &subgroup_properties;
        vkGetPhysicalDeviceFeatures2(m_pdev, &features2);

        m_device_props.pipeline_statistics = features2.features.pipelineStatisticsQuery;
        m_device_props.subgroup_size = subgroup_properties.subgroupSize;
    }

    // inform about support and limits
    {
        logger.info("device info");
        logger.info("\tpipeline statistics: {}", m_device_props.pipeline_statistics);
        logger.info("\tsubgroup size: {}", m_device_props.subgroup_size);
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
            m_queue_families.graphics,
            m_queue_families.present,
            m_queue_families.compute
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

        // get all available extensions
        auto available_extensions = get_available_physical_extensions(m_pdev);
        for (const auto &ext : optional_device_extensions) {
            if (std::find_if(available_extensions.begin(), available_extensions.end(), [&](const auto &e) {
                return strcmp(e.extensionName, ext) == 0;
            }) == available_extensions.end()) {
                logger.warn("optional extension {} not available", ext);
            } else {
                enabled_extensions.push_back(ext);
            }
        }

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.queueCreateInfoCount = uniqueQueueFamilies.size();
        createInfo.enabledExtensionCount = enabled_extensions.size();
        createInfo.ppEnabledExtensionNames = enabled_extensions.data();

        VkPhysicalDeviceFeatures enabled_features{};

        if (m_device_props.pipeline_statistics) {
            logger.info("pipeline statistics supported");
            enabled_features.pipelineStatisticsQuery = VK_TRUE;
        }

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

        auto result = vkCreateDevice(m_pdev, &createInfo, nullptr, &m_device);
        if (result != VK_SUCCESS) {
            logger.error("failed to create logical device: {}", result);
            return;
        }
        logger.info("logical device created");
    }

    {
        // get queues
        vkGetDeviceQueue(m_device, m_queue_families.graphics, 0, &m_graphics_queue);
        vkGetDeviceQueue(m_device, m_queue_families.present, 0, &m_present_queue);
        vkGetDeviceQueue(m_device, m_queue_families.compute, 0, &m_compute_queue);
    }

    // create swapchain initially
    u32 width, height;
    glfwGetFramebufferSize(window, (int*)&width, (int*)&height);

    m_swapchain = swapchain_builder_t(m_pdev, m_device, m_surface, m_queue_families.graphics, m_queue_families.present)
        .set_desired_format({.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_modes({VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_FIFO_KHR})
        .set_desired_extent(width, height)
        .add_image_usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
        .build().unwrap();

    logger.info("swapchain created");
    logger.info("    present_mode: {}", m_swapchain.present_mode);
    logger.info("    format:       {}", m_swapchain.image_format);
    logger.info("    image count:  {}", m_swapchain.image_count);

    // setup frame stuffies.
    for (u32 i = 0; i < options.frames_in_flight; ++i) {
        Frame frame;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.image_available));
        VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.render_finished));
        VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &frame.in_flight));
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

    logger.info("available validation layers ({}):", layer_count);
    for (const auto& layer_properties : available_layers) {
        logger.info("\t{}", layer_properties.layerName);
    }
}

void dump_available_physical_devices(VkInstance instance) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    logger.info("available physical devices ({}):", device_count);
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        logger.info("\t{}", device_properties.deviceName);
    }
}

std::vector<VkExtensionProperties> get_available_instance_extensions() {
    u32 extension_count;
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

    return extensions;
}

std::vector<VkExtensionProperties> get_available_physical_extensions(VkPhysicalDevice device) {
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    return available_extensions;
}

void dump_available_physical_extensions(VkPhysicalDevice device) {
    auto extensions = get_available_physical_extensions(device);
    logger.info("available extensions ({}):", extensions.size());
    for (const auto& extension : extensions) {
        logger.info("\t{}", extension.extensionName);
    }
}

void dump_available_instance_extensions() {
    auto extensions = get_available_instance_extensions();
    logger.info("available instance extensions ({}):", extensions.size());
    for (const auto& extension : extensions) {
        logger.info("\t{}", extension.extensionName);
    }
}

bool is_device_suitable(VkPhysicalDevice device, std::span<const char *> required_extensions) {
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

VkPhysicalDevice pick_physical_device(VkInstance instance, std::span<const char *> required_extensions) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    for (const auto& device : devices) {
        if (is_device_suitable(device, required_extensions)) {
            return device;
        }
    }

    return VK_NULL_HANDLE;
}

// returns true if all N validation layers are supported, else false.
bool check_validation_layer_support(std::span<const char *> validation_layers) {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (usize i = 0; i < validation_layers.size(); i++) {
        bool layer_found = false;
        const char *layer_name = validation_layers[i];

        for (const auto& layer_properties : available_layers) {
            if (strcmp(layer_name, layer_properties.layerName) == 0) {
                layer_found = true;
                break;
            }
        }

        if (!layer_found) {
            logger.error("validation layer {} not found", layer_name);
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
        logger.warn("validation warning: {}", pCallbackData->pMessage);

        panic("aborting on validation warning!");
    }
    else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        logger.error("validation error: {}", pCallbackData->pMessage);
        panic("aborting on validation error!");
    }
    else {
        logger.info("validation: {}", pCallbackData->pMessage);
    }

    return VK_FALSE;
}
