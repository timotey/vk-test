#include <algorithm>
#include <iterator>
#include <stdexcept>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <ranges>
#include <bit>
#include <iostream>
#include <fstream>
#include <span>
#include <array>
#include <set>
#include <vector>
#include <filesystem>
#include <optional>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "helper.hpp"

namespace views = std::ranges::views;
namespace ranges= std::ranges;

static VKAPI_ATTR VkBool32 VKAPI_CALL
dbcb(
    VkDebugUtilsMessageSeverityFlagBitsEXT const       t_messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT const              t_messageType,
    VkDebugUtilsMessengerCallbackDataEXT const * const t_pCallbackData,
    [[maybe_unused]] void * const                      i)
{
    using dmt = vk::DebugUtilsMessageTypeFlagBitsEXT;
    using dms = vk::DebugUtilsMessageSeverityFlagBitsEXT;
	auto const messageSeverity = static_cast<dms>(t_messageSeverity);
	auto const messageType     = static_cast<dmt>(t_messageType);
	// auto * pCallbackData =
	//	static_cast<vk::DebugUtilsMessengerCallbackDataEXT *>(t_pCallbackData);
	auto const infobit  = messageSeverity & dms::eInfo;
	auto const errorbit = messageSeverity & dms::eError;
	auto const warnbit  = messageSeverity & dms::eWarning;
	auto const verbbit  = messageSeverity & dms::eVerbose;
	auto const genbit  = (messageType & dmt::eGeneral);
	auto const perfbit = (messageType & dmt::ePerformance);
	auto const valbit  = (messageType & dmt::eValidation);
	std::cerr << "["
        << (verbbit  ? 'V' : '-')
        << (infobit  ? 'I' : '-')
	    << (warnbit  ? 'W' : '-')
        << (errorbit ? 'E' : '-')
        << "]["
	    << (genbit  ? 'G' : '-')
        << (perfbit ? 'P' : '-')
	    << (valbit  ? 'V' : '-')
	    << "]validation layer: "
        << t_pCallbackData->pMessage << std::endl;
	return VK_FALSE;
}

struct queue_family {
	std::uint32_t index;
	std::uint32_t count;
	bool graphics;
	bool compute;
	bool transfer;
	bool sparse_binding;
	bool protected_memory;
};

std::vector<queue_family>
enumerate_queue_families(vk::PhysicalDevice const & t_d){
	auto const families = t_d.getQueueFamilyProperties();
	auto const numbers  = range(static_cast<std::uint32_t>(families.size()));
	std::vector<queue_family> out;
	out.reserve(families.size());
	std::transform( families.begin(), families.end(), numbers.begin(),
	    std::back_inserter(out),
	    [](auto const & t_f, auto i) -> queue_family {
            using qfb = vk::QueueFlagBits;
		    return {
		        i,
		        t_f.queueCount,
		        bool(t_f.queueFlags & qfb::eCompute),
		        bool(t_f.queueFlags & qfb::eGraphics),
		        bool(t_f.queueFlags & qfb::eTransfer),
		        bool(t_f.queueFlags & qfb::eSparseBinding),
		        bool(t_f.queueFlags & qfb::eProtected)};
	    });
	return out;
};

template<class InputIt>
bool
check_extension_support(
    vk::PhysicalDevice const & t_dev, InputIt t_beg, InputIt t_end){
	std::vector<char const *> sorted_extensions(
	    static_cast<std::size_t>(std::distance(t_beg, t_end)));
	std::partial_sort_copy( t_beg, t_end,
	    sorted_extensions.begin(),
	    sorted_extensions.end(),
	    [](char const * a, char const * b) {
		    return std::strcmp(a, b) < 0;
	    });

	std::vector<vk::ExtensionProperties> dev_extensions =
	    t_dev.enumerateDeviceExtensionProperties();
	std::sort(
	    dev_extensions.begin(),
	    dev_extensions.end(),
	    [](auto const & a, auto const & b) {
		    return std::strcmp(a.extensionName, b.extensionName) < 0;
	    });
	std::vector<char const *> dev_extensions_stripped(dev_extensions.size());
	std::transform(
	    dev_extensions.begin(),
	    dev_extensions.end(),
	    dev_extensions_stripped.begin(),
	    [](auto const & a) {
		    return a.extensionName.data();
	    });
	return std::includes(
	    dev_extensions_stripped.begin(),
	    dev_extensions_stripped.end(),
	    sorted_extensions.begin(),
	    sorted_extensions.end(),
	    [](auto a, auto b) {
		    return std::strcmp(a, b) < 0;
	    });
}

struct pick_devce_and_queues_t{
	vk::PhysicalDevice device;
	queue_family       graphics;
	queue_family       present;
};

template<class FwIt>
pick_devce_and_queues_t
pick_devce_and_queues(
    vk::Instance const &   inst,
    vk::SurfaceKHR const & window,
    FwIt                   t_ext_begin,
    FwIt                   t_ext_end)
{
	std::pair<vk::Device, std::vector<vk::Queue>> ret;
	auto devices = inst.enumeratePhysicalDevices();
	for (auto device : devices)
	{
		auto queues = enumerate_queue_families(device);
		if (!check_extension_support(device, t_ext_begin, t_ext_end))
			continue;
		auto graphics_queue_info_it = std::find_if(
		    queues.begin(),
		    queues.end(),
		    [&window = std::as_const(window)](queue_family const & a) {
			    return a.graphics;
		    });
		auto present_queue_info_it = std::find_if(
		    queues.begin(),
		    queues.end(),
		    [&window = std::as_const(window),
		     &device = std::as_const(device)](queue_family const & a) {
			    return device.getSurfaceSupportKHR(a.index, window);
		    });
		if (present_queue_info_it != queues.end() &&
		    graphics_queue_info_it != queues.end())
		{
			return {device, *present_queue_info_it, *graphics_queue_info_it};
		}
	}
	throw std::runtime_error("No suitable vulkan device");
}

template<class T, class FwIt1, class FwIt2>
T choose_with_priority(
    FwIt1 const range_begin,
    FwIt1 const range_end,
    FwIt2 const opts_begin,
    FwIt2 const opts_end,
    T const     t_default) {
	for (auto beg = opts_begin; beg != opts_end; ++beg)
		if (auto result = std::find(range_begin, range_end, *beg);
		    result != range_end)
			return *beg;
	return t_default;
}

vk::SwapchainCreateInfoKHR
configure_swapchain(
    std::vector<vk::SurfaceFormatKHR> const & t_preferred_formats,
    std::vector<vk::PresentModeKHR> const &   t_preferred_present_modes,
    std::uint32_t                             t_preferred_image_count,
    std::uint32_t                             t_array_layers,
    vk::PhysicalDevice const &                device,
    vk::SurfaceKHR const &                    surface,
    SDL_Window *                              native_win) {
	// get what's avaliavble in our graphics device
	auto avaliable_capabilities = device.getSurfaceCapabilitiesKHR(surface);
	auto avaliable_formats      = device.getSurfaceFormatsKHR(surface);
	auto avaliable_modes        = device.getSurfacePresentModesKHR(surface);
	if (avaliable_formats.empty() || avaliable_modes.empty())
		throw std::runtime_error(
		    "swapchain inadequate"); // we cannot create the swapchain if
		                             // there's no options to choose from

	// get SDL's opinion on window size
	std::tuple<std::uint32_t, std::uint32_t> native_win_surface_size;
	SDL_Vulkan_GetDrawableSize(
	    native_win,
	    (std::int32_t *)(&std::get<0>(native_win_surface_size)),
	    (std::int32_t *)(&std::get<1>(native_win_surface_size)));
	// choose the right image extent based on vulkan's and SDL's perspective
	vk::Extent2D const image_size =
	    (avaliable_capabilities.currentExtent.width !=
	     std::numeric_limits<std::uint32_t>::max()) ?
        avaliable_capabilities.currentExtent :
        vk::Extent2D {
	        std::clamp(
	            std::get<0>(native_win_surface_size),
	            avaliable_capabilities.minImageExtent.width,
	            avaliable_capabilities.maxImageExtent.width),
	        std::clamp(
	            std::get<1>(native_win_surface_size),
	            avaliable_capabilities.minImageExtent.height,
	            avaliable_capabilities.maxImageExtent.height)};

	// choose the right image format
	auto image_format = choose_with_priority(
	    avaliable_formats.begin(),
	    avaliable_formats.end(),
	    t_preferred_formats.begin(),
	    t_preferred_formats.end(),
	    avaliable_formats.front());

	auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	auto image_count     = t_preferred_image_count;
	return {
	    .surface       = surface,
	    .minImageCount = std::clamp(
	        image_count,
	        avaliable_capabilities.minImageCount,
	        avaliable_capabilities.maxImageCount ?
                avaliable_capabilities.maxImageCount :
                std::numeric_limits<decltype(
	                avaliable_capabilities.maxImageCount)>::max()),
	    .imageFormat      = image_format.format,
	    .imageColorSpace  = image_format.colorSpace,
	    .imageExtent      = image_size,
	    .imageArrayLayers = t_array_layers,
	    .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
	    .compositeAlpha =
	        (composite_alpha & avaliable_capabilities.supportedCompositeAlpha) ?
            composite_alpha :
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
	    .presentMode = choose_with_priority(
	        avaliable_modes.begin(),
	        avaliable_modes.end(),
	        t_preferred_present_modes.begin(),
	        t_preferred_present_modes.end(),
	        vk::PresentModeKHR::eImmediate),
	    .clipped = true,
	};
}

vk::UniqueShaderModule
create_shader_module(vk::Device const & t_d, std::filesystem::path t_path_to_the_shader){
	std::ifstream shader_contents(t_path_to_the_shader, std::ios::binary);
	if (!shader_contents.is_open()) throw std::runtime_error("could not find the shader by path");
	shader_contents.seekg(0, std::ios::end);
	std::vector<std::uint32_t> spirv_data(
	    static_cast<std::size_t>(shader_contents.tellg()) /
	    sizeof(decltype(spirv_data)::value_type));
	shader_contents.seekg(std::ios::beg);

	shader_contents.read(
	    reinterpret_cast<char *>(spirv_data.data()),
	    static_cast<std::streamoff>(
	        spirv_data.size() * sizeof(decltype(spirv_data)::value_type)));
	return t_d.createShaderModuleUnique(
	    {.codeSize = spirv_data.size() * 4, .pCode = spirv_data.data()});
}


std::pair<vk::UniquePipeline, std::vector<vk::UniqueShaderModule>>
make_graphics_pipeline(
    vk::Device const &             t_dev,
    std::string_view               t_pipeline_name,
    vk::GraphicsPipelineCreateInfo t_info) {
	namespace fs = std::filesystem;
	auto const path_to_shaders =
	    fs::current_path() / "build" / "shaders" / t_pipeline_name;
	if (!fs::exists(path_to_shaders))
		throw std::runtime_error("no shader path");

    using stage = vk::ShaderStageFlagBits;
	std::vector<vk::PipelineShaderStageCreateInfo> stages;
    std::vector<vk::UniqueShaderModule> modules;
    modules.push_back(create_shader_module(t_dev, path_to_shaders / "main.vert.spv"));
    modules.push_back(create_shader_module(t_dev, path_to_shaders / "main.frag.spv"));
	stages.reserve(5); // it's the maximum number of stages iirc
        stages.push_back({
            .stage = stage::eVertex,
            .module = *(modules[0]),
            .pName  = "main",
        });
        stages.push_back({
            .stage = stage::eFragment,
            .module = *(modules[1]),
            .pName  = "main",
        });
	t_info.stageCount = static_cast<std::uint32_t>(stages.size());
	t_info.pStages    = stages.data();
	return {t_dev.createGraphicsPipelineUnique({}, t_info).value, std::move(modules)};
}

int
main(int argc, char const * const * argv){
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		throw std::runtime_error(SDL_GetError());
	auto const args = parse_args(argc, argv);

	auto sdl_window = SDL_CreateWindow(
	    "TEST", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1000, 1000,
	    SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	if (!sdl_window)
		throw std::runtime_error(std::string("no window: ") + SDL_GetError());

	static std::array<char const *, 1> const inst_layers {
	    "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_standard_validation",
	};
	static std::array<char const *, 0> const dev_layers {};
	static std::array<char const *, 1> const dev_extensions {
	    "VK_KHR_swapchain",
	};
	{
		vk::ApplicationInfo const info {
		    .pApplicationName   = "Hello triangle",
		    .applicationVersion = 1,
		    .pEngineName        = "None",
		    .engineVersion      = 1,
		    .apiVersion         = VK_API_VERSION_1_0};
        using dsf = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using dmt = vk::DebugUtilsMessageTypeFlagBitsEXT;
		vk::DebugUtilsMessengerCreateInfoEXT debug_info {
		    .messageSeverity = dsf::eError | dsf::eWarning | dsf::eInfo | dsf::eVerbose,
		    .messageType = dmt::eGeneral | dmt::ePerformance | dmt::eValidation,
		    .pfnUserCallback = dbcb,
		};
		std::uint32_t ext_count = 0;
		SDL_Vulkan_GetInstanceExtensions(sdl_window, &ext_count, nullptr);
		std::vector<char const *> inst_extensions(ext_count);
		SDL_Vulkan_GetInstanceExtensions(
		    sdl_window,
		    &ext_count,
		    inst_extensions.data());
		vk::InstanceCreateInfo inst_info {
		    .pNext             = static_cast<void *>(&debug_info),
		    .pApplicationInfo  = &info,
		    .enabledLayerCount = static_cast<std::uint32_t>(inst_layers.size()),
		    .ppEnabledLayerNames     = inst_layers.data(),
		    .enabledExtensionCount   = ext_count,
		    .ppEnabledExtensionNames = inst_extensions.data(),
		};
		auto inst = vk::createInstanceUnique(inst_info);
		auto window = [&]{
			VkSurfaceKHR native_win;
			SDL_Vulkan_CreateSurface(sdl_window, *inst, &native_win);
			return vk::UniqueSurfaceKHR{native_win, vk::ObjectDestroy<vk::Instance, vk::DispatchLoaderStatic>(*inst)};
		}();

		auto queues = pick_devce_and_queues( *inst, *window, dev_extensions.begin(), dev_extensions.end());
		vk::PhysicalDeviceFeatures             f {};
		std::vector<float>                     queue_priorities_graphics {1};
		std::vector<float>                     queue_priorities_present {1};
		std::vector<vk::DeviceQueueCreateInfo> queue_info {
		    {
                .queueFamilyIndex = static_cast<std::uint32_t>(queues.graphics.index),
		        .queueCount = static_cast<std::uint32_t>(queue_priorities_graphics.size()),
		        .pQueuePriorities = queue_priorities_graphics.data()
            },{
                .queueFamilyIndex = static_cast<std::uint32_t>(queues.present.index),
		        .queueCount = static_cast<std::uint32_t>(queue_priorities_present.size()),
		        .pQueuePriorities = queue_priorities_present.data()
            },
		};
		queue_info.erase(std::unique(
		    std::begin(queue_info),
		    std::end(queue_info),
		    [](vk::DeviceQueueCreateInfo const & a,
		       vk::DeviceQueueCreateInfo const & b) {
			    return a.queueFamilyIndex == b.queueFamilyIndex;
		    }));

		auto logic_dev = queues.device.createDeviceUnique( {
		        .queueCreateInfoCount =
		            static_cast<std::uint32_t>(queue_info.size()),
		        .pQueueCreateInfos = queue_info.data(),
		        .enabledLayerCount =
		            static_cast<std::uint32_t>(dev_layers.size()),
		        .ppEnabledLayerNames     = dev_layers.data(),
		        .enabledExtensionCount   = dev_extensions.size(),
		        .ppEnabledExtensionNames = dev_extensions.data(),
		        .pEnabledFeatures        = &f,
		    },
		    nullptr);

		[[maybe_unused]] auto queue_present = logic_dev->getQueue(queues.present.index, 0);
		[[maybe_unused]] auto queue_graphics = logic_dev->getQueue(queues.graphics.index, 0);

		auto swapchain_info = configure_swapchain( {}, {vk::PresentModeKHR::eImmediate}, 3, 1, queues.device, *window, sdl_window);
        if (queues.graphics.index != queues.present.index) {
			swapchain_info.imageSharingMode      = vk::SharingMode::eConcurrent;
			swapchain_info.queueFamilyIndexCount = 2;
			static auto queue_family_indices     = {
                queues.graphics.index,
                queues.present.index,
            };
			swapchain_info.pQueueFamilyIndices = &*queue_family_indices.begin();
		}
		auto swapchain = logic_dev->createSwapchainKHRUnique(swapchain_info);
        
		auto swapchain_images = logic_dev->getSwapchainImagesKHR(*swapchain);
		std::vector<vk::UniqueImageView> swapchain_image_views;
		swapchain_image_views.reserve(swapchain_images.size());
		for (auto const & image:swapchain_images) {
            using cs = vk::ComponentSwizzle;
            swapchain_image_views.push_back(
                logic_dev->createImageViewUnique({
                    .image    = image,
                    .viewType = vk::ImageViewType::e2D,
                    .format   = swapchain_info.imageFormat,
                    .components = { .r = cs::eIdentity, .g = cs::eIdentity, .b = cs::eIdentity, .a = cs::eIdentity, },
                    .subresourceRange = {
                            .aspectMask     = vk::ImageAspectFlagBits::eColor,
                            .baseMipLevel   = 0,
                            .levelCount     = 1,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                })
            );
        }

		vk::PipelineVertexInputStateCreateInfo vertexinput_info = {
		    .vertexBindingDescriptionCount   = 0,
		    .vertexAttributeDescriptionCount = 0,
		};
		vk::PipelineInputAssemblyStateCreateInfo inputassembly_info = {
		    .topology               = vk::PrimitiveTopology::eTriangleList,
		    .primitiveRestartEnable = false,
		};
		vk::Viewport viewport_info = {
		    .x      = 0,
		    .y      = 0,
		    .width  = static_cast<float>(swapchain_info.imageExtent.width),
		    .height = static_cast<float>(swapchain_info.imageExtent.height),
		};
		vk::Rect2D scissor_info = {
		    .offset = {0, 0},
		    .extent = {1, 1},
		};
		vk::PipelineViewportStateCreateInfo viewportstate_info = {
		    .viewportCount = 1,
		    .pViewports    = &viewport_info,
		    .scissorCount  = 1,
		    .pScissors     = &scissor_info,
		};
		vk::PipelineRasterizationStateCreateInfo rasterizationrtate_info = {
		    .rasterizerDiscardEnable = false,
		    .polygonMode             = vk::PolygonMode::eFill,
		    .cullMode                = vk::CullModeFlagBits::eBack,
		    .frontFace               = vk::FrontFace::eClockwise,
		    .depthBiasEnable         = false,
		    .lineWidth               = 1.0f,
		};
		vk::PipelineMultisampleStateCreateInfo multisamplestate_info = {
		    .rasterizationSamples = vk::SampleCountFlagBits::e1,
		    .sampleShadingEnable  = false,
		};
        using ccf = vk::ColorComponentFlagBits;
		vk::PipelineColorBlendAttachmentState noblend_attachment = {
		    .blendEnable    = false,
		    .colorWriteMask = ccf::eR | ccf::eG | ccf::eB | ccf::eA,
		};
		vk::PipelineColorBlendStateCreateInfo blendstate_info = {
		    .logicOpEnable   = false,
		    .logicOp         = vk::LogicOp::eCopy,
		    .attachmentCount = 1,
		    .pAttachments    = &noblend_attachment,
		};

        vk::PipelineLayoutCreateInfo pipeline_layout_info{};
        auto pipeline_layout = logic_dev->createPipelineLayoutUnique(pipeline_layout_info);

        using al = vk::AttachmentLoadOp;
        using as = vk::AttachmentStoreOp;
        using imglayout = vk::ImageLayout;
        vk::AttachmentDescription color_attachment{
            .format  = swapchain_info.imageFormat,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp  = al::eClear,
            .storeOp = as::eStore,
            .stencilLoadOp = al::eLoad,
            .stencilStoreOp = as::eStore,
            .initialLayout = imglayout::eUndefined,
            .finalLayout = imglayout::ePresentSrcKHR,
        };
        vk::AttachmentReference attach_ref{
            .attachment = 0,
            .layout = imglayout::eColorAttachmentOptimal,
        };
        vk::SubpassDescription sd{
            .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attach_ref,
        };
        vk::SubpassDependency dep{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        };
        vk::RenderPassCreateInfo rp_info{
            .attachmentCount = 1,
            .pAttachments    = &color_attachment,
            .subpassCount = 1,
            .pSubpasses = &sd,
            .dependencyCount = 1,
            .pDependencies = &dep,
        };
        auto render_pass = logic_dev->createRenderPassUnique(rp_info);
		vk::GraphicsPipelineCreateInfo pipeline_info = {
            .stageCount          = 2,
            .pStages             = nullptr,
		    .pVertexInputState   = &vertexinput_info,
		    .pInputAssemblyState = &inputassembly_info,
		    .pViewportState      = &viewportstate_info,
		    .pRasterizationState = &rasterizationrtate_info,
		    .pMultisampleState   = &multisamplestate_info,
		    .pDepthStencilState  = nullptr,
		    .pColorBlendState    = &blendstate_info,
		    .pDynamicState       = nullptr,
            .layout              = *pipeline_layout,
            .renderPass          = *render_pass,
		};
		auto pipeline =
		    make_graphics_pipeline(*logic_dev, "default", pipeline_info);
        std::vector<vk::UniqueFramebuffer> fbos;
        fbos.reserve(swapchain_image_views.size());
        for(auto&& image_view : swapchain_image_views){
            vk::FramebufferCreateInfo i{
                .renderPass = *render_pass,
                .attachmentCount = 1,
                .pAttachments = &*image_view,
                .width  = swapchain_info.imageExtent.width,
                .height = swapchain_info.imageExtent.height,
                .layers = 1,
            };
            fbos.push_back(logic_dev->createFramebufferUnique(i));
        }
        vk::CommandPoolCreateInfo cmd_pool_info{
            .queueFamilyIndex = queue_info[0].queueFamilyIndex,
        };
        auto cmd_pool = logic_dev->createCommandPoolUnique(cmd_pool_info);
        vk::CommandBufferAllocateInfo cmd_buffer_info{
            .commandPool = *cmd_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = uint32_t( fbos.size()),
        };
        auto cmd_bufs = logic_dev->allocateCommandBuffersUnique(cmd_buffer_info);
        for(auto [buf, fbo] : utils::zip(cmd_bufs, fbos)){
            vk::CommandBufferBeginInfo cmd_buf_beg_info{};
            buf->begin(cmd_buf_beg_info);
            vk::ClearValue clean{};

            vk::RenderPassBeginInfo pass_info{
                .renderPass  = *render_pass,
                .framebuffer = *fbo,
                .renderArea = {
                    .offset = {},
                    .extent = swapchain_info.imageExtent,
                },
                .clearValueCount = 1,
                .pClearValues = &clean,
            };
            buf->beginRenderPass(pass_info, vk::SubpassContents::eInline);
            buf->bindPipeline(vk::PipelineBindPoint::eGraphics, *(pipeline.first));
            buf->draw(3,1,0,0);
            buf->endRenderPass();
            buf->end();
        }
        vk::SemaphoreCreateInfo semaphore_info{};
        auto image_available = logic_dev->createSemaphoreUnique(semaphore_info);
        auto render_finished = logic_dev->createSemaphoreUnique(semaphore_info);
        bool running  = true;
        while(running){
            SDL_Event e;
            while(SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = false;
            auto image_index = logic_dev->acquireNextImageKHR(*swapchain, std::numeric_limits<uint64_t>::max(), *image_available).value;

            vk::PipelineStageFlags wait_stages{
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
            };
            vk::SubmitInfo submit_info{
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*image_available,
                .pWaitDstStageMask = &wait_stages,
                .commandBufferCount = 1,
                .pCommandBuffers = &*(cmd_bufs[image_index]),
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &*render_finished,
            };
            queue_graphics.submit({submit_info});
            vk::PresentInfoKHR present_info{
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*render_finished,
                .swapchainCount = 1,
                .pSwapchains = &*swapchain,
                .pImageIndices = &image_index,
            };
            auto result = queue_present.presentKHR(present_info);
            if(result != vk::Result::eSuccess && result != vk::Result::eErrorOutOfDateKHR && result != vk::Result::eSuboptimalKHR)
                throw std::runtime_error(vk::to_string(result));
            queue_present.waitIdle();
        }
        logic_dev->waitIdle();
	}
	SDL_DestroyWindow(sdl_window);
	return 0;
}
