#include <asm-generic/errno-base.h>
#include <cerrno>
#include <cstdint>
#include <stdexcept>

#include <sys/types.h>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "oc.h"
#include "log.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <set>
#include <vulkan/vulkan_core.h>

static logger_t gpu_log = logger_t("gpu");

#include <sys/inotify.h>
#include <fcntl.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>

struct fswatcher_t {
    int inotify_fd;

    struct elem_t {
        void (*on_modified)(std::string, void *userdata);
        void *userdata;
        std::string path;

        bool operator==(const elem_t &other) const {
            return path == other.path && on_modified == other.on_modified;
        }
    };

    std::unordered_map<int, elem_t> watches;

    void init() {
        inotify_fd = inotify_init();
        auto old_flags = fcntl(inotify_fd, F_GETFL);
        fcntl(inotify_fd, F_SETFL, old_flags | O_NONBLOCK);
    }

    void add_watch(const char *path, void (*on_modified)(std::string, void *userdata), void *userdata) {
        int ret = inotify_add_watch(inotify_fd, path, IN_ALL_EVENTS);
        if (ret == -1) {
            panic("inotify_add_watch error: {}", strerror(errno));
        }
        watches[ret] = {
            on_modified,
            userdata,
            path
        };
    }
    void process_watches() {
        // read from inotify until no more events are available now
        const usize event_max_size = sizeof(inotify_event) + 256;
        const usize buffer_size = 128 * event_max_size;
        char buffer[buffer_size];

        while (true) {
            auto len = read(inotify_fd, buffer, buffer_size);
            if (len < 0 && errno == EAGAIN) break;
            if (len == 0) break;

            ssize_t idx = 0;
            while(idx < len) {
                auto ev = (inotify_event *)(buffer + idx);
                idx += sizeof(inotify_event) + ev->len;

                if (ev->mask & IN_MODIFY) {
                    auto &e = watches[ev->wd];
                    e.on_modified(e.path, e.userdata);
                }
            }
        }
    }
};

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

struct swap_chain_support_details_t {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

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


struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    VkSurfaceKHR surface;

    GLFWwindow *window;

    VkRenderPass display_render_pass;

    VkDebugUtilsMessengerEXT debug_messager;

    struct {
        u32 graphics;
        u32 present;
    } queue_families;

    // can maybe be broken out but i don't see why.
    struct {
        VkSwapchainKHR handle;
        std::vector<VkImage> images;
        VkFormat image_format;
        VkExtent2D extent;

        std::vector<VkImageView> image_views;
        std::vector<VkFramebuffer> framebuffers;
    } swapchain;

    void init(GLFWwindow *window) {
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

            createInfo.enabledLayerCount = 0;

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

        auto queue_families_found = [&]() {
            return qfamily_graphics_found && qfamily_present_found;
        };

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
                if (queue_families_found()) break;

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

            if (!queue_families_found()) {
                gpu_log.error("failed to find required queue families");
                return;
            }

            queue_families.graphics = qfamily_graphics;
            queue_families.present = qfamily_present;
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

        {
            // create the render pass
            // @todo: actually, the image format may change, so we should recreate the render pass
            //       when the swapchain is recreated.
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
    }

    void recreate_swapchain(bool need_to_clear = true) {
        if (need_to_clear) {
            for (size_t i = 0; i < swapchain.framebuffers.size(); i++) {
                vkDestroyFramebuffer(device, swapchain.framebuffers[i], nullptr);
            }

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
};

#include <sys/stat.h>

bool file_a_is_newer_than_b(const char *a, const char *b) {
    struct stat a_stat;
    struct stat b_stat;

    int ret_a = stat(a, &a_stat);
    int ret_b = stat(b, &b_stat);

    if (ret_b != 0) {
        return true;
    }
    if (ret_a != 0) {
        return false;
    }

    return a_stat.st_mtime > b_stat.st_mtime;
}

struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool operator==(const shader_program_load_params_t &other) const {
        return strcmp(vertex_hlsl_path, other.vertex_hlsl_path) == 0 &&
               strcmp(fragment_hlsl_path, other.fragment_hlsl_path) == 0;
    }
};
struct shader_program_t {
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    shader_program_load_params_t params;
    bool modified = false;
};

// @todo: setup a pipeline cache which will allow us to
// 1. create only 1 pipeline for each configuration
// 2. Hot reload the pipeline when the shader changes
struct pipeline_config_t {
    ref_t<shader_program_t> shader;

    // @todo: make this cleaner and make optional
    VkPipelineVertexInputStateCreateInfo vertex_input_info;
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo rasterizer;
    VkPipelineMultisampleStateCreateInfo multisampling;
    VkPipelineColorBlendStateCreateInfo color_blending;
    VkPipelineDynamicStateCreateInfo dynamic_state;
    VkPipelineLayout pipeline_layout;
    VkRenderPass render_pass;

    bool operator ==(const pipeline_config_t &other) const {
        return shader == other.shader;
    }
};
struct gpu_pipeline_t {
    VkPipeline pipeline;
    pipeline_config_t config;
    bool modified = false;
};

template <>
struct std::hash<shader_program_load_params_t> {
    std::size_t operator()(const shader_program_load_params_t &params) const {
        std::size_t h1 = std::hash<const char*>{}(params.vertex_hlsl_path);
        std::size_t h2 = std::hash<const char*>{}(params.fragment_hlsl_path);
        return h1 ^ (h2 << 1);
    }
};

template <>
struct std::hash<pipeline_config_t> {
    std::size_t operator()(const pipeline_config_t &config) const {
        std::size_t h1 = std::hash<shader_program_load_params_t>{}(config.shader->params);
        return h1;
    }
};

#define SHADER_STAGE_VERTEX 0
#define SHADER_STAGE_FRAGMENT 1

VkPipelineShaderStageCreateInfo compile_shader(gpu_t &gpu, const char *path, int type) {
    // in dev mode, we compile the shader into the tmp dir. The filename in tmp is
    // based on the hash of the original file name.

    u64 hash = std::hash<const char *>{}(path);

    const char *tmp_dir = "/tmp";
    auto tmp_path = fmt::format("{}/{}.spv", tmp_dir, hash);

    VkShaderStageFlagBits vk_stage;
    const char *glslc_stage;
    switch (type) {
    case SHADER_STAGE_VERTEX:
        glslc_stage = "vertex";
        vk_stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case SHADER_STAGE_FRAGMENT:
        glslc_stage = "fragment";
        vk_stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    }

    if (file_a_is_newer_than_b(path, tmp_path.c_str())) {
        gpu_log.info("compiling shader {}", path);
        auto cmd = fmt::format("glslc -fshader-stage={} -o {} {}", glslc_stage, tmp_path, path);

        // @todo: use exec instead of system.
        // we want to be able to do these things in parallel.
        system(cmd.c_str());
    }

    auto code = file_read(tmp_path.c_str()).unwrap();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.contents.len;
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.contents.data);

    VkShaderModule shader_module;
    if (vkCreateShaderModule(gpu.device, &createInfo, nullptr, &shader_module) != VK_SUCCESS) {
        gpu_log.error("failed to create shader module");
    }

    file_close(code);

    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = vk_stage;
    stage_info.module = shader_module;
    stage_info.pName = "main";

    return stage_info;
}

// @todo: pipelines should be somewhere else (pipeline cache), but needs to be referenced from here.
struct loader_t {
    ref_t<shader_program_t> load_shader_program(const shader_program_load_params_t &params) {
        auto exists_it = loaded_shaders.find(params);
        if (exists_it != loaded_shaders.end()) {
            return exists_it->second;
        }


        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.push_back(compile_shader(*gpu, params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
        stages.push_back(compile_shader(*gpu, params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));

        loaded_shaders[params] = make_ref<shader_program_t>();
        shader_program_t &program = *loaded_shaders[params];
        program.stages = stages;
        program.params = params;
        program.modified = false;

        watcher.add_watch(params.vertex_hlsl_path, [](std::string, void *userdata) {
            auto *shader = static_cast<shader_program_t *>(userdata);
            shader->modified = true;
        }, &program);
        watcher.add_watch(params.fragment_hlsl_path, [](std::string, void *userdata) {
            auto *shader = static_cast<shader_program_t *>(userdata);
            shader->modified = true;
        }, &program);

        return loaded_shaders[params];
    }

    ref_t<gpu_pipeline_t> make_pipeline(const pipeline_config_t &config) {
        auto exists_it = loaded_pipelines.find(config);
        if (exists_it != loaded_pipelines.end()) {
            return exists_it->second;
        }

        auto pipeline = make_ref<gpu_pipeline_t>();
        pipeline->pipeline = VK_NULL_HANDLE;
        pipeline->config = config;
        pipeline->modified = true;

        loaded_pipelines[config] = pipeline;

        return pipeline;
    }

    void init(gpu_t &gpu) {
        this->gpu = &gpu;
        watcher.init();
    }

    void process_hotreload() {
        watcher.process_watches();

        for (auto it : loaded_pipelines) {
            auto &pipeline = it.second;
            if (pipeline->config.shader->modified) {
                pipeline->modified = true;
            }
        }

        for (auto it : loaded_shaders) {
            auto &program = it.second;
            if (program->modified) {
                program->stages.clear();

                program->stages.push_back(compile_shader(*gpu, program->params.vertex_hlsl_path, SHADER_STAGE_VERTEX));
                program->stages.push_back(compile_shader(*gpu, program->params.fragment_hlsl_path, SHADER_STAGE_FRAGMENT));

                program->modified = false;
            }
        }

        for (auto it : loaded_pipelines) {
            auto &pipeline = it.second;

            if (pipeline->modified) {
                pipeline->modified = false;
                g_log.info("recreating pipeline");
                if (pipeline->pipeline != VK_NULL_HANDLE) {
                    // @todo: when is it safe to destroy a pipeline?
                    // vkDestroyPipeline(gpu->device, pipeline->pipeline, nullptr);
                    pipeline->pipeline = VK_NULL_HANDLE;
                }

                VkGraphicsPipelineCreateInfo pipeline_info{};
                pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

                pipeline_info.stageCount = pipeline->config.shader->stages.size();
                pipeline_info.pStages = pipeline->config.shader->stages.data();

                pipeline_info.pVertexInputState = &pipeline->config.vertex_input_info;
                pipeline_info.pInputAssemblyState = &pipeline->config.input_assembly;
                pipeline_info.pViewportState = &pipeline->config.viewport_state;
                pipeline_info.pRasterizationState = &pipeline->config.rasterizer;
                pipeline_info.pMultisampleState = &pipeline->config.multisampling;
                pipeline_info.pDepthStencilState = nullptr;
                pipeline_info.pColorBlendState = &pipeline->config.color_blending;
                pipeline_info.pDynamicState = &pipeline->config.dynamic_state;
                pipeline_info.layout = pipeline->config.pipeline_layout;
                pipeline_info.renderPass = pipeline->config.render_pass;
                pipeline_info.subpass = 0;
                pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

                if (vkCreateGraphicsPipelines(gpu->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->pipeline) != VK_SUCCESS) {
                    gpu_log.error("failed to create graphics pipeline");
                }
            }
        }
    }

    std::unordered_map<shader_program_load_params_t, ref_t<shader_program_t>> loaded_shaders;
    std::unordered_map<pipeline_config_t, ref_t<gpu_pipeline_t>> loaded_pipelines;

    fswatcher_t watcher;
    gpu_t *gpu;
};

loader_t g_loader;

struct imgui_renderer_state_t {
    VkCommandBuffer cmds;
};

void imgui_init(gpu_t &gpu) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    VkDescriptorPool descriptor_pool;
    {
        VkDescriptorPoolSize imgui_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = array_size(imgui_pool_sizes);
        pool_info.pPoolSizes = imgui_pool_sizes;
        if (vkCreateDescriptorPool(gpu.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool");
        }
    }

    // create unique gpu stuffs for imgui...
    ImGui_ImplGlfw_InitForVulkan(gpu.window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = gpu.instance;
    init_info.PhysicalDevice = gpu.pdev;
    init_info.Device = gpu.device;
    init_info.QueueFamily = gpu.queue_families.graphics,
    init_info.Queue = gpu.graphics_queue,
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = descriptor_pool;
    init_info.RenderPass = gpu.display_render_pass;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = gpu.swapchain.images.size();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = VK_NULL_HANDLE;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);
}

int main(void) {
    oc_init();
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "vulkan", nullptr, nullptr);

    // initialize vulkan
    gpu_t gpu;
    gpu.init(window);
    g_log.info("gpu initialized");

    g_loader.init(gpu);

    imgui_init(gpu);
    g_log.info("imgui initialized");

    // create a test pipeline and pass
    ref_t<gpu_pipeline_t> pipeline;
    {
        // viewport and scissor states are provided each instantiation of the pipeline.
        // this is handy for resizes.
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(array_size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        // vertex input
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE; // useful to set as true for shadow mapping
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0; // Optional
        pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        VkPipelineLayout pipelineLayout;
        if (vkCreatePipelineLayout(gpu.device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            gpu_log.error("failed to create pipeline layout!");
            return 1;
        }

        auto shader = g_loader.load_shader_program({
           .vertex_hlsl_path = "eassets/shaders/test.vert",
           .fragment_hlsl_path = "eassets/shaders/test.frag",
        });

        pipeline = g_loader.make_pipeline({
            .shader = shader,
            .vertex_input_info = vertexInputInfo,
            .input_assembly = inputAssembly,
            .viewport_state = viewportState,
            .rasterizer = rasterizer,
            .multisampling = multisampling,
            .color_blending = colorBlending,
            .dynamic_state = dynamicState,
            .pipeline_layout = pipelineLayout,
            .render_pass = gpu.display_render_pass,
        });
    }

    // setup the command-buffers.
    // i do not think these must be owned by the gpu.

    VkCommandPool cmd_pool;
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        // can also be transient.

        poolInfo.queueFamilyIndex = gpu.queue_families.graphics;
        if (vkCreateCommandPool(gpu.device, &poolInfo, nullptr, &cmd_pool) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    VkCommandBuffer cmds;
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmd_pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(gpu.device, &allocInfo, &cmds) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    // create sync objects
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateSemaphore(gpu.device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create semaphore");
        }
        if (vkCreateSemaphore(gpu.device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create semaphore");
        }
        if (vkCreateFence(gpu.device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fence");
        }
    }

    g_log.info("running...");
    gpu_log.info("swapchain size: {}", gpu.swapchain.image_views.size());
    while(!glfwWindowShouldClose(window)) {
        g_loader.process_hotreload();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        static bool show_demo_window = true;
        ImGui::ShowDemoWindow(&show_demo_window);

        ImGui::Begin("test");

        ImGui::Text("Pipeline: %p", pipeline->pipeline);

        ImGui::End();

        // draw frame
        // @todo: support multiple frames in flight.
        {
            u32 image_idx;
            auto swapchain_result = vkAcquireNextImageKHR(gpu.device, gpu.swapchain.handle, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &image_idx);
            {
                if (swapchain_result == VK_ERROR_OUT_OF_DATE_KHR || swapchain_result == VK_SUBOPTIMAL_KHR) {
                    vkDeviceWaitIdle(gpu.device);
                    gpu.recreate_swapchain();
                    ImGui::EndFrame();
                    continue;
                } else if (swapchain_result != VK_SUCCESS) {
                    throw std::runtime_error("failed to acquire swap chain image!");
                }
            }

            vkWaitForFences(gpu.device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
            vkResetFences(gpu.device, 1, &inFlightFence);

            VkViewport full_viewport{};
            full_viewport.x = 0.0f;
            full_viewport.y = 0.0f;
            full_viewport.width = (float) gpu.swapchain.extent.width;
            full_viewport.height = (float) gpu.swapchain.extent.height;
            full_viewport.minDepth = 0.0f;
            full_viewport.maxDepth = 1.0f;

            VkRect2D full_scissor{};
            full_scissor.offset = {0, 0};
            full_scissor.extent = gpu.swapchain.extent;

            vkResetCommandBuffer(cmds, 0);
            // use the command buffer
            {
                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = 0; // Optional
                beginInfo.pInheritanceInfo = nullptr; // Optional

                if (vkBeginCommandBuffer(cmds, &beginInfo) != VK_SUCCESS) {
                    throw std::runtime_error("failed to begin recording command buffer!");
                }
            }

            // begin render pass
            {
                VkRenderPassBeginInfo renderPassInfo{};
                renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassInfo.renderPass = gpu.display_render_pass;
                renderPassInfo.framebuffer = gpu.swapchain.framebuffers[image_idx];
                renderPassInfo.renderArea.offset = {0, 0};
                renderPassInfo.renderArea.extent = gpu.swapchain.extent;

                VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearColor;

                vkCmdBeginRenderPass(cmds, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            }

            vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

            vkCmdSetViewport(cmds, 0, 1, &full_viewport);
            vkCmdSetScissor(cmds, 0, 1, &full_scissor);
            vkCmdDraw(cmds, 3, 1, 0, 0);

            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();
            ImGui_ImplVulkan_RenderDrawData(draw_data, cmds);

            vkCmdEndRenderPass(cmds);

            if (vkEndCommandBuffer(cmds) != VK_SUCCESS) {
                throw std::runtime_error("failed to record command buffer!");
            }

            // submit command buffer
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &imageAvailableSemaphore;
            submitInfo.pWaitDstStageMask = waitStages;

            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmds;

            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &renderFinishedSemaphore;

            if (vkQueueSubmit(gpu.graphics_queue, 1, &submitInfo, inFlightFence) != VK_SUCCESS) {
                throw std::runtime_error("failed to submit draw command buffer!");
            }

            // present
            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &gpu.swapchain.handle;
            presentInfo.pImageIndices = &image_idx;
            presentInfo.pResults = nullptr;

            vkQueuePresentKHR(gpu.present_queue, &presentInfo);
        }

        glfwPollEvents();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
