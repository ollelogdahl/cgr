#include "gpu.h"

#include "log.h"

#include <vector>
#include <set>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

struct swap_chain_support_details_t {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

void dump_available_validation_layers();
void dump_available_physical_devices(VkInstance instance);
void dump_available_extensions(VkPhysicalDevice device);
bool is_device_suitable(VkPhysicalDevice device, slice<const char *> required_extensions);

template <usize N>
bool check_validation_layer_support(const char * (&validation_layers)[N]);

swap_chain_support_details_t query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

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
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
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
        appInfo.apiVersion = VK_API_VERSION_1_0;

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
            gpu_log.error("failed to create instance");
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
        {
            VkPhysicalDeviceProperties device_properties;
            vkGetPhysicalDeviceProperties(pdev, &device_properties);
            device_name = device_properties.deviceName;
        }
        gpu_log.info("selected physical device: {}", device_name);
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

        /*
        this is deprecated:
        if (validation_layers_available) {
            createInfo.enabledLayerCount = array_size(validation_layers);
            createInfo.ppEnabledLayerNames = validation_layers;
        }
        */

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

    recreate_swapchain(false);
}

void gpu_t::recreate_swapchain(bool need_to_clear) {
    if (need_to_clear) {
        for (size_t i = 0; i < swapchain.framebuffers.size(); i++) {
            vkDestroyFramebuffer(device, swapchain.framebuffers[i], nullptr);
        }

        vkDestroyRenderPass(device, display_render_pass, nullptr);

        for (size_t i = 0; i < swapchain.image_views.size(); i++) {
            vkDestroyImageView(device, swapchain.image_views[i], nullptr);
        }

        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
    }

    // setup swap chain
    swap_chain_support_details_t swap_chain_support = query_swap_chain_support(pdev, surface);

    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;
    {
        for (auto &surf : swap_chain_support.formats) {
            if (surf.format == VK_FORMAT_B8G8R8A8_SRGB && surf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surface_format = surf;
                break;
            }
        }
    }

    {
        for (auto &mode : swap_chain_support.present_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = mode;
                break;
            }
        }

        present_mode = VK_PRESENT_MODE_FIFO_KHR;
    }

    {
        if (swap_chain_support.capabilities.currentExtent.width != UINT32_MAX) {
            extent = swap_chain_support.capabilities.currentExtent;
        } else {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            extent.width = clamp((u32)width, swap_chain_support.capabilities.minImageExtent.width, swap_chain_support.capabilities.maxImageExtent.width);
            extent.height = clamp((u32)height, swap_chain_support.capabilities.minImageExtent.height, swap_chain_support.capabilities.maxImageExtent.height);
        }
    }

    u32 image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }


    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = image_count;
    createInfo.imageFormat = surface_format.format;
    createInfo.imageColorSpace = surface_format.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;

    // could also be VK_IMAGE_USAGE_TRANSFER_DST_BIT
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    u32 qfamilies_separate[] = {queue_families.graphics, queue_families.present};
    if (queue_families.graphics != queue_families.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = qfamilies_separate;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    createInfo.preTransform = swap_chain_support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    createInfo.presentMode = present_mode;
    createInfo.clipped = VK_TRUE;

    createInfo.oldSwapchain = VK_NULL_HANDLE;

    auto result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain.handle);
    if (result != VK_SUCCESS) {
        gpu_log.error("failed to create swap chain");
        return;
    }
    // gpu_log.info("swap chain created");

    vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, nullptr);
    swapchain.images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, swapchain.images.data());

    swapchain.image_format = surface_format.format;
    swapchain.extent = extent;

    {
        // re-create the render pass
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchain.image_format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &display_render_pass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    // create image views into swap-chain.
    swapchain.image_views.resize(swapchain.images.size());
    for (usize i = 0; i < swapchain.images.size(); i++) {
        auto &img = swapchain.images[i];
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = img;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchain.image_format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        auto result = vkCreateImageView(device, &createInfo, nullptr, &swapchain.image_views[i]);
        if (result != VK_SUCCESS) {
            gpu_log.error("failed to create image views");
            return;
        }
    }

    // setup framebuffers
    // @todo: i don't really love this. i think instead we should
    // set usage as TRANSFER_DST and use a single framebuffer for
    // each frame in flight (1 currently).
    {
        swapchain.framebuffers.reserve(swapchain.image_views.size());
        for (usize i = 0; i < swapchain.image_views.size(); ++i) {
            VkImageView attachments[] = {
                swapchain.image_views[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = display_render_pass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapchain.extent.width;
            framebufferInfo.height = swapchain.extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchain.framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
}

void gpu_t::create_buffer(VkBuffer &buffer, VkDeviceMemory &buffer_memory, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
        panic("failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(pdev, &memProperties);

    auto get_type_index = [&](u32 type_filter, VkMemoryPropertyFlags properties) -> u32 {
        for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        panic("failed to find suitable memory type");
    };

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = get_type_index(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &buffer_memory) != VK_SUCCESS) {
        panic("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, buffer_memory, 0);
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

void dump_available_extensions(VkPhysicalDevice device) {
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    gpu_log.info("available extensions ({}):", extension_count);
    for (const auto& extension : available_extensions) {
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

swap_chain_support_details_t query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    swap_chain_support_details_t details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);

    if (format_count != 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);

    if (present_mode_count != 0) {
        details.present_modes.resize(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }

    return details;
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
