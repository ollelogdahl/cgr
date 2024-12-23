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

    bool is_discrete = device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
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

    return is_discrete && supports_geometry_shader && supports_extensions;
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

};

struct shader_program_load_params_t {
    const char *vertex_hlsl_path;
    const char *fragment_hlsl_path;

    bool hotload = true;
};

struct gpu_t {
    VkInstance instance;
    VkPhysicalDevice pdev = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphics_queue, present_queue;
    VkSurfaceKHR surface;

    // can maybe be broken out but i don't see why.
    struct {
        VkSwapchainKHR handle;
        std::vector<VkImage> images;
        VkFormat image_format;
        VkExtent2D extent;

        std::vector<VkImageView> image_views;
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

                createInfo.enabledExtensionCount = glfwExtensionCount;
                createInfo.ppEnabledExtensionNames = glfwExtensions;
            }

            createInfo.enabledLayerCount = 0;

            VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
            if (result != VK_SUCCESS) {
                gpu_log.error("failed to create instance");
                return;
            }
            gpu_log.info("vulkan instance created");
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

            if (validation_layers_available) {
                createInfo.enabledLayerCount = array_size(validation_layers);
                createInfo.ppEnabledLayerNames = validation_layers;
            }

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
            for (auto i = 0; i < swapchain.images.size(); i++) {
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
        const auto code = file_read(path).unwrap();

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.content.len;
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.content.ptr);

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(gpu.device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            gpu_log.error("failed to create shader module");
        }

        file_close(code);
        return shaderModule;
    };

    VkShaderModule frag;
    VkShaderModule vert;

    if (params.vertex_hlsl_path != nullptr) {
        // invoke glslc to compile the shader
        // we could also use libshaderc, but i think that will be more complicated.
        // @todo: use forks instead of system.
        auto cmd = fmt::format("glslc -fshader-stage=vertex -o /tmp/1.spv {}", params.vertex_hlsl_path);
        system(cmd.c_str());
        vert = create_module("/tmp/1.spv");
    }

    if (params.fragment_hlsl_path != nullptr) {
        auto cmd = fmt::format("glslc -fshader-stage=fragment -o /tmp/2.spv {}", params.fragment_hlsl_path);
        system(cmd.c_str());
        frag = create_module("/tmp/2.spv");
    }

    // @todo: consider sharing the same module sometimes?
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;

    return program;
}

int main(void) {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(800, 600, "vulkan", nullptr, nullptr);

    // initialize vulkan
    gpu_t gpu;
    gpu.init(window);
    std::cout << "vulkan initialized" << std::endl;

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);

    glfwTerminate();
}
