#ifndef __QF_Vulkan_render_h
#define __QF_Vulkan_render_h

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include "QF/cexpr.h"
#include "QF/simd/types.h"
#ifndef __QFCC__
#include "QF/Vulkan/command.h"
#endif

typedef struct qfv_output_s {
	VkExtent2D  extent;
	VkImage     image;		// only if owned
	VkImageView view;
	VkFormat    format;
	uint32_t    frames;
	VkImageView *view_list;	// per frame
	VkImageLayout finalLayout;
} qfv_output_t;

typedef struct qfv_reference_s {
	const char *name;
	int         line;
} qfv_reference_t;

typedef struct qfv_descriptorsetlayoutinfo_s {
	const char *name;
	VkDescriptorSetLayoutCreateFlags flags;
	uint32_t    num_bindings;
	VkDescriptorSetLayoutBinding *bindings;
	VkDescriptorSetLayout setLayout;
} qfv_descriptorsetlayoutinfo_t;

typedef struct qfv_layoutinfo_s {
	const char *name;
	uint32_t    num_sets;
	qfv_reference_t *sets;
	uint32_t    num_ranges;
	VkPushConstantRange *ranges;
	VkPipelineLayout layout;
} qfv_layoutinfo_t;

typedef struct qfv_imageinfo_s {
	const char *name;
	VkImageCreateFlags flags;
	VkImageType imageType;
	VkFormat    format;
	VkExtent3D  extent;
	uint32_t    mipLevels;
	uint32_t    arrayLayers;
	VkSampleCountFlagBits samples;
	VkImageTiling tiling;
	VkImageUsageFlags usage;
	VkImageLayout initialLayout;
} qfv_imageinfo_t;

typedef struct qfv_imageviewinfo_s {
	const char *name;
	VkImageViewCreateFlags flags;
	qfv_reference_t image;
	VkImageViewType viewType;
	VkFormat    format;
	VkComponentMapping components;
	VkImageSubresourceRange subresourceRange;
} qfv_imageviewinfo_t;

typedef struct qfv_bufferinfo_s {
	const char *name;
	VkBufferCreateFlags flags;
	VkDeviceSize size;
	VkBufferUsageFlags usage;
	VkSharingMode sharingMode;
} qfv_bufferinfo_t;

typedef struct qfv_bufferviewinfo_s {
	const char *name;
	VkBufferViewCreateFlags flags;
	qfv_reference_t buffer;
	VkFormat    format;
	VkDeviceSize offset;
	VkDeviceSize range;
} qfv_bufferviewinfo_t;

typedef struct qfv_dependencymask_s {
	VkPipelineStageFlags stage;
	VkAccessFlags access;
} qfv_dependencymask_t;

typedef struct qfv_dependencyinfo_s {
	const char *name;
	qfv_dependencymask_t src;
	qfv_dependencymask_t dst;
	VkDependencyFlags flags;
} qfv_dependencyinfo_t;

typedef struct qfv_attachmentinfo_s {
	const char *name;
	VkAttachmentDescriptionFlags flags;
	VkFormat    format;
	VkSampleCountFlagBits samples;
	VkAttachmentLoadOp loadOp;
	VkAttachmentStoreOp storeOp;
	VkAttachmentLoadOp stencilLoadOp;
	VkAttachmentStoreOp stencilStoreOp;
	VkImageLayout initialLayout;
	VkImageLayout finalLayout;
	VkClearValue clearValue;
	qfv_reference_t view;
} qfv_attachmentinfo_t;

typedef struct qfv_taskinfo_s {
	exprfunc_t *func;
	const exprval_t **params;
	void       *param_data;
} qfv_taskinfo_t;

typedef struct qfv_attachmentrefinfo_s {
	const char *name;
	int         line;
	VkImageLayout layout;
	VkPipelineColorBlendAttachmentState blend;
} qfv_attachmentrefinfo_t;

typedef struct qfv_attachmentsetinfo_s {
	uint32_t     num_input;
	qfv_attachmentrefinfo_t *input;
	uint32_t     num_color;
	qfv_attachmentrefinfo_t *color;
	qfv_attachmentrefinfo_t *resolve;
	qfv_attachmentrefinfo_t *depth;
	uint32_t     num_preserve;
	qfv_reference_t *preserve;
} qfv_attachmentsetinfo_t;

typedef struct qfv_pipelineinfo_s {
	vec4f_t     color;
	const char *name;
	uint32_t    num_tasks;
	qfv_taskinfo_t *tasks;

	VkPipelineCreateFlags flags;
	VkPipelineShaderStageCreateInfo *compute_stage;
	uint32_t    dispatch[3];

	uint32_t    num_graph_stages;
	const VkPipelineShaderStageCreateInfo *graph_stages;
	const VkPipelineVertexInputStateCreateInfo *vertexInput;
	const VkPipelineInputAssemblyStateCreateInfo *inputAssembly;
	const VkPipelineTessellationStateCreateInfo *tessellation;
	const VkPipelineViewportStateCreateInfo *viewport;
	const VkPipelineRasterizationStateCreateInfo *rasterization;
	const VkPipelineMultisampleStateCreateInfo *multisample;
	const VkPipelineDepthStencilStateCreateInfo *depthStencil;
	const VkPipelineColorBlendStateCreateInfo *colorBlend;
	const VkPipelineDynamicStateCreateInfo *dynamic;
	qfv_reference_t layout;
} qfv_pipelineinfo_t;

typedef struct qfv_subpassinfo_s {
	vec4f_t     color;
	const char *name;
	uint32_t    num_dependencies;
	qfv_dependencyinfo_t *dependencies;
	qfv_attachmentsetinfo_t *attachments;
	uint32_t    num_pipelines;
	qfv_pipelineinfo_t *pipelines;
	qfv_pipelineinfo_t *base_pipeline;
} qfv_subpassinfo_t;

typedef struct qfv_framebufferinfo_s {
	qfv_reference_t *attachments;
	uint32_t    num_attachments;
	uint32_t    width;
	uint32_t    height;
	uint32_t    layers;
} qfv_framebufferinfo_t;

typedef struct qfv_renderpassinfo_s {
	vec4f_t     color;
	const char *name;
	void       *pNext;
	uint32_t    num_attachments;
	qfv_attachmentinfo_t *attachments;
	qfv_framebufferinfo_t framebuffer;
	uint32_t    num_subpasses;
	qfv_subpassinfo_t *subpasses;
} qfv_renderpassinfo_t;

typedef struct qfv_computeinfo_s {
	vec4f_t     color;
	const char *name;

	uint32_t    num_pipelines;
	qfv_pipelineinfo_t *pipelines;
} qfv_computeinfo_t;

typedef struct qfv_renderinfo_s {
	vec4f_t     color;
	const char *name;

	uint32_t    num_renderpasses;
	qfv_renderpassinfo_t *renderpasses;
	qfv_output_t output;
} qfv_renderinfo_t;

typedef struct qfv_processinfo_s {
	vec4f_t     color;
	const char *name;

	uint32_t    num_tasks;
	qfv_taskinfo_t *tasks;
} qfv_processinfo_t;

typedef struct qfv_stepinfo_s {
	vec4f_t     color;
	const char *name;

	uint32_t     num_dependencies;
	qfv_reference_t *dependencies;

	qfv_renderinfo_t *render;
	qfv_computeinfo_t *compute;
	qfv_processinfo_t *process;
} qfv_stepinfo_t;

typedef struct qfv_jobinfo_s {
	struct memsuper_s *memsuper;

	struct plitem_s *plitem;
	uint32_t     num_steps;
	qfv_stepinfo_t *steps;

	uint32_t    num_images;
	uint32_t    num_imageviews;
	qfv_imageinfo_t *images;
	qfv_imageviewinfo_t *imageviews;
	uint32_t    num_buffers;
	uint32_t    num_bufferviews;
	qfv_imageinfo_t *buffers;
	qfv_imageviewinfo_t *bufferviews;

	uint32_t    num_descriptorsetlayouts;
	qfv_descriptorsetlayoutinfo_t *descriptorsetlayouts;
} qfv_jobinfo_t;

#ifndef __QFCC__
typedef struct qfv_label_s {
	vec4f_t     color;
	const char *name;
} qfv_label_t;

typedef struct qfv_bar_s {
	VkBuffer   *buffers;
	VkDeviceSize *offsets;
	uint32_t    firstBinding;
	uint32_t    bindingCount;
} qfv_bar_t;

typedef struct qfv_pipeline_s {
	qfv_label_t label;
	VkPipelineBindPoint bindPoint;
	uint32_t    dispatch[3];
	VkPipeline  pipeline;
	VkPipelineLayout layout;

	VkViewport  viewport;
	VkRect2D    scissor;
	struct qfv_push_constants_s *push_constants;
	uint32_t    num_push_constants;
	uint32_t    num_descriptorsets;
	uint32_t    first_descriptorset;
	VkDescriptorSet *descriptorsets;

	uint32_t    task_count;
	qfv_taskinfo_t *tasks;
} qfv_pipeline_t;

typedef struct qfv_subpass_s_ {
	qfv_label_t label;
	VkCommandBufferInheritanceInfo inherit;
	VkCommandBufferBeginInfo beginInfo;
	uint32_t    pipeline_count;
	qfv_pipeline_t *pipelines;
} qfv_subpass_t_;

typedef struct qfv_renderpass_s_ {
	struct vulkan_ctx_s *vulkan_ctx;
	qfv_label_t label;		// for debugging

	VkRenderPassBeginInfo beginInfo;
	VkSubpassContents subpassContents;

	//struct qfv_imageset_s *attachment_images;
	//struct qfv_imageviewset_s *attachment_views;
	//VkDeviceMemory attachmentMemory;
	//size_t attachmentMemory_size;
	//qfv_output_t output;

	uint32_t    subpass_count;
	qfv_subpass_t_ *subpasses;
} qfv_renderpass_t_;

typedef struct qfv_render_s {
	qfv_label_t label;
	qfv_renderpass_t_ *active;
	qfv_renderpass_t_ *renderpasses;
	uint32_t    num_renderpasses;
} qfv_render_t;

typedef struct qfv_compute_s {
	qfv_label_t label;
	qfv_pipeline_t *pipelines;
	uint32_t    pipeline_count;
} qfv_compute_t;

typedef struct qfv_process_s {
	qfv_label_t label;
	qfv_taskinfo_t *tasks;
	uint32_t    task_count;
} qfv_process_t;

typedef struct qfv_step_s {
	qfv_label_t label;
	qfv_render_t *render;
	qfv_compute_t *compute;
	qfv_process_t *process;
} qfv_step_t;

typedef struct qfv_job_s {
	qfv_label_t label;
	struct qfv_resource_s *resources;
	struct qfv_resobj_s *images;
	struct qfv_resobj_s *image_views;

	uint32_t    num_renderpasses;
	uint32_t    num_pipelines;
	uint32_t    num_layouts;
	uint32_t    num_steps;
	VkRenderPass *renderpasses;
	VkPipeline *pipelines;
	VkPipelineLayout *layouts;
	qfv_step_t *steps;
	qfv_cmdbufferset_t commands;
	VkCommandPool command_pool;
} qfv_job_t;

typedef struct qfv_renderctx_s {
	struct hashctx_s *hashctx;
	exprtab_t   task_functions;
	qfv_jobinfo_t *jobinfo;
	qfv_job_t *job;
} qfv_renderctx_t;

typedef struct qfv_taskctx_s {
	struct vulkan_ctx_s *ctx;
	qfv_pipeline_t *pipeline;
} qfv_taskctx_t;

void QFV_RunRenderJob (struct vulkan_ctx_s *ctx);
void QFV_LoadRenderInfo (struct vulkan_ctx_s *ctx);
void QFV_BuildRender (struct vulkan_ctx_s *ctx);
void QFV_CreateFramebuffer (struct vulkan_ctx_s *ctx);
void QFV_DestroyFramebuffer (struct vulkan_ctx_s *ctx);
void QFV_Render_Init (struct vulkan_ctx_s *ctx);
void QFV_Render_Shutdown (struct vulkan_ctx_s *ctx);
void QFV_Render_AddTasks (struct vulkan_ctx_s *ctx, exprsym_t *task_sys);
#endif//__QFCC__

#endif//__QF_Vulkan_render_h
