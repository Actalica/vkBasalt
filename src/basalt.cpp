//BSD 2-Clause License
//
//Copyright (c) 2016, Baldur Karlsson
//All rights reserved.
//
//Redistribution and use in source and binary forms, with or without
//modification, are permitted provided that the following conditions are met:
//
//* Redistributions of source code must retain the above copyright notice, this
//  list of conditions and the following disclaimer.
//
//* Redistributions in binary form must reproduce the above copyright notice,
//  this list of conditions and the following disclaimer in the documentation
//  and/or other materials provided with the distribution.
//
//THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vk_layer_dispatch_table.h"
#include "vulkan/vk_dispatch_table_helper.h"

#include <mutex>
#include <map>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <string>

#include "image_view.hpp"
#include "sampler.hpp"
#include "framebuffer.hpp"
#include "descriptor_set.hpp"
#include "shader.hpp"
#include "graphics_pipeline.hpp"
#include "command_buffer.hpp"
#include "buffer.hpp"
#include "config.hpp"
#include "fake_swapchain.hpp"
#include "renderpass.hpp"

#ifndef ASSERT_VULKAN
#define ASSERT_VULKAN(val)\
        if(val!=VK_SUCCESS)\
        {\
            throw std::runtime_error("ASSERT_VULKAN failed " + std::to_string(val));\
        }
#endif

std::string fullScreenRectFile = std::string(getenv("HOME")) + "/.local/share/vkBasalt/shader/full_screen_rect.vert.spv";
std::string casFragmentFile = std::string(getenv("HOME")) + "/.local/share/vkBasalt/shader/cas.frag.spv";
std::string fxaaFragmentFile = std::string(getenv("HOME")) + "/.local/share/vkBasalt/shader/fxaa.frag.spv";

std::mutex globalLock;
#ifdef _GCC_
typedef std::lock_guard<std::mutex> scoped_lock __attribute__((unused)) ;
#else
typedef std::lock_guard<std::mutex> scoped_lock;
#endif

template<typename DispatchableType>
void *GetKey(DispatchableType inst)
{
    return *(void **)inst;
}


typedef struct {
    float sharpness;
} CasBufferObject;

CasBufferObject casUBO = {0.4f};
vkBasalt::Config casConfig;



// layer book-keeping information, to store dispatch tables by key
std::map<void *, VkLayerInstanceDispatchTable> instance_dispatch;
std::map<void *, VkLayerDispatchTable> device_dispatch;


//for each swapchain, we have the Images and the other stuff we need to execute the compute shader
typedef struct {
    VkDevice device;
    VkSwapchainCreateInfoKHR swapchainCreateInfo;
    VkExtent2D imageExtent;
    VkFormat format;
    uint32_t imageCount;
    VkImage *imageList;
    VkImage *fakeImageList;
    VkImageView *imageViewList;
    VkImageView *fakeImageViewList;
    VkDescriptorSet *descriptorSetList;
    VkCommandBuffer *commandBufferList;
    VkSemaphore *semaphoreList;
    VkFramebuffer *framebufferList;
    VkDescriptorPool imageSamplerDescriptorPool;
    VkDeviceMemory fakeImageMemory;
    VkRenderPass renderPass;
    VkPipeline casGraphicsPipeline;
} SwapchainStruct;

typedef struct {
    VkQueue queue;
    uint32_t queueFamilyIndex;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkDescriptorSetLayout imageSamplerDescriptorSetLayout;
    VkShaderModule fullScreenRectModule;
    VkShaderModule casFragmentModule;
    VkPipelineLayout casPipelineLayout;
    VkDescriptorSetLayout uniformBufferDescriptorSetLayout;
    VkDescriptorPool uniformBufferDescriptorPool;
    VkDescriptorSet casUniformBufferDescriptorSet;
    VkBuffer casUniformBuffer;
    VkDeviceMemory casUniformBufferMemory;
    VkSampler sampler;
} DeviceStruct;

std::unordered_map<VkDevice, DeviceStruct> deviceMap;
std::unordered_map<VkSwapchainKHR, SwapchainStruct> swapchainMap;

namespace vkBasalt{
    void destroySwapchainStruct(SwapchainStruct& swapchainStruct)
    {
        VkDevice device = swapchainStruct.device;
        VkLayerDispatchTable& dispatchTable = device_dispatch[GetKey(device)];
        if(swapchainStruct.imageCount>0)
        {
            dispatchTable.FreeCommandBuffers(device,deviceMap[device].commandPool,swapchainStruct.imageCount, swapchainStruct.commandBufferList);
            delete[] swapchainStruct.commandBufferList;
            std::cout << "after free commandbuffer" << std::endl;
            dispatchTable.DestroyDescriptorPool(device,swapchainStruct.imageSamplerDescriptorPool,nullptr);
            std::cout << "after DestroyDescriptorPool" << std::endl;
            delete[] swapchainStruct.imageList;
            dispatchTable.FreeMemory(device,swapchainStruct.fakeImageMemory,nullptr);
            for(unsigned int i=0;i<swapchainStruct.imageCount;i++)
            {
                dispatchTable.DestroyFramebuffer(device,swapchainStruct.framebufferList[i],nullptr);
                dispatchTable.DestroyImageView(device,swapchainStruct.fakeImageViewList[i],nullptr);
                dispatchTable.DestroyImage(device,swapchainStruct.fakeImageList[i],nullptr);
                dispatchTable.DestroySemaphore(device,swapchainStruct.semaphoreList[i],nullptr);
                std::cout << "after DestroySemaphore" << std::endl;
                dispatchTable.DestroyImageView(device,swapchainStruct.imageViewList[i],nullptr);
                std::cout << "after DestroyImageView" << std::endl;
            }
            dispatchTable.DestroyPipeline(device, swapchainStruct.casGraphicsPipeline, nullptr);
            dispatchTable.DestroyRenderPass(device,swapchainStruct.renderPass,nullptr);
            delete[] swapchainStruct.fakeImageList;
            delete[] swapchainStruct.imageViewList;
            delete[] swapchainStruct.fakeImageViewList;
            delete[] swapchainStruct.framebufferList;
            delete[] swapchainStruct.descriptorSetList;
            delete[] swapchainStruct.semaphoreList;
        } 
    }
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
    VkLayerInstanceCreateInfo *layerCreateInfo = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;

    // step through the chain of pNext until we get to the link info
    while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
        layerCreateInfo = (VkLayerInstanceCreateInfo *)layerCreateInfo->pNext;
    }
    std::cout << "interrupted create instance" << std::endl;
    if(layerCreateInfo == NULL)
    {
    // No loader instance create info
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gpa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    // move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createFunc = (PFN_vkCreateInstance)gpa(VK_NULL_HANDLE, "vkCreateInstance");

    VkResult ret = createFunc(pCreateInfo, pAllocator, pInstance);

    // fetch our own dispatch table for the functions we need, into the next layer
    VkLayerInstanceDispatchTable dispatchTable;
    layer_init_instance_dispatch_table(*pInstance,&dispatchTable,gpa);
    
    // store the table by key
    {
        scoped_lock l(globalLock);
        casUBO.sharpness = std::stod(casConfig.getOption("casSharpness"));
        if(casConfig.getOption("enableFxaa")==std::string("1"))
        {
            casFragmentFile = fxaaFragmentFile;
        }
        instance_dispatch[GetKey(*pInstance)] = dispatchTable;
    }

    return ret;
}

VK_LAYER_EXPORT void VKAPI_CALL vkBasalt_DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    scoped_lock l(globalLock);
    VkLayerInstanceDispatchTable dispatchTable = instance_dispatch[instance];
    std::cout << "before destroy instance " << dispatchTable.DestroyInstance << std::endl;
    //dispatchTable.DestroyInstance(instance, pAllocator);
    std::cout << "afer destroy instance" << std::endl;
    instance_dispatch.erase(GetKey(instance));
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
    VkLayerDeviceCreateInfo *layerCreateInfo = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;

    // step through the chain of pNext until we get to the link info
    while(layerCreateInfo && (layerCreateInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO ||
                            layerCreateInfo->function != VK_LAYER_LINK_INFO))
    {
    layerCreateInfo = (VkLayerDeviceCreateInfo *)layerCreateInfo->pNext;
    }

    if(layerCreateInfo == NULL)
    {
    // No loader instance create info
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa = layerCreateInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = layerCreateInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    // move chain on for next layer
    layerCreateInfo->u.pLayerInfo = layerCreateInfo->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createFunc = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");

    VkResult ret = createFunc(physicalDevice, pCreateInfo, pAllocator, pDevice);
    
    // fetch our own dispatch table for the functions we need, into the next layer
    VkLayerDispatchTable dispatchTable;
    layer_init_device_dispatch_table(*pDevice,&dispatchTable,gdpa);
    
    DeviceStruct deviceStruct;
    deviceStruct.queue = VK_NULL_HANDLE;
    deviceStruct.physicalDevice = physicalDevice;
    deviceStruct.commandPool = VK_NULL_HANDLE;
    
    VkDeviceSize casBufferSize = sizeof(CasBufferObject);
    //TODO make buffer device local
    vkBasalt::createBuffer(instance_dispatch[GetKey(physicalDevice)],*pDevice,dispatchTable,physicalDevice,casBufferSize,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,deviceStruct.casUniformBuffer,deviceStruct.casUniformBufferMemory);
    
    vkBasalt::createUniformBufferDescriptorSetLayout(*pDevice, dispatchTable,  deviceStruct.uniformBufferDescriptorSetLayout);
    vkBasalt::createUniformBufferDescriptorPool(*pDevice, dispatchTable, 1, deviceStruct.uniformBufferDescriptorPool);
    vkBasalt::writeCasBufferDescriptorSet(*pDevice, dispatchTable, deviceStruct.uniformBufferDescriptorPool, deviceStruct.uniformBufferDescriptorSetLayout, deviceStruct.casUniformBuffer,  deviceStruct.casUniformBufferDescriptorSet);
    
    vkBasalt::createImageSamplerDescriptorSetLayout(*pDevice, dispatchTable, deviceStruct.imageSamplerDescriptorSetLayout);
    
    auto fullScreenRectCode = vkBasalt::readFile(fullScreenRectFile.c_str());
    vkBasalt::createShaderModule(*pDevice, dispatchTable, fullScreenRectCode, &deviceStruct.fullScreenRectModule);
    
    auto casFragmentCode = vkBasalt::readFile(casFragmentFile.c_str());
    vkBasalt::createShaderModule(*pDevice, dispatchTable, casFragmentCode, &deviceStruct.casFragmentModule);
    
    std::array<VkDescriptorSetLayout, 2> layouts= {deviceStruct.imageSamplerDescriptorSetLayout,deviceStruct.uniformBufferDescriptorSetLayout};
    vkBasalt::createGraphicsPipelineLayout(*pDevice, dispatchTable,layouts.size(), layouts.data(), deviceStruct.casPipelineLayout);
    
    vkBasalt::createSampler(*pDevice,dispatchTable,deviceStruct.sampler);
    
    void* data;
    VkResult result = dispatchTable.MapMemory(*pDevice, deviceStruct.casUniformBufferMemory, 0, sizeof(CasBufferObject), 0, &data);
    ASSERT_VULKAN(result);
    std::memcpy(data, &casUBO, sizeof(CasBufferObject));
    dispatchTable.UnmapMemory(*pDevice, deviceStruct.casUniformBufferMemory);
    
    std::cout << casConfig.getOption("casSharpness") << std::endl;
    

    // store the table by key
    {
        scoped_lock l(globalLock);
        device_dispatch[GetKey(*pDevice)] = dispatchTable;
        deviceMap[*pDevice] = deviceStruct;
        
    }

    return ret;
}

VK_LAYER_EXPORT void VKAPI_CALL vkBasalt_DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    scoped_lock l(globalLock);
    DeviceStruct& deviceStruct = deviceMap[device];
    if(deviceStruct.commandPool != VK_NULL_HANDLE)
    {
        std::cout << "DestroyCommandPool" << std::endl;
        device_dispatch[GetKey(device)].DestroyCommandPool(device,deviceStruct.commandPool,pAllocator);
    }
    device_dispatch[GetKey(device)].DestroySampler(device,deviceStruct.sampler,nullptr);
    device_dispatch[GetKey(device)].DestroyPipelineLayout(device,deviceStruct.casPipelineLayout,nullptr);
    std::cout << "after DestroyPipelineLayout" << std::endl;
    device_dispatch[GetKey(device)].DestroyDescriptorSetLayout(device,deviceStruct.imageSamplerDescriptorSetLayout,nullptr);
    device_dispatch[GetKey(device)].DestroyShaderModule(device,deviceStruct.casFragmentModule,nullptr);
    device_dispatch[GetKey(device)].DestroyShaderModule(device,deviceStruct.fullScreenRectModule,nullptr);
    
    
    device_dispatch[GetKey(device)].DestroyDescriptorPool(device,deviceStruct.uniformBufferDescriptorPool,nullptr);
    device_dispatch[GetKey(device)].FreeMemory(device,deviceStruct.casUniformBufferMemory,nullptr);
    device_dispatch[GetKey(device)].DestroyDescriptorSetLayout(device,deviceStruct.uniformBufferDescriptorSetLayout,nullptr);
    device_dispatch[GetKey(device)].DestroyBuffer(device,deviceStruct.casUniformBuffer,nullptr);
    
    VkLayerDispatchTable dispatchTable = device_dispatch[GetKey(device)];
    std::cout << "before Destroy Device" << dispatchTable.DestroyDevice << std::endl;
    dispatchTable.DestroyDevice(device,pAllocator);
    
    device_dispatch.erase(GetKey(device));
    
    std::cout << "after  Destroy Device" << std::endl;
}

VKAPI_ATTR void VKAPI_CALL vkBasalt_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue)
{
    scoped_lock l(globalLock);
    device_dispatch[GetKey(device)].GetDeviceQueue(device,queueFamilyIndex,queueIndex,pQueue);
    DeviceStruct& deviceStruct = deviceMap[device];
    
    if(deviceStruct.queue != VK_NULL_HANDLE)
    {
        return;//we allready have a queue
    }
    
    //Save the first graphic capable in our deviceMap
    uint32_t count;
    VkBool32 graphicsCapable = VK_FALSE;
    //TODO also check if the queue is present capable
    instance_dispatch[GetKey(deviceStruct.physicalDevice)].GetPhysicalDeviceQueueFamilyProperties(deviceStruct.physicalDevice, &count, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueProperties(count);
    
    if(count > 0)
    {
        instance_dispatch[GetKey(deviceStruct.physicalDevice)].GetPhysicalDeviceQueueFamilyProperties(deviceStruct.physicalDevice, &count, queueProperties.data());
        if((queueProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            graphicsCapable = VK_TRUE;
        }
    }
    else
    {
        //TODO
        graphicsCapable = VK_TRUE;
    }
    
    if(graphicsCapable)
    {   
        VkCommandPoolCreateInfo commandPoolCreateInfo;
        commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolCreateInfo.pNext = nullptr;
        commandPoolCreateInfo.flags = 0;
        commandPoolCreateInfo.queueFamilyIndex = queueFamilyIndex;
        
        std::cout << "found graphic capable queue" << std::endl;
        device_dispatch[GetKey(device)].CreateCommandPool(device,&commandPoolCreateInfo,nullptr,&deviceStruct.commandPool);
        deviceStruct.queue = *pQueue;
        deviceStruct.queueFamilyIndex = queueFamilyIndex;
        std::cout << (deviceMap[device].queue == *pQueue) << std::endl;
        std::cout << "queue " << *pQueue << std::endl; 
        std::cout << "queue " << deviceMap[device].queue << std::endl;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    VkSwapchainCreateInfoKHR modifiedCreateInfo = *pCreateInfo;
    modifiedCreateInfo.imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;//we want to use the swapchain images as storage images
    scoped_lock l(globalLock);
    
    if(modifiedCreateInfo.oldSwapchain != VK_NULL_HANDLE)
    {
        //we need to delete the infos of the oldswapchain
        // TODO do we really? it seems as if afer recreating a swapchain the old one is destroyed with vkDestroySwapchain 
        /*std::cout << "oldswapchain != VK_NULL_HANDLE  swapchain " << modifiedCreateInfo.oldSwapchain << std::endl;
        SwapchainStruct& oldStruct = swapchainMap[modifiedCreateInfo.oldSwapchain];
        vkBasalt::destroySwapchainStruct(oldStruct);*/
    }
    std::cout << "queue " << deviceMap[device].queue << std::endl;
    std::cout << "format " << modifiedCreateInfo.imageFormat << std::endl;
    SwapchainStruct swapchainStruct;
    swapchainStruct.device = device;
    swapchainStruct.swapchainCreateInfo = *pCreateInfo;
    swapchainStruct.imageExtent = modifiedCreateInfo.imageExtent;
    swapchainStruct.format = modifiedCreateInfo.imageFormat;
    swapchainStruct.imageCount = 0;
    swapchainStruct.imageList = nullptr;
    swapchainStruct.imageViewList = nullptr;
    swapchainStruct.descriptorSetList =nullptr;
    swapchainStruct.commandBufferList = nullptr;
    swapchainStruct.semaphoreList = nullptr;
    swapchainStruct.imageSamplerDescriptorPool = VK_NULL_HANDLE;
    std::cout << "device " << swapchainStruct.device << std::endl;
    
    DeviceStruct& deviceStruct = deviceMap[device];
        
    vkBasalt::createRenderPass(device, device_dispatch[GetKey(device)], swapchainStruct.format, swapchainStruct.renderPass);
    vkBasalt::createGraphicsPipeline(device, device_dispatch[GetKey(device)], deviceStruct.fullScreenRectModule, deviceStruct.casFragmentModule, modifiedCreateInfo.imageExtent, swapchainStruct.renderPass, deviceStruct.casPipelineLayout, swapchainStruct.casGraphicsPipeline);
    
    VkResult result = device_dispatch[GetKey(device)].CreateSwapchainKHR(device, &modifiedCreateInfo, pAllocator, pSwapchain);
    
    swapchainMap[*pSwapchain] = swapchainStruct;
    std::cout << "swapchain " << *pSwapchain << std::endl;
    
    std::cout << "Interrupted create swapchain" << std::endl;
    
    
    
    return result;
}       

VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pCount, VkImage *pSwapchainImages) 
{
    scoped_lock l(globalLock);
    std::cout << "Interrupted get swapchain images " << *pCount << std::endl;
    if(pSwapchainImages==nullptr)
    {
        return device_dispatch[GetKey(device)].GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
    }
    
    
    DeviceStruct& deviceStruct = deviceMap[device];
    std::cout << "queue " << deviceStruct.queue << std::endl;
    std::cout << "swapchain " << swapchain << std::endl;
    SwapchainStruct& swapchainStruct = swapchainMap[swapchain];
    swapchainStruct.imageCount = *pCount;
    swapchainStruct.imageList = new VkImage[*pCount];
    swapchainStruct.fakeImageList = new VkImage[*pCount];
    swapchainStruct.imageViewList = new VkImageView[*pCount];
    swapchainStruct.fakeImageViewList = new VkImageView[*pCount];
    swapchainStruct.descriptorSetList = new VkDescriptorSet[*pCount];
    swapchainStruct.commandBufferList = new VkCommandBuffer[*pCount];
    swapchainStruct.semaphoreList = new VkSemaphore[*pCount];
    swapchainStruct.framebufferList = new VkFramebuffer[*pCount];
    std::cout << "format " << swapchainStruct.format << std::endl;
    std::cout << "device " << swapchainStruct.device << std::endl;
    
    vkBasalt::createFakeSwapchainImages(instance_dispatch[GetKey(deviceStruct.physicalDevice)], deviceStruct.physicalDevice, device,  device_dispatch[GetKey(device)], swapchainStruct.swapchainCreateInfo, *pCount, swapchainStruct.fakeImageList, swapchainStruct.fakeImageMemory);
    std::cout << "after createFakeSwapchainImages " << std::endl;
    
    
    VkResult result = device_dispatch[GetKey(device)].GetSwapchainImagesKHR(device, swapchain, pCount, pSwapchainImages);
    for(unsigned int i=0;i<*pCount;i++)
    {
        swapchainStruct.imageList[i] = pSwapchainImages[i];
        pSwapchainImages[i] = swapchainStruct.fakeImageList[i];
    }
    std::cout << "before creating swapchain image views " << std::endl;
    vkBasalt::createImageViews(device,device_dispatch[GetKey(device)],swapchainStruct.format,swapchainStruct.imageCount,swapchainStruct.imageList,swapchainStruct.imageViewList);
    
    std::cout << "before creating framebuffers " << std::endl;
    vkBasalt::createFramebuffers(device,device_dispatch[GetKey(device)],swapchainStruct.imageCount, swapchainStruct.renderPass, swapchainStruct.imageExtent, swapchainStruct.imageViewList, swapchainStruct.framebufferList);
    
    std::cout << "before creating fake swapchain image views " << std::endl;
    vkBasalt::createImageViews(device,device_dispatch[GetKey(device)],swapchainStruct.format,swapchainStruct.imageCount,swapchainStruct.fakeImageList, swapchainStruct.fakeImageViewList);
    
    std::cout << "before creating descriptor Pool " << std::endl;
    vkBasalt::createImageSamplerDescriptorPool(device, device_dispatch[GetKey(device)], swapchainStruct.imageCount, swapchainStruct.imageSamplerDescriptorPool);
    std::cout << "after creating descriptor Pool " << std::endl;
    vkBasalt::allocateAndWriteImageSamplerDescriptorSets(device, device_dispatch[GetKey(device)], swapchainStruct.imageSamplerDescriptorPool, swapchainStruct.imageCount, deviceStruct.imageSamplerDescriptorSetLayout, deviceStruct.sampler, swapchainStruct.fakeImageViewList, swapchainStruct.descriptorSetList);
    
    std::cout << "before creating descriptor Pool " << std::endl;
    
    vkBasalt::allocateCommandBuffer(device, device_dispatch[GetKey(device)], deviceMap[device].commandPool,swapchainStruct.imageCount , swapchainStruct.commandBufferList);
    std::cout << "after allocateCommandBuffer " << std::endl;
    vkBasalt::writeCASCommandBuffers(device, device_dispatch[GetKey(device)], swapchainStruct.casGraphicsPipeline, deviceStruct.casPipelineLayout, swapchainStruct.imageExtent, swapchainStruct.imageCount, deviceStruct.casUniformBufferDescriptorSet, swapchainStruct.renderPass, swapchainStruct.fakeImageList, swapchainStruct.descriptorSetList, swapchainStruct.framebufferList, swapchainStruct.commandBufferList);
    vkBasalt::createSemaphores(device, device_dispatch[GetKey(device)], swapchainStruct.imageCount, swapchainStruct.semaphoreList);
    for(unsigned int i=0;i<swapchainStruct.imageCount;i++)
    {
        std::cout << i << "writen commandbuffer" << swapchainStruct.commandBufferList[i] << std::endl;
    }
    
    
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBasalt_QueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* pPresentInfo)
{
    scoped_lock l(globalLock);
    //std::cout << "Interrupted QueuePresentKHR" << std::endl;

    std::vector<VkSemaphore> presentSemaphores;
    presentSemaphores.reserve(pPresentInfo->swapchainCount);

    std::vector<VkPipelineStageFlags> waitStages;

    for(unsigned int i=0;i<(*pPresentInfo).swapchainCount;i++)
    {
        uint32_t index = (*pPresentInfo).pImageIndices[i];
        VkSwapchainKHR swapchain = (*pPresentInfo).pSwapchains[i];
        SwapchainStruct& swapchainStruct = swapchainMap[swapchain];
        VkDevice device = swapchainStruct.device;
        DeviceStruct& deviceStruct = deviceMap[device];
        
        //device_dispatch[GetKey(device)].QueueWaitIdle(queue);
        //device_dispatch[GetKey(device)].DeviceWaitIdle(device);
        //std::cout << (*pPresentInfo).pImageIndices[i] << std::endl;
        
        //std::cout << &(swapchainStruct.commandBufferList[index]) << std::endl;
        //std::cout << swapchainStruct.commandBufferList[index] << std::endl;
        
        waitStages.resize(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        VkSubmitInfo submitInfo;
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.pWaitSemaphores = nullptr;
        submitInfo.pWaitDstStageMask = nullptr;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(swapchainStruct.commandBufferList[index]);
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &(swapchainStruct.semaphoreList[index]);

        presentSemaphores.push_back(swapchainStruct.semaphoreList[index]);

        if (i == 0)
        {
            submitInfo.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
            submitInfo.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
            submitInfo.pWaitDstStageMask = waitStages.data();
        }

        VkResult vr = device_dispatch[GetKey(device)].QueueSubmit(deviceStruct.queue, 1, &submitInfo, VK_NULL_HANDLE);

        if (vr != VK_SUCCESS)
            return vr;
        //device_dispatch[GetKey(device)].QueueWaitIdle(deviceStruct.queue);
        //device_dispatch[GetKey(device)].DeviceWaitIdle(device);
        
        
    }
    //usleep(1000000);
    VkPresentInfoKHR presentInfo = *pPresentInfo;
    presentInfo.waitSemaphoreCount = presentSemaphores.size();
    presentInfo.pWaitSemaphores = presentSemaphores.data();

    return device_dispatch[GetKey(queue)].QueuePresentKHR(queue, &presentInfo);
}

VKAPI_ATTR void VKAPI_CALL vkBasalt_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,const VkAllocationCallbacks* pAllocator)
{
    scoped_lock l(globalLock);
    //we need to delete the infos of the oldswapchain 
    SwapchainStruct& oldStruct = swapchainMap[swapchain];
    std::cout << "destroying swapchain " << swapchain << std::endl;
    vkBasalt::destroySwapchainStruct(oldStruct);
    
    device_dispatch[GetKey(device)].DestroySwapchainKHR(device, swapchain,pAllocator);
}
///////////////////////////////////////////////////////////////////////////////////////////
// Enumeration function

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                                                       VkLayerProperties *pProperties)
{
    if(pPropertyCount) *pPropertyCount = 1;

    if(pProperties)
    {
        strcpy(pProperties->layerName, "VK_LAYER_SAMPLE_SampleLayer");
        strcpy(pProperties->description, "Sample layer - https://renderdoc.org/vulkan-layer-guide.html");
        pProperties->implementationVersion = 1;
        pProperties->specVersion = VK_API_VERSION_1_0;
    }

    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_EnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount, VkLayerProperties *pProperties)
{
    return vkBasalt_EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_EnumerateInstanceExtensionProperties(
    const char *pLayerName, uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    if(pLayerName == NULL || strcmp(pLayerName, "VK_LAYER_SAMPLE_SampleLayer"))
    return VK_ERROR_LAYER_NOT_PRESENT;

    // don't expose any extensions
    if(pPropertyCount) *pPropertyCount = 0;
    return VK_SUCCESS;
}

VK_LAYER_EXPORT VkResult VKAPI_CALL vkBasalt_EnumerateDeviceExtensionProperties(
                                     VkPhysicalDevice physicalDevice, const char *pLayerName,
                                     uint32_t *pPropertyCount, VkExtensionProperties *pProperties)
{
    // pass through any queries that aren't to us
    if(pLayerName == NULL || strcmp(pLayerName, "VK_LAYER_SAMPLE_SampleLayer"))
    {
        if(physicalDevice == VK_NULL_HANDLE)
          return VK_SUCCESS;

        scoped_lock l(globalLock);
        return instance_dispatch[GetKey(physicalDevice)].EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    // don't expose any extensions
    if(pPropertyCount) *pPropertyCount = 0;
    return VK_SUCCESS;
}

extern "C"{// these are the entry points for the layer, so they need to be c-linkeable

#define GETPROCADDR(func) if(!strcmp(pName, "vk" #func)) return (PFN_vkVoidFunction)&vkBasalt_##func;
/*
Return our funktions for the funktions we want to intercept
the macro takes the name and returns our vkBasalt_##func, if the name is equal
*/
VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetDeviceProcAddr(VkDevice device, const char *pName)
{
    // device chain functions we intercept
    GETPROCADDR(GetDeviceProcAddr);
    GETPROCADDR(EnumerateDeviceLayerProperties);
    GETPROCADDR(EnumerateDeviceExtensionProperties);
    GETPROCADDR(CreateDevice);
    GETPROCADDR(DestroyDevice);
    GETPROCADDR(GetDeviceQueue);
    GETPROCADDR(CreateSwapchainKHR);
    GETPROCADDR(GetSwapchainImagesKHR);
    GETPROCADDR(QueuePresentKHR);
    GETPROCADDR(DestroySwapchainKHR);
    {
        scoped_lock l(globalLock);
        return device_dispatch[GetKey(device)].GetDeviceProcAddr(device, pName);
    }
}

VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkBasalt_GetInstanceProcAddr(VkInstance instance, const char *pName)
{
    // instance chain functions we intercept
    GETPROCADDR(GetInstanceProcAddr);
    GETPROCADDR(EnumerateInstanceLayerProperties);
    GETPROCADDR(EnumerateInstanceExtensionProperties);
    GETPROCADDR(CreateInstance);
    GETPROCADDR(DestroyInstance);

    // device chain functions we intercept
    GETPROCADDR(GetDeviceProcAddr);
    GETPROCADDR(EnumerateDeviceLayerProperties);
    GETPROCADDR(EnumerateDeviceExtensionProperties);
    GETPROCADDR(CreateDevice);
    GETPROCADDR(DestroyDevice);
    GETPROCADDR(GetDeviceQueue);
    GETPROCADDR(CreateSwapchainKHR);
    GETPROCADDR(GetSwapchainImagesKHR);
    GETPROCADDR(QueuePresentKHR);
    GETPROCADDR(DestroySwapchainKHR);
    {
        scoped_lock l(globalLock);
        return instance_dispatch[GetKey(instance)].GetInstanceProcAddr(instance, pName);
    }
}

}//extern "C"
