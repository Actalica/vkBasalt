#include "compute_pipeline.hpp"

#ifndef ASSERT_VULKAN
#define ASSERT_VULKAN(val)\
        if(val!=VK_SUCCESS)\
        {\
            throw std::runtime_error("ASSERT_VULKAN failed " + val);\
        }
#endif

namespace vkBasalt
{
    void createComputePipelineLayouts(const VkDevice& device, const VkLayerDispatchTable& dispatchTable, const uint32_t& count, const VkDescriptorSetLayout* descriptorSetLayouts, VkPipelineLayout* pipelineLayouts)
    {   
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = nullptr;
        pipelineLayoutCreateInfo.flags = 0;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = VK_NULL_HANDLE;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
        
        
        for(unsigned int i=0;i<count;i++)
        {
            pipelineLayoutCreateInfo.pSetLayouts = &(descriptorSetLayouts[i]);
            VkResult result = dispatchTable.CreatePipelineLayout(device,&pipelineLayoutCreateInfo,nullptr,&(pipelineLayouts[i]));
            ASSERT_VULKAN(result);
        }
    }
    
    
    void createComputePipelines(const VkDevice& device, const VkLayerDispatchTable& dispatchTable,const VkShaderModule& shaderModule,const uint32_t& count,const VkPipelineLayout* pipelineLayouts, VkPipeline* pipelines)
    {
        VkPipelineShaderStageCreateInfo computeStageCreateInfo;
        computeStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeStageCreateInfo.pNext = nullptr;
        computeStageCreateInfo.flags = 0;
        computeStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeStageCreateInfo.module = shaderModule;
        computeStageCreateInfo.pName = "main";
        computeStageCreateInfo.pSpecializationInfo = nullptr;
        
        
        
        
        VkComputePipelineCreateInfo computePipelineCreateInfo;
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.pNext = nullptr;
        computePipelineCreateInfo.flags = 0;
        computePipelineCreateInfo.stage = computeStageCreateInfo;
        computePipelineCreateInfo.layout = VK_NULL_HANDLE;
        computePipelineCreateInfo.basePipelineHandle = 0;//TODO
        computePipelineCreateInfo.basePipelineIndex = 0;//TODO
        
        
        for(unsigned int i=0;i<count;i++)
        {
            computePipelineCreateInfo.layout = pipelineLayouts[i];
            VkResult result = dispatchTable.CreateComputePipelines(device,VK_NULL_HANDLE,1,&computePipelineCreateInfo,nullptr,&(pipelines[i]));
            ASSERT_VULKAN(result);
        }
    }
}
