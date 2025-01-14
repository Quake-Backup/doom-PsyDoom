//------------------------------------------------------------------------------------------------------------------------------------------
// A module that handles submitting primitives to draw the sky
//------------------------------------------------------------------------------------------------------------------------------------------
#include "rv_sky.h"

#if PSYDOOM_VULKAN_RENDERER

#include "Asserts.h"
#include "Doom/Base/i_main.h"
#include "Doom/Base/w_wad.h"
#include "Doom/Game/doomdata.h"
#include "Doom/Renderer/r_data.h"
#include "Doom/Renderer/r_sky.h"
#include "Gpu.h"
#include "PsyDoom/Vulkan/VDrawing.h"
#include "PsyDoom/Vulkan/VRenderer.h"
#include "PsyDoom/Vulkan/VTypes.h"
#include "PsyQ/LIBGPU.h"
#include "rv_main.h"
#include "rv_utils.h"

//------------------------------------------------------------------------------------------------------------------------------------------
// Get the texture parameters for the sky texture (texture window and CLUT position).
// Note that 8-bit color is always assumed.
//------------------------------------------------------------------------------------------------------------------------------------------
static void RV_GetSkyTexParams(
    uint16_t& texWinX,
    uint16_t& texWinY,
    uint16_t& texWinW,
    uint16_t& texWinH,
    uint16_t& clutX,
    uint16_t& clutY
) noexcept {
    texture_t& skytex = *gpSkyTexture;

    // Get the texture window location
    Gpu::TexFmt texFmt = {};
    Gpu::BlendMode blendMode = {};
    RV_TexPageIdToTexParams(skytex.texPageId, texFmt, texWinX, texWinY, blendMode);
    ASSERT(texFmt == Gpu::TexFmt::Bpp8);

    // Set the texture window size and position
    texWinX += skytex.texPageCoordX;
    texWinY += skytex.texPageCoordY;
    texWinW = skytex.width;
    texWinH = skytex.height;

    // Get the CLUT location
    RV_ClutIdToClutXy(gPaletteClutId_CurMapSky, clutX, clutY);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Computes the current 'U' texture coordinate offset for the sky based on player rotation
//------------------------------------------------------------------------------------------------------------------------------------------
static float RV_GetSkyUCoordOffset() noexcept {
    // One full revolution is equal to 1024 texel units.
    // When the sky texture is 256 pixels wide, this means 4 wrappings.
    // Note: have to add back on 90 degrees because the float viewing angle is adjusted to -90 degrees of the fixed point angle.
    const float rotatePercent = -(gViewAnglef + RV_PI_2<float>) * (1.0f / RV_2PI<float>);
    return rotatePercent * 1024;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Uploads the latest frame of the sky texture to VRAM if required; should be called at least once a frame.
// Won't do any work for normal skies since they are always precached after level start, but should do work periodically for the fire sky.
//------------------------------------------------------------------------------------------------------------------------------------------
void RV_CacheSkyTex() noexcept {
    // Texture already up to date in VRAM?
    texture_t& skyTex = *gpSkyTexture;

    if (skyTex.uploadFrameNum != TEX_INVALID_UPLOAD_FRAME_NUM)
        return;

    // Need to upload the texture to VRAM, do that now and also ensure texture metrics are up-to-date
    const WadLump& skyTexLump = W_GetLump(skyTex.lumpNum);
    const std::byte* const pLumpData = (const std::byte*) skyTexLump.pCachedData;
    const uint16_t* const pTexData = (const std::uint16_t*)(pLumpData + sizeof(texlump_header_t));

    R_UpdateTexMetricsFromData(skyTex, pLumpData, skyTexLump.uncompressedSize);

    SRECT vramRect = getTextureVramRect(skyTex);
    LIBGPU_LoadImage(vramRect, pTexData);
    skyTex.uploadFrameNum = gNumFramesDrawn;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Draws a background sky which covers the screen.
// This sky is rendered before anything else, so it is completely a background layer.
// This is needed for some custom maps (in the GEC master edition) because they rely on being able to see through 1 sided walls.
// Those 1 sided walls can be seen through because they are masked to be fully transparent.
//------------------------------------------------------------------------------------------------------------------------------------------
void RV_DrawBackgroundSky() noexcept {
    // Use an identity transform matrix for drawing this sky quad
    VShaderUniforms_Draw uniforms = {};
    VRenderer::initRendererUniformFields(uniforms);
    uniforms.mvpMatrix = Matrix4f::IDENTITY();

    VDrawing::setDrawUniforms(uniforms);

    // Set the correct draw pipeline
    VDrawing::setDrawPipeline(VPipelineType::World_Sky);

    // Get the basic texture params for the sky
    uint16_t texWinX, texWinY;
    uint16_t texWinW, texWinH;
    uint16_t clutX, clutY;
    RV_GetSkyTexParams(texWinX, texWinY, texWinW, texWinH, clutX, clutY);

    // Get the sky 'U' texture coordinate and add the sky triangle
    const float uOffset = RV_GetSkyUCoordOffset();

    // Submit the quad
    VDrawing::addWorldSkyQuad(
        -1.0f, -1.0f, 0.0f,
        +1.0f, -1.0f, 0.0f,
        +1.0f, +1.0f, 0.0f,
        -1.0f, +1.0f, 0.0f,
        uOffset,
        clutX, clutY,
        texWinX, texWinY,
        texWinW, texWinH
    );
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Add a vertical wall for where the sky should be rendered, stretched past the top or bottom of the screen. The xz endpoints of the wall
// are specified along with the y coordinate for where the sky starts, and whether the wall is an upper or lower sky wall.
//------------------------------------------------------------------------------------------------------------------------------------------
void RV_AddInfiniteSkyWall(
    const float x1,
    const float z1,
    const float x2,
    const float z2,
    const float y,
    const bool bIsUpperSkyWall
) noexcept {
    // Get the basic texture params for the sky
    uint16_t texWinX, texWinY;
    uint16_t texWinW, texWinH;
    uint16_t clutX, clutY;
    RV_GetSkyTexParams(texWinX, texWinY, texWinW, texWinH, clutX, clutY);

    // Get the sky 'U' texture coordinate and add the sky triangle
    const float uOffset = RV_GetSkyUCoordOffset();

    // Ensure the correct draw pipeline is set and add the wall
    VDrawing::setDrawPipeline(VPipelineType::World_Sky);
    VDrawing::addWorldInfiniteSkyWall(x1, z1, x2, z2, y, bIsUpperSkyWall, uOffset, clutX, clutY, texWinX, texWinY, texWinW, texWinH);
}

#endif  // #if PSYDOOM_VULKAN_RENDERER
