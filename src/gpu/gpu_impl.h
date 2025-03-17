#include "gpu.h"

#include "log.h"

extern logger_t gpu_log;


const char * vk_result_to_cstr(VkResult result);
// void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);

const char *format_as(VkResult);

extern std::vector<VkPresentModeKHR> present_modes_in_order_of_preference;
