#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "oc.h"
#include "log.h"

#include <iostream>
#include <vector>
#include <cstring>
#include <set>

static logger_t gpu_log = logger_t("gpu");

#include <sys/inotify.h>
#include <fcntl.h>

struct fswatcher_t {
    int inotify_fd;

    void init() {
        inotify_fd = inotify_init();
        auto old_flags = fcntl(inotify_fd, F_GETFL);
        fcntl(inotify_fd, F_SETFL, old_flags | O_NONBLOCK);
    }

    void fd_add_modify_watch(const char *path) {
        inotify_add_watch(inotify_fd, path, IN_MODIFY);
    }
    void process_watches() {
        // read from inotify until no more events are available now
        const usize event_max_size = sizeof(inotify_event) + 256;
        const usize buffer_size = 128 * event_max_size;
        char buffer[buffer_size];

        while (true) {
            auto len = read(inotify_fd, buffer, buffer_size);

            auto ev = (inotify_event *)&buffer[0];
            auto end = (inotify_event *)&buffer[len];
            while(ev < end) {
                if (ev->len) {
                    g_log.info("got event for: {}", ev->name);
                }

                ev += sizeof(inotify_event) + ev->len;
            }
        }
    }
};

struct hotloader_t {
    int fd_watcher;

    void check_hotload();
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

struct shader_program_t {
    VkPipelineShaderStageCreateInfo frag;
    VkPipelineShaderStageCreateInfo vert;
};

struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool hotload = true;
};

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

    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        gpu_log.error("validation error: {}", pCallbackData->pMessage);
        panic("aborting on validation error!");
    }

    return VK_FALSE;
}


struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    VkSurfaceKHR surface;

    VkDebugUtilsMessengerEXT debug_messager;

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
        bool requests_validation_layers = true;
        bool validation_layers_available = true;

        const char *validation_layers[] = {
            "VK_LAYER_KHRONOS_validation"
        };

        std::vector<const char *> required_device_extensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

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

            // load the function
            auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

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

            const char *device_name;
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

        // @todo: break this out when we support resizing.
        {
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

            u32 qfamilies_separate[] = {qfamily_graphics, qfamily_present};
            if (qfamily_graphics != qfamily_present) {
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
            gpu_log.info("swap chain created");

            vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, nullptr);
            swapchain.images.resize(image_count);
            vkGetSwapchainImagesKHR(device, swapchain.handle, &image_count, swapchain.images.data());

            swapchain.image_format = surface_format.format;
            swapchain.extent = extent;
        }

        {
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
        }
    }
};

shader_program_t load_shader_program(gpu_t &gpu, const shader_program_load_params_t &params) {
    // @todo: handle errors
    shader_program_t program;

    auto create_module = [&](const char *path) {
        auto code = file_read(path).unwrap();

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.contents.len;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.contents.data);

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(gpu.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            gpu_log.error("failed to create shader module");
        }

        file_close(code);
        return shaderModule;
    };

    VkShaderModule frag;
    VkShaderModule vert;

    //if (params.vertex_hlsl_path != nullptr) {
    {
        // invoke glslc to compile the shader
        // we could also use libshaderc, but i think that will be more complicated.
        // @todo: use forks instead of system.
        auto cmd = fmt::format("glslc -fshader-stage=vertex -o /tmp/1.spv {}", params.vertex_hlsl_path);
        system(cmd.c_str());
        vert = create_module("/tmp/1.spv");
    }

    //if (params.fragment_hlsl_path != nullptr) {
    {
        auto cmd = fmt::format("glslc -fshader-stage=fragment -o /tmp/2.spv {}", params.fragment_hlsl_path);
        system(cmd.c_str());
        frag = create_module("/tmp/2.spv");
    }

    // @todo: consider sharing the same module sometimes?
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vert;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = frag;
    fragShaderStageInfo.pName = "main";

    program.frag = fragShaderStageInfo;
    program.vert = vertShaderStageInfo;

    return program;
}

int main(void) {
    oc_init();

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "vulkan", nullptr, nullptr);

    // initialize vulkan
    gpu_t gpu;
    gpu.init(window);
    std::cout << "vulkan initialized" << std::endl;

    // create a test pipeline and pass
    VkPipeline pipeline;
    VkRenderPass renderPass;
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

        // create a test render pass
        {
            VkAttachmentDescription colorAttachment{};
            colorAttachment.format = gpu.swapchain.image_format;
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

            if (vkCreateRenderPass(gpu.device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
                throw std::runtime_error("failed to create render pass!");
            }
        }


        auto shader = load_shader_program(gpu, {
           .vertex_hlsl_path = "eassets/shaders/test.vert",
           .fragment_hlsl_path = "eassets/shaders/test.frag",
        });

        // create the pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;

        VkPipelineShaderStageCreateInfo stages[] = {
            shader.vert, shader.frag
        };

        pipelineInfo.pStages = stages;

        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr; // Optional
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;

        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        if (vkCreateGraphicsPipelines(gpu.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }
    }

    // setup the swapchain framebuffers
    {
        for (usize i = 0; i < gpu.swapchain.image_views.size(); ++i) {
            VkImageView attachments[] = {
                gpu.swapchain.image_views[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = gpu.swapchain.extent.width;
            framebufferInfo.height = gpu.swapchain.extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(gpu.device, &framebufferInfo, nullptr, &gpu.swapchain.framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) gpu.swapchain.extent.width;
    viewport.height = (float) gpu.swapchain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = gpu.swapchain.extent;

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
