/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include "command.h"
#include "utils.h"
#include "gpu.h"

#ifdef PL_HAVE_VK_PROC_ADDR
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName);
#endif

const struct pl_vk_inst_params pl_vk_inst_default_params = {0};

struct vk_fun {
    const char *name;
    size_t offset;
    bool device_level;
};

struct vk_ext {
    const char *name;
    uint32_t core_ver;
    struct vk_fun *funs;
};

#define PL_VK_INST_FUN(N)                   \
    { .name = "vk" #N,                      \
      .offset = offsetof(struct vk_ctx, N), \
    }

#define PL_VK_DEV_FUN(N)                    \
    { .name = "vk" #N,                      \
      .offset = offsetof(struct vk_ctx, N), \
      .device_level = true,                 \
    }

// Table of optional vulkan instance extensions
static const char *vk_instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
};

// List of mandatory instance-level function pointers, including functions
// associated with mandatory instance extensions
static const struct vk_fun vk_inst_funs[] = {
    PL_VK_INST_FUN(CreateDevice),
    PL_VK_INST_FUN(EnumerateDeviceExtensionProperties),
    PL_VK_INST_FUN(GetDeviceProcAddr),
    PL_VK_INST_FUN(GetPhysicalDeviceExternalBufferProperties),
    PL_VK_INST_FUN(GetPhysicalDeviceExternalSemaphoreProperties),
    PL_VK_INST_FUN(GetPhysicalDeviceFeatures2KHR),
    PL_VK_INST_FUN(GetPhysicalDeviceFormatProperties),
    PL_VK_INST_FUN(GetPhysicalDeviceFormatProperties2KHR),
    PL_VK_INST_FUN(GetPhysicalDeviceImageFormatProperties2KHR),
    PL_VK_INST_FUN(GetPhysicalDeviceMemoryProperties),
    PL_VK_INST_FUN(GetPhysicalDeviceProperties),
    PL_VK_INST_FUN(GetPhysicalDeviceProperties2),
    PL_VK_INST_FUN(GetPhysicalDeviceQueueFamilyProperties),

    // These are not actually mandatory, but they're universal enough that we
    // just load them unconditionally (in lieu of not having proper support for
    // loading arbitrary instance extensions). Their use is generally guarded
    // behind various VkSurfaceKHR values already being provided by the API
    // user (implying this extension is loaded).
    PL_VK_INST_FUN(GetPhysicalDeviceSurfaceCapabilitiesKHR),
    PL_VK_INST_FUN(GetPhysicalDeviceSurfaceFormatsKHR),
    PL_VK_INST_FUN(GetPhysicalDeviceSurfacePresentModesKHR),
    PL_VK_INST_FUN(GetPhysicalDeviceSurfaceSupportKHR),
};

// Table of vulkan device extensions and functions they load, including
// functions exported by dependent instance-level extensions
static const struct vk_ext vk_device_extensions[] = {
    {
        .name = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(AcquireNextImageKHR),
            PL_VK_DEV_FUN(CreateSwapchainKHR),
            PL_VK_DEV_FUN(DestroySwapchainKHR),
            PL_VK_DEV_FUN(GetSwapchainImagesKHR),
            PL_VK_DEV_FUN(QueuePresentKHR),
            {0}
        },
    }, {
        .name = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(CmdPushDescriptorSetKHR),
            {0}
        },
    }, {
        .name = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetMemoryFdKHR),
            {0}
        },
    }, {
        .name = VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetMemoryFdPropertiesKHR),
            {0}
        },
#ifdef PL_HAVE_WIN32
    }, {
        .name = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetMemoryWin32HandleKHR),
            {0}
        },
#endif
    }, {
        .name = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetMemoryHostPointerPropertiesEXT),
            {0}
        },
    }, {
        .name = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetSemaphoreFdKHR),
            {0}
        },
#ifdef PL_HAVE_WIN32
    }, {
        .name = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetSemaphoreWin32HandleKHR),
            {0}
        },
#endif
    }, {
        .name = VK_EXT_PCI_BUS_INFO_EXTENSION_NAME,
    }, {
        .name = VK_EXT_HDR_METADATA_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(SetHdrMetadataEXT),
            {0}
        },
    }, {
        .name = VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
        .core_ver = VK_API_VERSION_1_2,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(ResetQueryPoolEXT),
            {0}
        },
    }, {
        .name = VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        .core_ver = VK_API_VERSION_1_2,
    }, {
        .name = VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(GetImageDrmFormatModifierPropertiesEXT),
            {0}
        },
    }, {
        .name = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
        .core_ver = VK_API_VERSION_1_2,
        .funs = (struct vk_fun[]) {
            PL_VK_DEV_FUN(WaitSemaphoresKHR),
            {0}
        },
#ifdef VK_KHR_portability_subset
    }, {
        .name = VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
#endif
    },
};

// Make sure to keep this in sync with the above!
const char * const pl_vulkan_recommended_extensions[] = {
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
    VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#ifdef PL_HAVE_WIN32
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#endif
    VK_EXT_PCI_BUS_INFO_EXTENSION_NAME,
    VK_EXT_HDR_METADATA_EXTENSION_NAME,
    VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME,
    VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
    VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
#ifdef VK_KHR_portability_subset
    VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
#endif
};

const int pl_vulkan_num_recommended_extensions =
    PL_ARRAY_SIZE(pl_vulkan_recommended_extensions);

// +1 because VK_KHR_swapchain is not automatically pulled in
static_assert(PL_ARRAY_SIZE(pl_vulkan_recommended_extensions) + 1 ==
              PL_ARRAY_SIZE(vk_device_extensions),
              "pl_vulkan_recommended_extensions out of sync with "
              "vk_device_extensions?");

// pNext chain of features we want enabled
static const VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphores = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
    .timelineSemaphore = true,
};

static const VkPhysicalDeviceHostQueryResetFeatures host_query_reset = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
    .pNext = (void *) &timeline_semaphores,
    .hostQueryReset = true,
};

const VkPhysicalDeviceFeatures2 pl_vulkan_recommended_features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    .pNext = (void *) &host_query_reset,
    .features = {
        .shaderImageGatherExtended = true,
        .shaderStorageImageReadWithoutFormat = true,
        .shaderStorageImageWriteWithoutFormat = true,

        // Needed for GPU-assisted validation, but not harmful to enable
        .fragmentStoresAndAtomics = true,
        .vertexPipelineStoresAndAtomics = true,
        .shaderInt64 = true,
    }
};

// List of mandatory device-level functions
//
// Note: Also includes VK_EXT_debug_utils functions, even though they aren't
// mandatory, simply because we load that extension in a special way.
static const struct vk_fun vk_dev_funs[] = {
    PL_VK_DEV_FUN(AllocateCommandBuffers),
    PL_VK_DEV_FUN(AllocateDescriptorSets),
    PL_VK_DEV_FUN(AllocateMemory),
    PL_VK_DEV_FUN(BeginCommandBuffer),
    PL_VK_DEV_FUN(BindBufferMemory),
    PL_VK_DEV_FUN(BindImageMemory),
    PL_VK_DEV_FUN(CmdBeginDebugUtilsLabelEXT),
    PL_VK_DEV_FUN(CmdBeginRenderPass),
    PL_VK_DEV_FUN(CmdBindDescriptorSets),
    PL_VK_DEV_FUN(CmdBindIndexBuffer),
    PL_VK_DEV_FUN(CmdBindPipeline),
    PL_VK_DEV_FUN(CmdBindVertexBuffers),
    PL_VK_DEV_FUN(CmdBlitImage),
    PL_VK_DEV_FUN(CmdClearColorImage),
    PL_VK_DEV_FUN(CmdCopyBuffer),
    PL_VK_DEV_FUN(CmdCopyBufferToImage),
    PL_VK_DEV_FUN(CmdCopyImage),
    PL_VK_DEV_FUN(CmdCopyImageToBuffer),
    PL_VK_DEV_FUN(CmdDispatch),
    PL_VK_DEV_FUN(CmdDraw),
    PL_VK_DEV_FUN(CmdDrawIndexed),
    PL_VK_DEV_FUN(CmdEndDebugUtilsLabelEXT),
    PL_VK_DEV_FUN(CmdEndRenderPass),
    PL_VK_DEV_FUN(CmdPipelineBarrier),
    PL_VK_DEV_FUN(CmdPushConstants),
    PL_VK_DEV_FUN(CmdResetQueryPool),
    PL_VK_DEV_FUN(CmdSetEvent),
    PL_VK_DEV_FUN(CmdSetScissor),
    PL_VK_DEV_FUN(CmdSetViewport),
    PL_VK_DEV_FUN(CmdUpdateBuffer),
    PL_VK_DEV_FUN(CmdWaitEvents),
    PL_VK_DEV_FUN(CmdWriteTimestamp),
    PL_VK_DEV_FUN(CreateBuffer),
    PL_VK_DEV_FUN(CreateBufferView),
    PL_VK_DEV_FUN(CreateCommandPool),
    PL_VK_DEV_FUN(CreateComputePipelines),
    PL_VK_DEV_FUN(CreateDescriptorPool),
    PL_VK_DEV_FUN(CreateDescriptorSetLayout),
    PL_VK_DEV_FUN(CreateEvent),
    PL_VK_DEV_FUN(CreateFence),
    PL_VK_DEV_FUN(CreateFramebuffer),
    PL_VK_DEV_FUN(CreateGraphicsPipelines),
    PL_VK_DEV_FUN(CreateImage),
    PL_VK_DEV_FUN(CreateImageView),
    PL_VK_DEV_FUN(CreatePipelineCache),
    PL_VK_DEV_FUN(CreatePipelineLayout),
    PL_VK_DEV_FUN(CreateQueryPool),
    PL_VK_DEV_FUN(CreateRenderPass),
    PL_VK_DEV_FUN(CreateSampler),
    PL_VK_DEV_FUN(CreateSemaphore),
    PL_VK_DEV_FUN(CreateShaderModule),
    PL_VK_DEV_FUN(DestroyBuffer),
    PL_VK_DEV_FUN(DestroyBufferView),
    PL_VK_DEV_FUN(DestroyCommandPool),
    PL_VK_DEV_FUN(DestroyDescriptorPool),
    PL_VK_DEV_FUN(DestroyDescriptorSetLayout),
    PL_VK_DEV_FUN(DestroyDevice),
    PL_VK_DEV_FUN(DestroyEvent),
    PL_VK_DEV_FUN(DestroyFence),
    PL_VK_DEV_FUN(DestroyFramebuffer),
    PL_VK_DEV_FUN(DestroyImage),
    PL_VK_DEV_FUN(DestroyImageView),
    PL_VK_DEV_FUN(DestroyInstance),
    PL_VK_DEV_FUN(DestroyPipeline),
    PL_VK_DEV_FUN(DestroyPipelineCache),
    PL_VK_DEV_FUN(DestroyPipelineLayout),
    PL_VK_DEV_FUN(DestroyQueryPool),
    PL_VK_DEV_FUN(DestroyRenderPass),
    PL_VK_DEV_FUN(DestroySampler),
    PL_VK_DEV_FUN(DestroySemaphore),
    PL_VK_DEV_FUN(DestroyShaderModule),
    PL_VK_DEV_FUN(EndCommandBuffer),
    PL_VK_DEV_FUN(FlushMappedMemoryRanges),
    PL_VK_DEV_FUN(FreeCommandBuffers),
    PL_VK_DEV_FUN(FreeMemory),
    PL_VK_DEV_FUN(GetBufferMemoryRequirements),
    PL_VK_DEV_FUN(GetDeviceQueue),
    PL_VK_DEV_FUN(GetImageMemoryRequirements2),
    PL_VK_DEV_FUN(GetImageSubresourceLayout),
    PL_VK_DEV_FUN(GetPipelineCacheData),
    PL_VK_DEV_FUN(GetQueryPoolResults),
    PL_VK_DEV_FUN(InvalidateMappedMemoryRanges),
    PL_VK_DEV_FUN(MapMemory),
    PL_VK_DEV_FUN(QueueSubmit),
    PL_VK_DEV_FUN(ResetEvent),
    PL_VK_DEV_FUN(ResetFences),
    PL_VK_DEV_FUN(SetDebugUtilsObjectNameEXT),
    PL_VK_DEV_FUN(UpdateDescriptorSets),
    PL_VK_DEV_FUN(WaitForFences),
};

static void load_vk_fun(struct vk_ctx *vk, const struct vk_fun *fun)
{
    PFN_vkVoidFunction *pfn = (void *) ((uintptr_t) vk + (ptrdiff_t) fun->offset);

    if (fun->device_level) {
        *pfn = vk->GetDeviceProcAddr(vk->dev, fun->name);
    } else {
        *pfn = vk->GetInstanceProcAddr(vk->inst, fun->name);
    };

    if (!*pfn) {
        // Some functions get their extension suffix stripped when promoted
        // to core. As a very simple work-around to this, try loading the
        // function a second time with the reserved suffixes stripped.
        static const char *ext_suffixes[] = { "KHR", "EXT" };
        pl_str fun_name = pl_str0(fun->name);
        char buf[64];

        for (int i = 0; i < PL_ARRAY_SIZE(ext_suffixes); i++) {
            if (!pl_str_eatend0(&fun_name, ext_suffixes[i]))
                continue;

            pl_assert(sizeof(buf) > fun_name.len);
            snprintf(buf, sizeof(buf), "%.*s", PL_STR_FMT(fun_name));
            if (fun->device_level) {
                *pfn = vk->GetDeviceProcAddr(vk->dev, buf);
            } else {
                *pfn = vk->GetInstanceProcAddr(vk->inst, buf);
            }
            return;
        }
    }
}

// Private struct for pl_vk_inst
struct priv {
    VkDebugUtilsMessengerEXT debug_utils_cb;
};

void pl_vk_inst_destroy(pl_vk_inst *inst_ptr)
{
    pl_vk_inst inst = *inst_ptr;
    if (!inst)
        return;

    struct priv *p = PL_PRIV(inst);
    if (p->debug_utils_cb) {
        PL_VK_LOAD_FUN(inst->instance, DestroyDebugUtilsMessengerEXT, inst->get_proc_addr);
        DestroyDebugUtilsMessengerEXT(inst->instance, p->debug_utils_cb, PL_VK_ALLOC);
    }

    PL_VK_LOAD_FUN(inst->instance, DestroyInstance, inst->get_proc_addr);
    DestroyInstance(inst->instance, PL_VK_ALLOC);
    pl_free_ptr((void **) inst_ptr);
}

static VkBool32 VKAPI_PTR vk_dbg_utils_cb(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                          VkDebugUtilsMessageTypeFlagsEXT msgType,
                                          const VkDebugUtilsMessengerCallbackDataEXT *data,
                                          void *priv)
{
    pl_log log = priv;

    // MSAN really doesn't like reading from the stack-allocated memory
    // allocated by the non-instrumented vulkan library, so just comment it out
    // when building with MSAN as a cheap hack-around.
#ifndef MSAN

    // Ignore errors for messages that we consider false positives
    switch (data->messageIdNumber) {
    case 0x7cd0911d: // VUID-VkSwapchainCreateInfoKHR-imageExtent-01274
    case 0x8928392f: // UNASSIGNED-BestPractices-NonSuccess-Result
    case 0xdc18ad6b: // UNASSIGNED-BestPractices-vkAllocateMemory-small-allocation
    case 0xb3d4346b: // UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation
        return false;

    // Work-around for validation layers bug
    case 0xc05b3a9d: // VUID-vkMapMemory-memory-00683
        return false;

    case 0x5f379b89: // UNASSIGNED-BestPractices-Error-Result
        if (strstr(data->pMessage, "VK_ERROR_FORMAT_NOT_SUPPORTED"))
            return false;
        break;
    }

    enum pl_log_level lev;
    switch (sev) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:     lev = PL_LOG_ERR;   break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:   lev = PL_LOG_WARN;  break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:      lev = PL_LOG_DEBUG; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:   lev = PL_LOG_TRACE; break;
    default:                                                lev = PL_LOG_INFO;  break;
    }

    pl_msg(log, lev, "vk %s", data->pMessage);

    for (int i = 0; i < data->queueLabelCount; i++)
        pl_msg(log, lev, "    during %s", data->pQueueLabels[i].pLabelName);
    for (int i = 0; i < data->cmdBufLabelCount; i++)
        pl_msg(log, lev, "    inside %s", data->pCmdBufLabels[i].pLabelName);
    for (int i = 0; i < data->objectCount; i++) {
        const VkDebugUtilsObjectNameInfoEXT *obj = &data->pObjects[i];
        pl_msg(log, lev, "    using %s: %s (0x%llx)",
               vk_obj_type(obj->objectType),
               obj->pObjectName ? obj->pObjectName : "anon",
               (unsigned long long) obj->objectHandle);
    }

    // The return value of this function determines whether the call will
    // be explicitly aborted (to prevent GPU errors) or not. In this case,
    // we generally want this to be on for the validation errors, but nothing
    // else (e.g. performance warnings)
    bool is_error = (sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) &&
                    (msgType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT);

    if (is_error) {
        pl_log_stack_trace(log, lev);
        pl_debug_abort();
        return true;
    }

#endif // MSAN

    return false;
}

static PFN_vkGetInstanceProcAddr get_proc_addr_fallback(pl_log log,
                                    PFN_vkGetInstanceProcAddr get_proc_addr)
{
    if (get_proc_addr)
        return get_proc_addr;

#ifdef PL_HAVE_VK_PROC_ADDR
    return vkGetInstanceProcAddr;
#else
    pl_fatal(log, "No `vkGetInstanceProcAddr` function provided, and "
             "libplacebo built without linking against this function!");
    return NULL;
#endif
}

#define PRINTF_VER(ver) \
    (int) VK_VERSION_MAJOR(ver), \
    (int) VK_VERSION_MINOR(ver), \
    (int) VK_VERSION_PATCH(ver)

pl_vk_inst pl_vk_inst_create(pl_log log, const struct pl_vk_inst_params *params)
{
    void *tmp = pl_tmp(NULL);
    params = PL_DEF(params, &pl_vk_inst_default_params);
    VkInstance inst = NULL;

    PL_ARRAY(const char *) exts = {0};

    PFN_vkGetInstanceProcAddr get_addr;
    if (!(get_addr = get_proc_addr_fallback(log, params->get_proc_addr)))
        goto error;

    // Query instance version support
    uint32_t api_ver = VK_API_VERSION_1_0;
    PL_VK_LOAD_FUN(NULL, EnumerateInstanceVersion, get_addr);
    if (EnumerateInstanceVersion && EnumerateInstanceVersion(&api_ver) != VK_SUCCESS)
        goto error;

    pl_debug(log, "Available instance version: %d.%d.%d", PRINTF_VER(api_ver));

    if (params->max_api_version) {
        api_ver = PL_MIN(api_ver, params->max_api_version);
        pl_info(log, "Restricting API version to %d.%d.%d... new version %d.%d.%d",
                PRINTF_VER(params->max_api_version), PRINTF_VER(api_ver));
    }

    if (api_ver < VK_API_VERSION_1_1) {
        pl_fatal(log, "Instance API version %d.%d.%d is lower than the minimum "
                 "required version of %d.%d.%d, cannot proceed!",
                 PRINTF_VER(api_ver), PRINTF_VER(VK_API_VERSION_1_1));
        goto error;
    }

    VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &(VkApplicationInfo) {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = api_ver,
        },
    };

#ifdef VK_EXT_validation_features
    // Try enabling as many validation features as possible. Ignored for
    // instances not supporting VK_EXT_validation_features.
    VkValidationFeatureEnableEXT validation_features[] = {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
# if VK_EXT_VALIDATION_FEATURES_SPEC_VERSION >= 2
        VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
# endif
    };

    VkValidationFeaturesEXT vinfo = {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pEnabledValidationFeatures = validation_features,
        .enabledValidationFeatureCount = PL_ARRAY_SIZE(validation_features),
    };

    if (params->debug_extra)
        info.pNext = &vinfo;
#else
    if (params->debug_extra) {
        pl_warn(log, "Enabled extra debugging but vulkan headers too old to "
                "support it, please update vulkan and recompile libplacebo!");
    }
#endif

    // Enumerate all supported layers
    PL_VK_LOAD_FUN(NULL, EnumerateInstanceLayerProperties, get_addr);
    uint32_t num_layers_avail = 0;
    EnumerateInstanceLayerProperties(&num_layers_avail, NULL);
    VkLayerProperties *layers_avail = pl_calloc_ptr(tmp, num_layers_avail, layers_avail);
    EnumerateInstanceLayerProperties(&num_layers_avail, layers_avail);

    pl_debug(log, "Available layers:");
    for (int i = 0; i < num_layers_avail; i++) {
        pl_debug(log, "    %s (v%d.%d.%d)", layers_avail[i].layerName,
                 PRINTF_VER(layers_avail[i].specVersion));
    }

    PL_ARRAY(const char *) layers = {0};

    // Sorted by priority
    static const char *debug_layers[] = {
        "VK_LAYER_KHRONOS_validation",
        "VK_LAYER_LUNARG_standard_validation",
    };

    // This layer has to be initialized first, otherwise all sorts of weirdness
    // happens (random segfaults, yum)
    bool debug = params->debug;
    if (debug) {
        for (int i = 0; i < PL_ARRAY_SIZE(debug_layers); i++) {
            for (int n = 0; n < num_layers_avail; n++) {
                if (strcmp(debug_layers[i], layers_avail[n].layerName) != 0)
                    continue;

                pl_info(log, "Enabling debug meta layer: %s", debug_layers[i]);
                PL_ARRAY_APPEND(tmp, layers, debug_layers[i]);
                goto debug_layers_done;
            }
        }

        // No layer found..
        pl_warn(log, "API debugging requested but no debug meta layers present... ignoring");
        debug = false;
    }

debug_layers_done: ;

    for (int i = 0; i < params->num_layers; i++)
        PL_ARRAY_APPEND(tmp, layers, params->layers[i]);

    for (int i = 0; i < params->num_opt_layers; i++) {
        const char *layer = params->opt_layers[i];
        for (int n = 0; n < num_layers_avail; n++) {
            if (strcmp(layer, layers_avail[n].layerName) == 0) {
                PL_ARRAY_APPEND(tmp, layers, layer);
                break;
            }
        }
    }

    // Enumerate all supported extensions
    PL_VK_LOAD_FUN(NULL, EnumerateInstanceExtensionProperties, get_addr);
    uint32_t num_exts_avail = 0;
    EnumerateInstanceExtensionProperties(NULL, &num_exts_avail, NULL);
    VkExtensionProperties *exts_avail = pl_calloc_ptr(tmp, num_exts_avail, exts_avail);
    EnumerateInstanceExtensionProperties(NULL, &num_exts_avail, exts_avail);

    struct {
        VkExtensionProperties *exts;
        uint32_t num_exts;
    } *layer_exts = pl_calloc_ptr(tmp, num_layers_avail, layer_exts);

    // Enumerate extensions from layers
    for (int i = 0; i < num_layers_avail; i++) {
        VkExtensionProperties **lexts = &layer_exts[i].exts;
        uint32_t *num = &layer_exts[i].num_exts;

        EnumerateInstanceExtensionProperties(layers_avail[i].layerName, num, NULL);
        *lexts = pl_calloc_ptr(tmp, *num, *lexts);
        EnumerateInstanceExtensionProperties(layers_avail[i].layerName, num, *lexts);

        // Replace all extensions that are already available globally by {0}
        for (int j = 0; j < *num; j++) {
            for (int k = 0; k < num_exts_avail; k++) {
                if (strcmp((*lexts)[j].extensionName, exts_avail[k].extensionName) == 0)
                    (*lexts)[j] = (VkExtensionProperties) {0};
            }
        }
    }

    pl_debug(log, "Available instance extensions:");
    for (int i = 0; i < num_exts_avail; i++)
        pl_debug(log, "    %s", exts_avail[i].extensionName);
    for (int i = 0; i < num_layers_avail; i++) {
        for (int j = 0; j < layer_exts[i].num_exts; j++) {
            if (!layer_exts[i].exts[j].extensionName[0])
                continue;

            pl_debug(log, "    %s (via %s)",
                     layer_exts[i].exts[j].extensionName,
                     layers_avail[i].layerName);
        }
    }

    // Add mandatory extensions
    PL_ARRAY_APPEND(tmp, exts, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    // Add optional extensions
    for (int i = 0; i < PL_ARRAY_SIZE(vk_instance_extensions); i++) {
        const char *ext = vk_instance_extensions[i];
        for (int n = 0; n < num_exts_avail; n++) {
            if (strcmp(ext, exts_avail[n].extensionName) == 0) {
                PL_ARRAY_APPEND(tmp, exts, ext);
                break;
            }
        }
    }

#ifdef VK_KHR_portability_enumeration
    // Required for macOS ( MoltenVK ) compatibility
    for (int n = 0; n < num_exts_avail; n++) {
        if (strcmp(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, exts_avail[n].extensionName) == 0) {
            PL_ARRAY_APPEND(tmp, exts, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            break;
        }
    }
#endif

    // Add extra user extensions
    for (int i = 0; i < params->num_extensions; i++) {
        const char *ext = params->extensions[i];
        PL_ARRAY_APPEND(tmp, exts, ext);

        // Enable any additional layers that are required for this extension
        for (int n = 0; n < num_layers_avail; n++) {
            for (int j = 0; j < layer_exts[n].num_exts; j++) {
                if (!layer_exts[n].exts[j].extensionName[0])
                    continue;
                if (strcmp(ext, layer_exts[n].exts[j].extensionName) == 0) {
                    PL_ARRAY_APPEND(tmp, layers, layers_avail[n].layerName);
                    goto next_user_ext;
                }
            }
        }

next_user_ext: ;
    }

    // Add extra optional user extensions
    for (int i = 0; i < params->num_opt_extensions; i++) {
        const char *ext = params->opt_extensions[i];
        for (int n = 0; n < num_exts_avail; n++) {
            if (strcmp(ext, exts_avail[n].extensionName) == 0) {
                PL_ARRAY_APPEND(tmp, exts, ext);
                goto next_opt_user_ext;
            }
        }

        for (int n = 0; n < num_layers_avail; n++) {
            for (int j = 0; j < layer_exts[n].num_exts; j++) {
                if (!layer_exts[n].exts[j].extensionName[0])
                    continue;
                if (strcmp(ext, layer_exts[n].exts[j].extensionName) == 0) {
                    PL_ARRAY_APPEND(tmp, exts, ext);
                    PL_ARRAY_APPEND(tmp, layers, layers_avail[n].layerName);
                    goto next_opt_user_ext;
                }
            }
        }

next_opt_user_ext: ;
    }

    // If debugging is enabled, load the necessary debug utils extension
    if (debug) {
        for (int n = 0; n < num_exts_avail; n++) {
            const char * const debug_ext = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            if (strcmp(debug_ext, exts_avail[n].extensionName) != 0)
                continue;

            pl_info(log, "Enabling debug report extension: %s", debug_ext);
            PL_ARRAY_APPEND(tmp, exts, debug_ext);
            goto debug_ext_done;
        }

        // No extension found
        pl_warn(log, "API debug layers enabled but no debug report extension "
                "found... ignoring. Debug messages may be spilling to "
                "stdout/stderr!");
        debug = false;
    }

debug_ext_done: ;

    info.ppEnabledExtensionNames = exts.elem;
    info.enabledExtensionCount = exts.num;
    info.ppEnabledLayerNames = layers.elem;
    info.enabledLayerCount = layers.num;

    pl_info(log, "Creating vulkan instance%s", exts.num ? " with extensions:" : "");
    for (int i = 0; i < exts.num; i++)
        pl_info(log, "    %s", exts.elem[i]);

    if (layers.num) {
        pl_info(log, "  and layers:");
        for (int i = 0; i < layers.num; i++)
            pl_info(log, "    %s", layers.elem[i]);
    }

    PL_VK_LOAD_FUN(NULL, CreateInstance, get_addr);
    VkResult res = CreateInstance(&info, PL_VK_ALLOC, &inst);
    if (res != VK_SUCCESS) {
        pl_fatal(log, "Failed creating instance: %s", vk_res_str(res));
        goto error;
    }

    struct pl_vk_inst *pl_vk = pl_zalloc_obj(NULL, pl_vk, struct priv);
    struct priv *p = PL_PRIV(pl_vk);
    *pl_vk = (struct pl_vk_inst) {
        .instance = inst,
        .api_version = api_ver,
        .get_proc_addr = get_addr,
        .extensions = pl_steal(pl_vk, exts.elem),
        .num_extensions = exts.num,
        .layers = pl_steal(pl_vk, layers.elem),
        .num_layers = layers.num,
    };

    // Set up a debug callback to catch validation messages
    if (debug) {
        VkDebugUtilsMessengerCreateInfoEXT dinfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vk_dbg_utils_cb,
            .pUserData = (void *) log,
        };

        PL_VK_LOAD_FUN(inst, CreateDebugUtilsMessengerEXT, get_addr);
        CreateDebugUtilsMessengerEXT(inst, &dinfo, PL_VK_ALLOC, &p->debug_utils_cb);
    }

    pl_free(tmp);
    return pl_vk;

error:
    pl_fatal(log, "Failed initializing vulkan instance");
    if (inst) {
        PL_VK_LOAD_FUN(inst, DestroyInstance, get_addr);
        DestroyInstance(inst, PL_VK_ALLOC);
    }
    pl_free(tmp);
    return NULL;
}

const struct pl_vulkan_params pl_vulkan_default_params = { PL_VULKAN_DEFAULTS };

void pl_vulkan_destroy(pl_vulkan *pl_vk)
{
    if (!*pl_vk)
        return;

    struct vk_ctx *vk = PL_PRIV(*pl_vk);
    pl_gpu_destroy((*pl_vk)->gpu);
    vk_malloc_destroy(&vk->ma);

    if (vk->dev) {
        PL_DEBUG(vk, "Waiting for remaining commands...");
        vk_wait_idle(vk);
        pl_assert(vk->cmds_pending.num == 0);
        for (int i = 0; i < vk->pools.num; i++)
            vk_cmdpool_destroy(vk, vk->pools.elem[i]);

        if (!vk->imported)
            vk->DestroyDevice(vk->dev, PL_VK_ALLOC);
    }

    pl_vk_inst_destroy(&vk->internal_instance);
    pl_mutex_destroy(&vk->lock);
    pl_free_ptr((void **) pl_vk);
}

static bool supports_surf(pl_log log, VkInstance inst,
                          PFN_vkGetInstanceProcAddr get_addr,
                          VkPhysicalDevice physd, VkSurfaceKHR surf)
{
    // Hack for the VK macro's logging to work
    struct { pl_log log; } *vk = (void *) &log;

    PL_VK_LOAD_FUN(inst, GetPhysicalDeviceQueueFamilyProperties, get_addr);
    PL_VK_LOAD_FUN(inst, GetPhysicalDeviceSurfaceSupportKHR, get_addr);
    uint32_t qfnum = 0;
    GetPhysicalDeviceQueueFamilyProperties(physd, &qfnum, NULL);

    for (int i = 0; i < qfnum; i++) {
        VkBool32 sup = false;
        VK(GetPhysicalDeviceSurfaceSupportKHR(physd, i, surf, &sup));
        if (sup)
            return true;
    }

error:
    return false;
}

VkPhysicalDevice pl_vulkan_choose_device(pl_log log,
                                const struct pl_vulkan_device_params *params)
{
    // Hack for the VK macro's logging to work
    struct { pl_log log; } *vk = (void *) &log;
    PL_INFO(vk, "Probing for vulkan devices:");

    pl_assert(params->instance);
    VkInstance inst = params->instance;
    VkPhysicalDevice dev = VK_NULL_HANDLE;

    PFN_vkGetInstanceProcAddr get_addr;
    if (!(get_addr = get_proc_addr_fallback(log, params->get_proc_addr)))
        return NULL;

    PL_VK_LOAD_FUN(inst, EnumeratePhysicalDevices, get_addr);
    PL_VK_LOAD_FUN(inst, GetPhysicalDeviceProperties2, get_addr);
    pl_assert(GetPhysicalDeviceProperties2);

    VkPhysicalDevice *devices = NULL;
    uint32_t num = 0;
    VK(EnumeratePhysicalDevices(inst, &num, NULL));
    devices = pl_calloc_ptr(NULL, num, devices);
    VK(EnumeratePhysicalDevices(inst, &num, devices));

    static const struct { const char *name; int priority; } types[] = {
        [VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]   = {"discrete",   5},
        [VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU] = {"integrated", 4},
        [VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]    = {"virtual",    3},
        [VK_PHYSICAL_DEVICE_TYPE_CPU]            = {"software",   2},
        [VK_PHYSICAL_DEVICE_TYPE_OTHER]          = {"other",      1},
    };

    static const uint8_t nil[VK_UUID_SIZE] = {0};
    bool uuid_set = memcmp(params->device_uuid, nil, VK_UUID_SIZE) != 0;

    int best = -1;
    for (int i = 0; i < num; i++) {
        VkPhysicalDeviceIDPropertiesKHR id_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR,
        };

        VkPhysicalDeviceProperties2 prop = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
            .pNext = &id_props,
        };

        GetPhysicalDeviceProperties2(devices[i], &prop);
        VkPhysicalDeviceType t = prop.properties.deviceType;
        const char *dtype = t < PL_ARRAY_SIZE(types) ? types[t].name : "unknown?";
        PL_INFO(vk, "    GPU %d: %s (%s)", i, prop.properties.deviceName, dtype);
        PL_INFO(vk, "           uuid: %s", PRINT_UUID(id_props.deviceUUID));

        if (params->surface) {
            if (!supports_surf(log, inst, get_addr, devices[i], params->surface)) {
                PL_DEBUG(vk, "      -> excluding due to lack of surface support");
                continue;
            }
        }

        if (uuid_set) {
            if (memcmp(id_props.deviceUUID, params->device_uuid, VK_UUID_SIZE) == 0) {
                dev = devices[i];
                continue;
            } else {
                PL_DEBUG(vk, "     -> excluding due to UUID mismatch");
                continue;
            }
        } else if (params->device_name && params->device_name[0] != '\0') {
            if (strcmp(params->device_name, prop.properties.deviceName) == 0) {
                dev = devices[i];
                continue;
            } else {
                PL_DEBUG(vk, "      -> excluding due to name mismatch");
                continue;
            }
        }

        if (!params->allow_software && t == VK_PHYSICAL_DEVICE_TYPE_CPU) {
            PL_DEBUG(vk, "      -> excluding due to params->allow_software");
            continue;
        }

        if (prop.properties.apiVersion < VK_API_VERSION_1_1) {
            PL_DEBUG(vk, "      -> excluding due to too low API version");
            continue;
        }

        int priority = t < PL_ARRAY_SIZE(types) ? types[t].priority : 0;
        if (priority > best) {
            dev = devices[i];
            best = priority;
        }
    }

error:
    pl_free(devices);
    return dev;
}

// Find the most specialized queue supported a combination of flags. In cases
// where there are multiple queue families at the same specialization level,
// this finds the one with the most queues. Returns -1 if no queue was found.
static int find_qf(VkQueueFamilyProperties *qfs, int qfnum, VkQueueFlags flags)
{
    int idx = -1;
    for (int i = 0; i < qfnum; i++) {
        if (!(qfs[i].queueFlags & flags))
            continue;

        // QF is more specialized. Since we don't care about other bits like
        // SPARSE_BIT, mask the ones we're interestew in
        const VkQueueFlags mask = VK_QUEUE_GRAPHICS_BIT |
                                  VK_QUEUE_TRANSFER_BIT |
                                  VK_QUEUE_COMPUTE_BIT;

        if (idx < 0 || (qfs[i].queueFlags & mask) < (qfs[idx].queueFlags & mask))
            idx = i;

        // QF has more queues (at the same specialization level)
        if (qfs[i].queueFlags == qfs[idx].queueFlags &&
            qfs[i].queueCount > qfs[idx].queueCount)
            idx = i;
    }

    return idx;
}

typedef PL_ARRAY(VkDeviceQueueCreateInfo) qinfo_arr_t;

static void add_qinfo(void *alloc, qinfo_arr_t *qinfos,
                      VkQueueFamilyProperties *qfs, int idx, int qcount)
{
    if (idx < 0)
        return;

    // Check to see if we've already added this queue family
    for (int i = 0; i < qinfos->num; i++) {
        if (qinfos->elem[i].queueFamilyIndex == idx)
            return;
    }

    if (!qcount)
        qcount = qfs[idx].queueCount;

    float *priorities = pl_calloc_ptr(alloc, qcount, priorities);
    VkDeviceQueueCreateInfo qinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = idx,
        .queueCount = PL_MIN(qcount, qfs[idx].queueCount),
        .pQueuePriorities = priorities,
    };

    PL_ARRAY_APPEND(alloc, *qinfos, qinfo);
}

static bool device_init(struct vk_ctx *vk, const struct pl_vulkan_params *params)
{
    pl_assert(vk->physd);
    void *tmp = pl_tmp(NULL);

    // Enumerate the queue families and find suitable families for each task
    uint32_t qfnum = 0;
    vk->GetPhysicalDeviceQueueFamilyProperties(vk->physd, &qfnum, NULL);
    VkQueueFamilyProperties *qfs = pl_calloc_ptr(tmp, qfnum, qfs);
    vk->GetPhysicalDeviceQueueFamilyProperties(vk->physd, &qfnum, qfs);

    PL_DEBUG(vk, "Queue families supported by device:");

    for (int i = 0; i < qfnum; i++) {
        PL_DEBUG(vk, "    %d: flags 0x%x num %d", i,
                 (unsigned) qfs[i].queueFlags, (int) qfs[i].queueCount);
    }

    VkQueueFlagBits gfx_flags = VK_QUEUE_GRAPHICS_BIT;
    if (!params->async_compute)
        gfx_flags |= VK_QUEUE_COMPUTE_BIT;

    int idx_gfx  = find_qf(qfs, qfnum, gfx_flags);
    int idx_comp = find_qf(qfs, qfnum, VK_QUEUE_COMPUTE_BIT);
    int idx_tf   = find_qf(qfs, qfnum, VK_QUEUE_TRANSFER_BIT);
    if (idx_tf < 0)
        idx_tf = idx_comp;

    if (!params->async_compute)
        idx_comp = idx_gfx;
    if (!params->async_transfer)
        idx_tf = idx_gfx;

    PL_DEBUG(vk, "Using graphics queue %d", idx_gfx);
    if (idx_tf != idx_gfx)
        PL_INFO(vk, "Using async transfer (queue %d)", idx_tf);
    if (idx_comp != idx_gfx)
        PL_INFO(vk, "Using async compute (queue %d)", idx_comp);

    // Vulkan requires at least one GRAPHICS+COMPUTE queue, so if this fails
    // something is horribly wrong.
    pl_assert(idx_gfx >= 0 && idx_comp >= 0 && idx_tf >= 0);

    // If needed, ensure we can actually present to the surface using this queue
    if (params->surface) {
        VkBool32 sup = false;
        VK(vk->GetPhysicalDeviceSurfaceSupportKHR(vk->physd, idx_gfx,
                                                  params->surface, &sup));
        if (!sup) {
            PL_FATAL(vk, "Queue family does not support surface presentation!");
            goto error;
        }
    }

    // Now that we know which QFs we want, we can create the logical device
    qinfo_arr_t qinfos = {0};
    add_qinfo(tmp, &qinfos, qfs, idx_gfx, params->queue_count);
    add_qinfo(tmp, &qinfos, qfs, idx_comp, params->queue_count);
    add_qinfo(tmp, &qinfos, qfs, idx_tf, params->queue_count);

    // Enumerate all supported extensions
    uint32_t num_exts_avail = 0;
    VK(vk->EnumerateDeviceExtensionProperties(vk->physd, NULL, &num_exts_avail, NULL));
    VkExtensionProperties *exts_avail = pl_calloc_ptr(tmp, num_exts_avail, exts_avail);
    VK(vk->EnumerateDeviceExtensionProperties(vk->physd, NULL, &num_exts_avail, exts_avail));

    PL_DEBUG(vk, "Available device extensions:");
    for (int i = 0; i < num_exts_avail; i++)
        PL_DEBUG(vk, "    %s", exts_avail[i].extensionName);

    // Add all extensions we need
    if (params->surface)
        PL_ARRAY_APPEND(vk->alloc, vk->exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (vk->api_ver < VK_API_VERSION_1_2)
        PL_ARRAY_APPEND(vk->alloc, vk->exts, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    // Keep track of all optional function pointers associated with extensions
    PL_ARRAY(const struct vk_fun *) ext_funs = {0};

    // Add all optional device-level extensions extensions
    for (int i = 0; i < PL_ARRAY_SIZE(vk_device_extensions); i++) {
        const struct vk_ext *ext = &vk_device_extensions[i];
        if (ext->core_ver && vk->api_ver >= ext->core_ver) {
            // Layer is already implicitly enabled by the API version
            for (const struct vk_fun *f = ext->funs; f && f->name; f++)
                PL_ARRAY_APPEND(tmp, ext_funs,  f);
            continue;
        }

        for (int n = 0; n < num_exts_avail; n++) {
            if (strcmp(ext->name, exts_avail[n].extensionName) == 0) {
                PL_ARRAY_APPEND(vk->alloc, vk->exts, ext->name);
                for (const struct vk_fun *f = ext->funs; f && f->name; f++)
                    PL_ARRAY_APPEND(tmp, ext_funs, f);
                break;
            }
        }
    }

    // Add extra user extensions
    for (int i = 0; i < params->num_extensions; i++)
        PL_ARRAY_APPEND(vk->alloc, vk->exts, params->extensions[i]);

    // Add optional extra user extensions
    for (int i = 0; i < params->num_opt_extensions; i++) {
        const char *ext = params->opt_extensions[i];
        for (int n = 0; n < num_exts_avail; n++) {
            if (strcmp(ext, exts_avail[n].extensionName) == 0) {
                PL_ARRAY_APPEND(vk->alloc, vk->exts, ext);
                break;
            }
        }
    }

    // Query all supported device features by constructing a pNext chain
    // starting with the features we care about and ending with whatever
    // features were requested by the user
    vk->features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;
    for (const VkBaseInStructure *in = pl_vulkan_recommended_features.pNext;
            in; in = in->pNext)
        vk_link_struct(&vk->features, vk_struct_memdup(vk->alloc, in));

    for (const VkBaseInStructure *in = (const VkBaseInStructure *) params->features;
            in; in = in->pNext)
    {
        if (vk_find_struct(&vk->features, in->sType))
            continue; // skip structs already present

        void *copy = vk_struct_memdup(vk->alloc, in);
        if (!copy) {
            PL_ERR(vk, "Unknown struct type %"PRIu64"?", (uint64_t) in->sType);
            continue;
        }

        vk_link_struct(&vk->features, copy);
    }

    vk->GetPhysicalDeviceFeatures2KHR(vk->physd, &vk->features);

    const VkPhysicalDeviceTimelineSemaphoreFeatures *timeline_sem;
    timeline_sem = vk_find_struct(&vk->features,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    if (!timeline_sem || !timeline_sem->timelineSemaphore) {
        PL_ERR(vk, "Selected vulkan device does not support timeline semaphores!");
        goto error;
    }

    // Go through the features chain a second time and mask every option
    // that wasn't whitelisted by either libplacebo or the user
    for (VkBaseOutStructure *chain = (VkBaseOutStructure *) &vk->features;
            chain; chain = chain->pNext)
    {
        const VkBaseInStructure *in_a, *in_b;
        in_a = vk_find_struct(&pl_vulkan_recommended_features, chain->sType);
        in_b = vk_find_struct(params->features, chain->sType);
        in_a = PL_DEF(in_a, in_b);
        in_b = PL_DEF(in_b, in_a);
        pl_assert(in_a && in_b);

        VkBool32 *req = (VkBool32 *) &chain[1];
        const VkBool32 *wl_a = (const VkBool32 *) &in_a[1];
        const VkBool32 *wl_b = (const VkBool32 *) &in_b[1];
        size_t size = vk_struct_size(chain->sType) - sizeof(chain[0]);
        for (int i = 0; i < size / sizeof(VkBool32); i++)
            req[i] &= wl_a[i] || wl_b[i];
    }

    VkDeviceCreateInfo dinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vk->features,
        .pQueueCreateInfos = qinfos.elem,
        .queueCreateInfoCount = qinfos.num,
        .ppEnabledExtensionNames = vk->exts.elem,
        .enabledExtensionCount = vk->exts.num,
    };

    PL_INFO(vk, "Creating vulkan device%s", vk->exts.num ? " with extensions:" : "");
    for (int i = 0; i < vk->exts.num; i++)
        PL_INFO(vk, "    %s", vk->exts.elem[i]);

    VK(vk->CreateDevice(vk->physd, &dinfo, PL_VK_ALLOC, &vk->dev));

    // Load all mandatory device-level functions
    for (int i = 0; i < PL_ARRAY_SIZE(vk_dev_funs); i++)
        load_vk_fun(vk, &vk_dev_funs[i]);

    // Load all of the optional functions from the extensions we enabled
    for (int i = 0; i < ext_funs.num; i++)
        load_vk_fun(vk, ext_funs.elem[i]);

    // Create the command pools
    for (int i = 0; i < qinfos.num; i++) {
        int qf = qinfos.elem[i].queueFamilyIndex;
        struct vk_cmdpool *pool = vk_cmdpool_create(vk, qinfos.elem[i], qfs[qf]);
        if (!pool)
            goto error;
        PL_ARRAY_APPEND(vk->alloc, vk->pools, pool);

        // Update the pool_* pointers based on the corresponding index
        const char *qf_name = NULL;
        if (qf == idx_tf) {
            vk->pool_transfer = pool;
            qf_name = "transfer";
        }
        if (qf == idx_comp) {
            vk->pool_compute = pool;
            qf_name = "compute";
        }
        if (qf == idx_gfx) {
            vk->pool_graphics = pool;
            qf_name = "graphics";
        }

        for (int n = 0; n < pool->num_queues; n++)
            PL_VK_NAME(QUEUE, pool->queues[n], qf_name);
    }

    pl_free(tmp);
    return true;

error:
    PL_FATAL(vk, "Failed creating logical device!");
    pl_free(tmp);
    vk->failed = true;
    return false;
}

static void lock_queue_internal(void *priv, int qf, int qidx)
{
    struct vk_ctx *vk = priv;
    pl_mutex_lock(&vk->queue_locks.elem[qf][qidx]);
}

static void unlock_queue_internal(void *priv, int qf, int qidx)
{
    struct vk_ctx *vk = priv;
    pl_mutex_unlock(&vk->queue_locks.elem[qf][qidx]);
}

static void lock_queue(pl_vulkan pl_vk, int qf, int qidx)
{
    struct vk_ctx *vk = PL_PRIV(pl_vk);
    vk->lock_queue(vk->queue_ctx, qf, qidx);
}

static void unlock_queue(pl_vulkan pl_vk, int qf, int qidx)
{
    struct vk_ctx *vk = PL_PRIV(pl_vk);
    vk->unlock_queue(vk->queue_ctx, qf, qidx);
}

static bool finalize_context(struct pl_vulkan *pl_vk, int max_glsl_version)
{
    struct vk_ctx *vk = PL_PRIV(pl_vk);

    pl_assert(vk->pool_graphics);
    pl_assert(vk->pool_compute);
    pl_assert(vk->pool_transfer);

    vk->ma = vk_malloc_create(vk);
    if (!vk->ma)
        return false;

    pl_vk->gpu = pl_gpu_create_vk(vk);
    if (!pl_vk->gpu)
        return false;

    // Blacklist / restrict features
    if (max_glsl_version) {
        struct pl_glsl_version *glsl = (struct pl_glsl_version *) &pl_vk->gpu->glsl;
        glsl->version = PL_MIN(glsl->version, max_glsl_version);
        glsl->version = PL_MAX(glsl->version, 140); // required for GL_KHR_vulkan_glsl
        PL_INFO(vk, "Restricting GLSL version to %d... new version is %d",
                max_glsl_version, glsl->version);
    }

    // Expose the resulting vulkan objects
    pl_vk->instance = vk->inst;
    pl_vk->phys_device = vk->physd;
    pl_vk->device = vk->dev;
    pl_vk->api_version = vk->api_ver;
    pl_vk->extensions = vk->exts.elem;
    pl_vk->num_extensions = vk->exts.num;
    pl_vk->features = &vk->features;
    pl_vk->num_queues = vk->pools.num;
    pl_vk->queues = pl_calloc_ptr(vk->alloc, vk->pools.num, pl_vk->queues);
    pl_vk->lock_queue = lock_queue;
    pl_vk->unlock_queue = unlock_queue;

    uint32_t max_qf = 0;
    for (int i = 0; i < vk->pools.num; i++) {
        struct pl_vulkan_queue *queues = (struct pl_vulkan_queue *) pl_vk->queues;
        queues[i] = (struct pl_vulkan_queue) {
            .index = vk->pools.elem[i]->qf,
            .count = vk->pools.elem[i]->num_queues,
        };

        if (vk->pools.elem[i] == vk->pool_graphics)
            pl_vk->queue_graphics = queues[i];
        if (vk->pools.elem[i] == vk->pool_compute)
            pl_vk->queue_compute = queues[i];
        if (vk->pools.elem[i] == vk->pool_transfer)
            pl_vk->queue_transfer = queues[i];

        max_qf = PL_MAX(max_qf, queues[i].index);
    }

    if (!vk->lock_queue) {
        vk->queue_locks.num = max_qf + 1;
        PL_ARRAY_RESIZE(vk->alloc, vk->queue_locks, vk->queue_locks.num);

        for (int i = 0; i < vk->pools.num; i++) {
            struct vk_cmdpool *pool = vk->pools.elem[i];
            pl_mutex **locks = vk->queue_locks.elem;
            locks[pool->qf] = pl_alloc(vk->alloc, pool->num_queues * sizeof(pl_mutex));
            for (int n = 0; n < pool->num_queues; n++)
                pl_mutex_init(&locks[pool->qf][n]);
        }

        vk->lock_queue = lock_queue_internal;
        vk->unlock_queue = unlock_queue_internal;
        vk->queue_ctx = vk;
    }

    pl_assert(vk->lock_queue);
    pl_assert(vk->unlock_queue);
    return true;
}

pl_vulkan pl_vulkan_create(pl_log log, const struct pl_vulkan_params *params)
{
    params = PL_DEF(params, &pl_vulkan_default_params);
    struct pl_vulkan *pl_vk = pl_zalloc_obj(NULL, pl_vk, struct vk_ctx);
    struct vk_ctx *vk = PL_PRIV(pl_vk);
    *vk = (struct vk_ctx) {
        .vulkan = pl_vk,
        .alloc = pl_vk,
        .log = log,
        .inst = params->instance,
        .GetInstanceProcAddr = get_proc_addr_fallback(log, params->get_proc_addr),
    };

    pl_mutex_init_type(&vk->lock, PL_MUTEX_RECURSIVE);
    if (!vk->GetInstanceProcAddr)
        goto error;

    if (!vk->inst) {
        pl_assert(!params->surface);
        pl_assert(!params->device);
        PL_DEBUG(vk, "No VkInstance provided, creating one...");

        // Mirror the instance params here to set `get_proc_addr` correctly
        struct pl_vk_inst_params iparams;
        iparams = *PL_DEF(params->instance_params, &pl_vk_inst_default_params);
        iparams.get_proc_addr = params->get_proc_addr;
        vk->internal_instance = pl_vk_inst_create(log, &iparams);
        if (!vk->internal_instance)
            goto error;
        vk->inst = vk->internal_instance->instance;
    }

    // Directly load all mandatory instance-level function pointers, since
    // these will be required for all further device creation logic
    for (int i = 0; i < PL_ARRAY_SIZE(vk_inst_funs); i++)
        load_vk_fun(vk, &vk_inst_funs[i]);

    // Choose the physical device
    if (params->device) {
        PL_DEBUG(vk, "Using specified VkPhysicalDevice");
        vk->physd = params->device;
    } else {
        struct pl_vulkan_device_params dparams = {
            .instance       = vk->inst,
            .get_proc_addr  = params->get_proc_addr,
            .surface        = params->surface,
            .device_name    = params->device_name,
            .allow_software = params->allow_software,
        };
        memcpy(dparams.device_uuid, params->device_uuid, VK_UUID_SIZE);

        vk->physd = pl_vulkan_choose_device(log, &dparams);
        if (!vk->physd) {
            PL_FATAL(vk, "Found no suitable device, giving up.");
            goto error;
        }
    }

    VkPhysicalDeviceIDPropertiesKHR id_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR,
    };

    VkPhysicalDeviceProperties2KHR prop = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
        .pNext = &id_props,
    };

    vk->GetPhysicalDeviceProperties2(vk->physd, &prop);
    vk->limits = prop.properties.limits;

    PL_INFO(vk, "Vulkan device properties:");
    PL_INFO(vk, "    Device Name: %s", prop.properties.deviceName);
    PL_INFO(vk, "    Device ID: %x:%x", (unsigned) prop.properties.vendorID,
            (unsigned) prop.properties.deviceID);
    PL_INFO(vk, "    Device UUID: %s", PRINT_UUID(id_props.deviceUUID));
    PL_INFO(vk, "    Driver version: %d", (int) prop.properties.driverVersion);
    PL_INFO(vk, "    API version: %d.%d.%d", PRINTF_VER(prop.properties.apiVersion));

    // Needed by device_init
    vk->api_ver = prop.properties.apiVersion;
    if (params->max_api_version) {
        vk->api_ver = PL_MIN(vk->api_ver, params->max_api_version);
        PL_INFO(vk, "Restricting API version to %d.%d.%d... new version %d.%d.%d",
                PRINTF_VER(params->max_api_version), PRINTF_VER(vk->api_ver));
    }

    if (vk->api_ver < VK_API_VERSION_1_1) {
        PL_FATAL(vk, "Device API version %d.%d.%d is lower than the minimum "
                 "required version of %d.%d.%d, cannot proceed!",
                 PRINTF_VER(vk->api_ver), PRINTF_VER(VK_API_VERSION_1_1));
        goto error;
    }

    // Finally, initialize the logical device and the rest of the vk_ctx
    if (!device_init(vk, params))
        goto error;

    if (!finalize_context(pl_vk, params->max_glsl_version))
        goto error;

    return pl_vk;

error:
    PL_FATAL(vk, "Failed initializing vulkan device");
    pl_vulkan_destroy((pl_vulkan *) &pl_vk);
    return NULL;
}

pl_vulkan pl_vulkan_import(pl_log log, const struct pl_vulkan_import_params *params)
{
    void *tmp = pl_tmp(NULL);

    struct pl_vulkan *pl_vk = pl_zalloc_obj(NULL, pl_vk, struct vk_ctx);
    struct vk_ctx *vk = PL_PRIV(pl_vk);
    *vk = (struct vk_ctx) {
        .vulkan = pl_vk,
        .alloc = pl_vk,
        .log = log,
        .imported = true,
        .inst = params->instance,
        .physd = params->phys_device,
        .dev = params->device,
        .GetInstanceProcAddr = get_proc_addr_fallback(log, params->get_proc_addr),
        .lock_queue = params->lock_queue,
        .unlock_queue = params->unlock_queue,
        .queue_ctx = params->queue_ctx,
    };

    pl_mutex_init_type(&vk->lock, PL_MUTEX_RECURSIVE);
    if (!vk->GetInstanceProcAddr)
        goto error;

    for (int i = 0; i < PL_ARRAY_SIZE(vk_inst_funs); i++)
        load_vk_fun(vk, &vk_inst_funs[i]);

    VkPhysicalDeviceIDPropertiesKHR id_props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR,
    };

    VkPhysicalDeviceProperties2KHR prop = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
        .pNext = &id_props,
    };

    pl_assert(vk->GetPhysicalDeviceProperties2);
    vk->GetPhysicalDeviceProperties2(vk->physd, &prop);
    vk->limits = prop.properties.limits;

    PL_INFO(vk, "Imported vulkan device properties:");
    PL_INFO(vk, "    Device Name: %s", prop.properties.deviceName);
    PL_INFO(vk, "    Device ID: %x:%x", (unsigned) prop.properties.vendorID,
            (unsigned) prop.properties.deviceID);
    PL_INFO(vk, "    Device UUID: %s", PRINT_UUID(id_props.deviceUUID));
    PL_INFO(vk, "    Driver version: %d", (int) prop.properties.driverVersion);
    PL_INFO(vk, "    API version: %d.%d.%d", PRINTF_VER(prop.properties.apiVersion));

    vk->api_ver = prop.properties.apiVersion;
    if (params->max_api_version) {
        vk->api_ver = PL_MIN(vk->api_ver, params->max_api_version);
        PL_INFO(vk, "Restricting API version to %d.%d.%d... new version %d.%d.%d",
                PRINTF_VER(params->max_api_version), PRINTF_VER(vk->api_ver));
    }

    if (vk->api_ver < VK_API_VERSION_1_1) {
        PL_FATAL(vk, "Device API version %d.%d.%d is lower than the minimum "
                 "required version of %d.%d.%d, cannot proceed!",
                 PRINTF_VER(vk->api_ver), PRINTF_VER(VK_API_VERSION_1_1));
        goto error;
    }

    VkPhysicalDeviceFeatures2 *features;
    features = vk_chain_memdup(vk->alloc, params->features);
    if (features) {
        // Go through and replace all meta-features structs by their individual
        // extension variants, since that's what we check for in our code
        const VkPhysicalDeviceVulkan12Features *vk12 = vk_find_struct(features,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);

        if (vk12 && vk12->hostQueryReset) {
            const VkPhysicalDeviceHostQueryResetFeatures hqr = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
                .hostQueryReset = true,
            };
            vk_link_struct(features, vk_struct_memdup(vk->alloc, &hqr));
        }

        if (vk12 && vk12->timelineSemaphore) {
            const VkPhysicalDeviceTimelineSemaphoreFeatures ts = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
                .timelineSemaphore = true,
            };
            vk_link_struct(features, vk_struct_memdup(vk->alloc, &ts));
        }

        vk->features = *features;
    }

    const VkPhysicalDeviceTimelineSemaphoreFeatures *timeline_sem;
    timeline_sem = vk_find_struct(&vk->features,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    if (!timeline_sem || !timeline_sem->timelineSemaphore) {
        PL_ERR(vk, "Imported Vulkan device does not support timeline "
               "semaphores. Please enable this device feature.");
        goto error;
    }

    // Load all mandatory device-level functions
    for (int i = 0; i < PL_ARRAY_SIZE(vk_dev_funs); i++)
        load_vk_fun(vk, &vk_dev_funs[i]);

    // Load all of the optional functions from the extensions enabled
    for (int i = 0; i < PL_ARRAY_SIZE(vk_device_extensions); i++) {
        const struct vk_ext *ext = &vk_device_extensions[i];
        for (int n = 0; n < params->num_extensions; n++) {
            if (strcmp(ext->name, params->extensions[n]) == 0 ||
                (ext->core_ver && ext->core_ver >= vk->api_ver))
            {
                // Extension is available, directly load it
                for (const struct vk_fun *f = ext->funs; f && f->name; f++)
                    load_vk_fun(vk, f);
                break;
            }
        }
    }

    uint32_t qfnum = 0;
    vk->GetPhysicalDeviceQueueFamilyProperties(vk->physd, &qfnum, NULL);
    VkQueueFamilyProperties *qfs = pl_calloc_ptr(tmp, qfnum, qfs);
    vk->GetPhysicalDeviceQueueFamilyProperties(vk->physd, &qfnum, qfs);

    // Create the command pools for each unique qf that exists
    struct {
        const struct pl_vulkan_queue *info;
        struct vk_cmdpool **pool;
        VkQueueFlagBits flags; // *any* of these flags provide the cap
    } qinfos[] = {
        {
            .info = &params->queue_graphics,
            .pool = &vk->pool_graphics,
            .flags = VK_QUEUE_GRAPHICS_BIT,
        }, {
            .info = &params->queue_compute,
            .pool = &vk->pool_compute,
            .flags = VK_QUEUE_COMPUTE_BIT,
        }, {
            .info = &params->queue_transfer,
            .pool = &vk->pool_transfer,
            .flags = VK_QUEUE_TRANSFER_BIT |
                     VK_QUEUE_GRAPHICS_BIT |
                     VK_QUEUE_COMPUTE_BIT,
        }
    };

    for (int i = 0; i < PL_ARRAY_SIZE(qinfos); i++) {
        int qf = qinfos[i].info->index;
        struct vk_cmdpool **pool = qinfos[i].pool;
        if (!qinfos[i].info->count)
            continue;

        // API sanity check
        pl_assert(qfs[qf].queueFlags & qinfos[i].flags);

        // See if we already created a pool for this queue family
        for (int j = 0; j < i; j++) {
            if (qinfos[j].info->count && qinfos[j].info->index == qf) {
                *pool = *qinfos[j].pool;
                goto next_qf;
            }
        }

        struct VkDeviceQueueCreateInfo qinfo = {
            .queueFamilyIndex = qf,
            .queueCount = qinfos[i].info->count,
        };

        *pool = vk_cmdpool_create(vk, qinfo, qfs[qf]);
        if (!*pool)
            goto error;
        PL_ARRAY_APPEND(vk->alloc, vk->pools, *pool);

        // Pre-emptively set "lower priority" pools as well
        for (int j = i+1; j < PL_ARRAY_SIZE(qinfos); j++) {
            if (qfs[qf].queueFlags & qinfos[j].flags)
                *qinfos[j].pool = *pool;
        }

next_qf: ;
    }

    if (!vk->pool_graphics) {
        PL_ERR(vk, "No valid queues provided?");
        goto error;
    }

    if (!finalize_context(pl_vk, params->max_glsl_version))
        goto error;

    pl_free(tmp);
    return pl_vk;

error:
    PL_FATAL(vk, "Failed importing vulkan device");
    pl_vulkan_destroy((pl_vulkan *) &pl_vk);
    pl_free(tmp);
    return NULL;
}
