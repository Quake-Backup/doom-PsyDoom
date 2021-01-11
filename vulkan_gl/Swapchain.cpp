#include "Swapchain.h"

#include "DeviceSurfaceCaps.h"
#include "Finally.h"
#include "LogicalDevice.h"
#include "ScreenFramebufferMgr.h"
#include "Semaphore.h"
#include "Utils.h"
#include "VkFuncs.h"
#include "VulkanInstance.h"
#include "WindowSurface.h"

#include <SDL.h>

BEGIN_NAMESPACE(vgl)

//------------------------------------------------------------------------------------------------------------------------------------------
// Creates an uninitialized swapchain
//------------------------------------------------------------------------------------------------------------------------------------------
Swapchain::Swapchain() noexcept
    : mbIsValid(false)
    , mbNeedsRecreate(false)
    , mpDevice(nullptr)
    , mSurfaceFormat()
    , mPresentMode()
    , mSwapExtentW(0)
    , mSwapExtentH(0)
    , mLength(0)
    , mVkSwapchain(VK_NULL_HANDLE)
    , mVkImages()
    , mVkImageViews()
{
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Automatically destroys the swapchain
//------------------------------------------------------------------------------------------------------------------------------------------
Swapchain::~Swapchain() noexcept {
    destroy();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Attempts to initialize the swapchain using the specified surface format and returns 'true' if successful
//------------------------------------------------------------------------------------------------------------------------------------------
bool Swapchain::init(LogicalDevice& device, const VkSurfaceFormatKHR& surfaceFormat) noexcept {
    // Preconditions
    ASSERT_LOG((!mbIsValid), "Must call destroy() before re-initializing!");
    ASSERT(device.getVkDevice());
    ASSERT(device.getScreenFramebufferMgr().isValid());
    ASSERT(surfaceFormat.format != VK_FORMAT_UNDEFINED);

    // If anything goes wrong, cleanup on exit - don't half initialize!
    auto cleanupOnError = finally([&]{
        if (!mbIsValid) {
            destroy(true);
        }
    });

    // Save these fields for later use
    mpDevice = &device;
    mSurfaceFormat = surfaceFormat;

    // Choosing present mode, swap extent and swapchain length
    choosePresentMode();
    
    if (!chooseSwapExtent()) {
        ASSERT_FAIL("Failed to choose a swap extent for the swapchain!");
        return false;
    }

    chooseSwapchainLength();

    // Create the actual swap chain itself and get all images in the swap chain, abort on failure also
    if (!createSwapchain()) {
        ASSERT_FAIL("Failed to create the swapchain!");
        return false;
    }

    if (!createSwapchainImageViews()) {
        ASSERT_FAIL("Failed to create the swapchain image views!");
        return false;
    }

    // All went well if we got to here!
    mbIsValid = true;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Destroys the swapchain and releases its resources
//------------------------------------------------------------------------------------------------------------------------------------------
void Swapchain::destroy(const bool bForceIfInvalid) noexcept {
    // Only destroy if we need to
    if ((!mbIsValid) && (!bForceIfInvalid))
        return;
    
    // Preconditions
    ASSERT_LOG(((!mpDevice) || mpDevice->getVkDevice()), "Parent device must still be valid if defined!");

    // Destroy the swapchain
    mbIsValid = false;

    for (const VkImageView imageView : mVkImageViews) {
        // Note: need to null check because if image view setup fails we could one or null image views in the list
        if (imageView) {
            ASSERT(mpDevice && mpDevice->getVkDevice());
            const VkFuncs& vkFuncs = mpDevice->getVkFuncs();
            vkFuncs.vkDestroyImageView(mpDevice->getVkDevice(), imageView, nullptr);
        }
    }

    mVkImageViews.clear();
    mVkImages.clear();          // Note: the images are automatically created and destroyed as part of the swap chain so just clear the references

    if (mVkSwapchain) {
        ASSERT(mpDevice && mpDevice->getVkDevice());
        const VkFuncs& vkFuncs = mpDevice->getVkFuncs();
        vkFuncs.vkDestroySwapchainKHR(mpDevice->getVkDevice(), mVkSwapchain, nullptr);
        mVkSwapchain = VK_NULL_HANDLE;
    }

    mLength = 0;
    mSwapExtentH = 0;
    mSwapExtentW = 0;
    mPresentMode = {};
    mSurfaceFormat = {};
    mpDevice = nullptr;
    mbNeedsRecreate = false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Marks the swapchain as needing recreation; called after a window resize
//------------------------------------------------------------------------------------------------------------------------------------------
void Swapchain::setNeedsRecreate() noexcept {
    mbNeedsRecreate = true;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Attempts to present the specified swap chain image.
// Presentation will wait for the given render finished semaphore to be signalled.
//
// Note: may fail due to the swap chain needing recreation.
// If this is the case then the 'needs recreate' flag will be set to 'true'.
//------------------------------------------------------------------------------------------------------------------------------------------
bool Swapchain::presentImage(const uint8_t imageIndex, const Semaphore& renderFinishedSemaphore) noexcept {
    // Sanity checks
    ASSERT(mbIsValid);
    ASSERT(imageIndex < mLength);
    ASSERT(renderFinishedSemaphore.isValid());

    // Do the present!
    const VkSemaphore waitSemaphores[] = { renderFinishedSemaphore.getVkSemaphore() };
    const VkSwapchainKHR swapChains[] = { mVkSwapchain };
    const uint32_t swapChainImageIndexes[] = { imageIndex };
    
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = C_ARRAY_SIZE(waitSemaphores);
    presentInfo.pWaitSemaphores = waitSemaphores;
    presentInfo.swapchainCount = C_ARRAY_SIZE(swapChainImageIndexes);
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = swapChainImageIndexes;
    presentInfo.pResults = nullptr;                         // Don't need an array of results, just 1 swap chain being presented!

    // Check the result - recreate the swap chain later if it fails or is suboptimal
    const VkFuncs& vkFuncs = mpDevice->getVkFuncs();
    const VkResult queuePresentResult = vkFuncs.vkQueuePresentKHR(mpDevice->getPresentationQueue(), &presentInfo);

    if (queuePresentResult == VK_SUCCESS) {
        return true;
    }
    else {
        if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentResult == VK_SUBOPTIMAL_KHR) {
            // Swapchain present failed due to need for recreation!
            mbNeedsRecreate = true;
        } else {
            ASSERT_FAIL("Failed to present the presentation queue with an unknown error!");
        }
    }

    // Failed if we got to here!
    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Attempts to acquire a swap chain image for later presentation.
// Returns UINT32_MAX on failure, if for example the swap chain needs recreation.
//
// The given semaphore is also signalled once the image index is ready, so subsequent
// rendering operations should block on that before the pixel output phase.
// That is of course assuming getting the image succeeds in the first place!
//------------------------------------------------------------------------------------------------------------------------------------------
uint32_t Swapchain::acquireImage(Semaphore& imageReadySemaphore) noexcept {
    // Sanity checks
    ASSERT(mbIsValid);
    ASSERT(imageReadySemaphore.isValid());

    // Wait for the previous presentation to finish first...
    //
    // Note that this is not specifically required, but validation layers may require us to explicitly sync with the GPU in
    // order to avoid a memory leak within the validation layer. See:
    //  https://vulkan-tutorial.com/Drawing_a_triangle/Drawing/Rendering_and_presentation
    //
    ASSERT(mbIsValid);
    const VkFuncs& vkFuncs = mpDevice->getVkFuncs();

    if (mpDevice->getVulkanInstance()->areValidationLayersEnabled()) {
        if (vkFuncs.vkQueueWaitIdle(mpDevice->getPresentationQueue()) != VK_SUCCESS) {
            ASSERT_FAIL("Failed to wait for the queue to be idle!");
        }
    }

    // If the swap chain needs recreation then acquiring an image fails
    if (mbNeedsRecreate)
        return UINT32_MAX;

    // Try to accquire an image from the swap chain and wait for as long as required.
    // Note that upon acquiring it may still not be ready to use as it may be in the process of being presented.
    // Therefore the app should wait on the 'mImageReadySemaphore' synchronization primitive.
    uint32_t swapImageIdx = UINT32_MAX;

    if (vkFuncs.vkAcquireNextImageKHR(
            mpDevice->getVkDevice(),
            mVkSwapchain,
            UINT64_MAX,
            imageReadySemaphore.getVkSemaphore(),
            nullptr,
            &swapImageIdx
        )
        != VK_SUCCESS
    )
    {
        ASSERT_FAIL("Failed to acquire an image to render!");
        return UINT32_MAX;
    }

    ASSERT(swapImageIdx < mLength);
    return swapImageIdx;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Chooses a presentation mode for the swap chain
//------------------------------------------------------------------------------------------------------------------------------------------
void Swapchain::choosePresentMode() noexcept {
    // Sanity checks
    ASSERT(mpDevice);
    ASSERT(mpDevice->getScreenFramebufferMgr().isValid());

    // There should be a valid surface caps object and at least 1 valid present mode if we've reached here!
    ScreenFramebufferMgr& framebufferMgr = mpDevice->getScreenFramebufferMgr();
    const DeviceSurfaceCaps& surfaceCaps = framebufferMgr.getDeviceSurfaceCaps();
    const std::vector<VkPresentModeKHR>& vkPresentModes = surfaceCaps.getVkPresentModes();
    ASSERT(!vkPresentModes.empty());

    // Prefer VK_PRESENT_MODE_MAILBOX_KHR if available since that allows us to implement tripple buffering.
    // Whenever we submit new images to the queue in this mode and the queue is full then the current image in
    // waiting will simply be replaced...
    //
    // Sigh... This actually appears to cause tearing with current NV drivers.
    // Disable even though it is technically more desirable! Perhaps one day the bug will be fixed.
    // Not sure how well or not this works on AMD.
    //
    // TODO: investigate if subpass dependencies might fix this - sync bug?
    #if 0
        if (Utils::containerContains(vkPresentModes, VK_PRESENT_MODE_MAILBOX_KHR)) {
            mPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            return;
        }
    #endif

    // Prefer VK_PRESENT_MODE_FIFO_KHR
    if (Utils::containerContains(vkPresentModes, VK_PRESENT_MODE_FIFO_KHR)) {
        mPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        return;
    }

    // This is sort of double buffered, but will present immediately (and tear) if the queue is empty
    if (Utils::containerContains(vkPresentModes, VK_PRESENT_MODE_FIFO_RELAXED_KHR)) {
        mPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        return;
    }

    // Failing that just return whatever (will probably be just VK_PRESENT_MODE_IMMEDIATE_KHR)
    // I'd imagine we'll never hit this point in practice though!
    mPresentMode = vkPresentModes.front();
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Chooses the size of the swap area
//------------------------------------------------------------------------------------------------------------------------------------------
bool Swapchain::chooseSwapExtent() noexcept {
    // Sanity checks
    ASSERT(mpDevice);
    ASSERT(mpDevice->getScreenFramebufferMgr().isValid());
    ASSERT(mpDevice->getWindowSurface() && mpDevice->getWindowSurface()->isValid());

    // Get the Vulkan surface capabilities
    ScreenFramebufferMgr& framebufferMgr = mpDevice->getScreenFramebufferMgr();
    const DeviceSurfaceCaps& deviceSurfaceCaps = framebufferMgr.getDeviceSurfaceCaps();
    const VkSurfaceCapabilitiesKHR& vkSurfaceCaps = deviceSurfaceCaps.getVkSurfaceCapabilities();

    // Get the current size of the window
    SDL_Window* const pSdlWindow = mpDevice->getWindowSurface()->getSdlWindow();
    ASSERT(pSdlWindow);
    
    int windowW = {};
    int windowH = {};
    SDL_GetWindowSize(pSdlWindow, &windowW, &windowH);

    if ((windowW < 0) || (windowH < 0)) {
        ASSERT_FAIL("Invalid window size obtained from SDL!");
        return false;
    }

    // Get the max image size for the framebuffer
    const uint32_t maxImageExtentW = vkSurfaceCaps.maxImageExtent.width;
    const uint32_t maxImageExtentH = vkSurfaceCaps.maxImageExtent.height;
        
    // Note: If the width/height is set to UINT32_MAX then we may differ from the resolution of the window.
    // Handle the width case first...
    if (vkSurfaceCaps.currentExtent.width == UINT32_MAX) {
        mSwapExtentW = vkSurfaceCaps.currentExtent.width;
    } else {
        mSwapExtentW = std::clamp((uint32_t) windowW, vkSurfaceCaps.minImageExtent.width, maxImageExtentW);
    }

    if (vkSurfaceCaps.currentExtent.height == UINT32_MAX) {
        mSwapExtentH = vkSurfaceCaps.currentExtent.height;
    } else {
        mSwapExtentH = std::clamp((uint32_t) windowH, vkSurfaceCaps.minImageExtent.height, maxImageExtentH);
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Choose how many images to use in the swap chain
//------------------------------------------------------------------------------------------------------------------------------------------
void Swapchain::chooseSwapchainLength() noexcept {
    // Sanity checks
    ASSERT(mpDevice);
    ASSERT(mpDevice->getScreenFramebufferMgr().isValid());

    // Get the Vulkan surface capabilities
    ScreenFramebufferMgr& framebufferMgr = mpDevice->getScreenFramebufferMgr();
    const DeviceSurfaceCaps& deviceSurfaceCaps = framebufferMgr.getDeviceSurfaceCaps();
    const VkSurfaceCapabilitiesKHR& vkSurfaceCaps = deviceSurfaceCaps.getVkSurfaceCapabilities();

    // Try to choose the max swap chain length allowed by the engine unless we are limited otherwise.
    // Note that '0' for max image count means no limit.
    mLength = Defines::MAX_SWAP_CHAIN_LENGTH;

    if (vkSurfaceCaps.maxImageCount != 0) {
        mLength = std::min(mLength, vkSurfaceCaps.maxImageCount);
    }

    // Make sure we respect the minimum also
    mLength = std::max(mLength, vkSurfaceCaps.minImageCount);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Creates the actual swap chain and returns 'true' on success
//------------------------------------------------------------------------------------------------------------------------------------------
bool Swapchain::createSwapchain() noexcept {
    // Sanity checks
    ASSERT(!mVkSwapchain);
    ASSERT(mpDevice);
    ASSERT(mpDevice->getWindowSurface());
    ASSERT(mpDevice->getScreenFramebufferMgr().isValid());

    // Get the Vulkan surface capabilities
    ScreenFramebufferMgr& framebufferMgr = mpDevice->getScreenFramebufferMgr();
    const DeviceSurfaceCaps& deviceSurfaceCaps = framebufferMgr.getDeviceSurfaceCaps();
    const VkSurfaceCapabilitiesKHR& vkSurfaceCaps = deviceSurfaceCaps.getVkSurfaceCapabilities();

    // Start filling in the create info structure for making the swap chain
    VkSwapchainCreateInfoKHR createInfo = {};

    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = mpDevice->getWindowSurface()->getVkSurface();
    createInfo.minImageCount = mLength;
    createInfo.imageFormat = mSurfaceFormat.format;
    createInfo.imageColorSpace = mSurfaceFormat.colorSpace;
    createInfo.imageExtent = { mSwapExtentW, mSwapExtentH };
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;      // Transfer destination so we can blit
    createInfo.preTransform =  vkSurfaceCaps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = mPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // Specify how the images in the swap chain are shared across different queues.
    // If the present and work queue are the same then use the exclusive mode, which offers better performance.
    // Otherwise, Vulkan needs to synchronize access to the images...
    const uint32_t queueIndices[] = { mpDevice->getPresentationQueueFamilyIdx(), mpDevice->getWorkQueueFamilyIdx() };

    if (mpDevice->getPresentationQueue() == mpDevice->getWorkQueue()) {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 1;
        createInfo.pQueueFamilyIndices = queueIndices;
    } else {   
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueIndices;
    }

    // Now create the actual swap chain
    const VkFuncs& vkFuncs = mpDevice->getVkFuncs();
    ASSERT(mpDevice->getVkDevice());

    if (vkFuncs.vkCreateSwapchainKHR(mpDevice->getVkDevice(), &createInfo, nullptr, &mVkSwapchain) != VK_SUCCESS) {
        ASSERT_FAIL("Failed to create the swap chain!");
        return false;
    }

    ASSERT(mVkSwapchain);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Gathers all of the images involved in the swap chain and creates image views for each of them
//------------------------------------------------------------------------------------------------------------------------------------------
bool Swapchain::createSwapchainImageViews() noexcept {
    // Sanity checks
    ASSERT(mVkImages.empty());
    ASSERT(mVkImageViews.empty());

    // Get the image count firstly
    ASSERT(mpDevice);
    const VkDevice vkDevice = mpDevice->getVkDevice();
    const VkFuncs& vkFuncs = mpDevice->getVkFuncs();

    uint32_t imageCount = 0;

    if (vkFuncs.vkGetSwapchainImagesKHR(vkDevice, mVkSwapchain, &imageCount, nullptr) != VK_SUCCESS) {
        ASSERT_FAIL("Failed to get the number of images in the swap chain!");
        return false;
    }

    // Get the images themselves
    mVkImages.resize(imageCount);

    if (vkFuncs.vkGetSwapchainImagesKHR(vkDevice, mVkSwapchain, &imageCount, mVkImages.data()) != VK_SUCCESS) {
        ASSERT_FAIL("Failed to get the images in the swap chain!");
        return false;
    }

    // Expect no null images in the swap chain
    if (Utils::containerContains(mVkImages, nullptr)) {
        ASSERT_FAIL("Expected no null images in the swap chain image list!");
        return false;
    }

    // Re-use this struct for multiple image views
    VkImageViewCreateInfo ivCreateInfo = {};
    ivCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCreateInfo.format = mSurfaceFormat.format;
    ivCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;              // Don't swap any components
    ivCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    ivCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;   // Image is just a color target
    ivCreateInfo.subresourceRange.baseMipLevel = 0;
    ivCreateInfo.subresourceRange.levelCount = 1;       // Just 1 mip level
    ivCreateInfo.subresourceRange.baseArrayLayer = 0;
    ivCreateInfo.subresourceRange.layerCount = 1;       // 2 layers would be stereoscopic rendering

    // Create all the image views for images in the swap chain
    mVkImageViews.resize(imageCount);

    for (size_t i = 0; i < imageCount; ++i) {
        ivCreateInfo.image = mVkImages[i];

        if (vkFuncs.vkCreateImageView(vkDevice, &ivCreateInfo, nullptr, &mVkImageViews[i]) != VK_SUCCESS) {
            ASSERT_FAIL("Failed to create a swap chain image view!");
            return false;
        }

        ASSERT(mVkImageViews[i]);
    }

    // If we got to here then all went well
    return true;
}

END_NAMESPACE(vgl)