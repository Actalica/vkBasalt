#ifndef SAMPLER_HPP_INCLUDED
#define SAMPLER_HPP_INCLUDED
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
    VkSampler createSampler(VkDevice device, VkLayerDispatchTable dispatchTable);
}

#endif // SAMPLER_HPP_INCLUDED
