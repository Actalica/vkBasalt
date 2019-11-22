#ifndef GRAPHICS_PIPELINE_HPP_INCLUDED
#define GRAPHICS_PIPELINE_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <vector>

#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vk_layer_dispatch_table.h"

namespace vkBasalt
{
    VkPipelineLayout createGraphicsPipelineLayout(VkDevice device, VkLayerDispatchTable dispatchTable, std::vector<VkDescriptorSetLayout> descriptorSetLayouts);
    VkPipeline createGraphicsPipeline(VkDevice device,
                                      VkLayerDispatchTable dispatchTable,
                                      VkShaderModule vertexModule,
                                      VkSpecializationInfo* vertexSpecializationInfo,
                                      VkShaderModule fragmentModule,
                                      VkSpecializationInfo* fragmentSpecializationInfo,
                                      VkExtent2D extent,
                                      VkRenderPass renderPass,
                                      VkPipelineLayout pipelineLayout);

}


#endif // GRAPHICS_PIPELINE_HPP_INCLUDED
