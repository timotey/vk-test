#include <iostream>
#include <span>
#include <array>
#include <set>
#include <vector>
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "helper.hpp"

static VKAPI_ATTR VkBool32 VKAPI_CALL
dbcb(
    VkDebugUtilsMessageSeverityFlagBitsEXT const       t_messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT const              t_messageType,
    VkDebugUtilsMessengerCallbackDataEXT const * const t_pCallbackData,
    [[maybe_unused]] void * const                      i)
{
	auto const messageSeverity =
	    static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(
	        t_messageSeverity);
	auto const messageType =
	    static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(t_messageType);
	// auto * pCallbackData =
	//	static_cast<vk::DebugUtilsMessengerCallbackDataEXT *>(t_pCallbackData);
	auto const infobit =
	    messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo;
	auto const errorbit =
	    messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
	auto const warnbit =
	    messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
	auto const verbbit =
	    messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;
	auto const genbit =
	    (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral);
	auto const perfbit =
	    (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
	auto const valbit =
	    (messageType & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
	std::cerr << "[" << (verbbit ? 'V' : '-') << (infobit ? 'I' : '-')
	          << (warnbit ? 'W' : '-') << (errorbit ? 'E' : '-') << "]["
	          << (genbit ? 'G' : '-') << (perfbit ? 'P' : '-')
	          << (valbit ? 'V' : '-')
	          << "]validation layer: " << t_pCallbackData->pMessage
	          << std::endl;

	return VK_FALSE;
}

struct queue_family
{
	std::uint32_t index;
	std::uint32_t count;
	bool          graphics;
	bool          compute;
	bool          transfer;
	bool          sparse_binding;
	bool          protected_memory;
};

std::vector<queue_family>
enumerate_queue_families(vk::PhysicalDevice const & t_d)
{
	auto const                families = t_d.getQueueFamilyProperties();
	auto const                numbers  = range<std::uint32_t>(families.size());
	std::vector<queue_family> out;
	out.reserve(families.size());
	std::transform(
	    families.begin(),
	    families.end(),
	    numbers.begin(),
	    std::back_inserter(out),
	    [](auto const & t_f, auto i) -> queue_family {
		    return {
		        i,
		        t_f.queueCount,
		        bool(t_f.queueFlags & vk::QueueFlagBits::eCompute),
		        bool(t_f.queueFlags & vk::QueueFlagBits::eGraphics),
		        bool(t_f.queueFlags & vk::QueueFlagBits::eTransfer),
		        bool(t_f.queueFlags & vk::QueueFlagBits::eSparseBinding),
		        bool(t_f.queueFlags & vk::QueueFlagBits::eProtected)};
	    });
	// for (size_t i = 0; i < out.size(); ++i)
	//{
	//	auto const & family = families[i];
	//	out.push_back(
	//	    {i,
	//	     family.queueCount,
	//	     bool(family.queueFlags & vk::QueueFlagBits::eCompute),
	//	     bool(family.queueFlags & vk::QueueFlagBits::eGraphics),
	//	     bool(family.queueFlags & vk::QueueFlagBits::eTransfer),
	//	     bool(family.queueFlags & vk::QueueFlagBits::eSparseBinding),
	//	     bool(family.queueFlags & vk::QueueFlagBits::eProtected)});
	//}
	return out;
};

template<class InputIt>
bool
check_extension_support(
    vk::PhysicalDevice const & t_dev,
    InputIt                    t_beg,
    InputIt                    t_end)
{
	std::vector<char const *> sorted_extensions(std::distance(t_beg, t_end));
	std::partial_sort_copy(
	    t_beg,
	    t_end,
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

struct pick_devce_and_queues_t
{
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
T
choose_with_priority(
    FwIt1 const range_begin,
    FwIt1 const range_end,
    FwIt2 const opts_begin,
    FwIt2 const opts_end,
    T const     t_default)
{
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
    vk::ImageUsageFlags                       t_usage,
    vk::PhysicalDevice const &                device,
    vk::SurfaceKHR const &                    surface,
    SDL_Window *                              native_win)
{
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
	    reinterpret_cast<std::int32_t *>(&std::get<0>(native_win_surface_size)),
	    reinterpret_cast<std::int32_t *>(
	        &std::get<1>(native_win_surface_size)));
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

	auto composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
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
	    .imageUsage       = t_usage,
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

int
main(int argc, char const * const * argv)
{
	SDL_SetMainReady();
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		throw std::runtime_error(SDL_GetError());
	auto const args = parse_args(argc, argv);

	auto sdl_window = SDL_CreateWindow(
	    "TEST",
	    SDL_WINDOWPOS_UNDEFINED,
	    SDL_WINDOWPOS_UNDEFINED,
	    100,
	    100,
	    SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	if (!sdl_window)
		throw std::runtime_error(std::string("no window: ") + SDL_GetError());

	static std::array<char const *, 1> const inst_layers {
	    "VK_LAYER_KHRONOS_validation",
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
		vk::DebugUtilsMessengerCreateInfoEXT debug_info {
		    .messageSeverity =
		        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
		        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
		        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
		        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose,
		    .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
		        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
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
		for (auto const i : inst_extensions)
			std::cout << i << '\n';
		std::cout << ext_count << '\n';
		auto                 inst = vk::createInstanceUnique(inst_info);
		vk::UniqueSurfaceKHR window;
		{
			VkSurfaceKHR native_win;
			SDL_Vulkan_CreateSurface(sdl_window, *inst, &native_win);
			window.get() = native_win;
		}

		auto queues = pick_devce_and_queues(
		    *inst,
		    *window,
		    dev_extensions.begin(),
		    dev_extensions.end());
		vk::PhysicalDeviceFeatures             f {};
		std::vector<float>                     queue_priorities_graphics {1};
		std::vector<float>                     queue_priorities_present {1};
		std::vector<vk::DeviceQueueCreateInfo> queue_info {
		    {.queueFamilyIndex =
		         static_cast<std::uint32_t>(queues.graphics.index),
		     .queueCount =
		         static_cast<std::uint32_t>(queue_priorities_graphics.size()),
		     .pQueuePriorities = queue_priorities_graphics.data()},
		    {.queueFamilyIndex =
		         static_cast<std::uint32_t>(queues.present.index),
		     .queueCount =
		         static_cast<std::uint32_t>(queue_priorities_present.size()),
		     .pQueuePriorities = queue_priorities_present.data()},
		};
		queue_info.erase(std::unique(
		    std::begin(queue_info),
		    std::end(queue_info),
		    [](vk::DeviceQueueCreateInfo const & a,
		       vk::DeviceQueueCreateInfo const & b) {
			    return a.queueFamilyIndex == b.queueFamilyIndex;
		    }));

		auto logic_dev = queues.device.createDeviceUnique(
		    {
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

		auto queue_present  = logic_dev->getQueue(queues.present.index, 0);
		auto queue_graphics = logic_dev->getQueue(queues.graphics.index, 0);

		auto swapchain_info = configure_swapchain(
		    {},
		    {vk::PresentModeKHR::eMailbox},
		    3,
		    1,
		    vk::ImageUsageFlagBits::eColorAttachment,
		    queues.device,
		    *window,
		    sdl_window);
		if (queues.graphics.index != queues.present.index)
		{
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
		std::transform(swapchain_images.begin(), swapchain_images.end(), std::back_inserter(swapchain_image_views),
		[&logic_dev, &swapchain_info](auto const & image)
		{
			return logic_dev->createImageViewUnique({
			    .image    = image,
			    .viewType = vk::ImageViewType::e2D,
			    .format   = swapchain_info.imageFormat,
			    .components =
			        {
			            .r = vk::ComponentSwizzle::eIdentity,
			            .g = vk::ComponentSwizzle::eIdentity,
			            .b = vk::ComponentSwizzle::eIdentity,
			            .a = vk::ComponentSwizzle::eIdentity,
			        },
			    .subresourceRange =
			        {
			            .aspectMask     = vk::ImageAspectFlagBits::eColor,
			            .baseMipLevel   = 0,
			            .levelCount     = 1,
			            .baseArrayLayer = 0,
			            .layerCount     = 1,
			        },
			});
		});

		std::cin.ignore();
	}
	SDL_DestroyWindow(sdl_window);
	return 0;
}
