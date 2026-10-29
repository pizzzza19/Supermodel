/* Real3D GPU data shared by Vulkan and Metal. GPL-3.0-or-later.
 * Derived from Supermodel-VK, Copyright 2026 The Supermodel-VK authors. */
#ifndef INCLUDED_R3D_SHARED_H
#define INCLUDED_R3D_SHARED_H
#include <cstdint>
namespace Vulkan3D {
constexpr uint32_t kMaxRomVerts = 6000000;

enum MaterialFlag : uint32_t
{
	kMatTextured		= 1u << 0,
	kMatAlphaTest		= 1u << 1,
	kMatTextureAlpha	= 1u << 2,
	kMatLighting		= 1u << 3,
	kMatFixedShade		= 1u << 4,
	kMatSpecular		= 1u << 5,
	kMatTranslator		= 1u << 6,
	kMatInverted		= 1u << 7,
	kMatSmooth			= 1u << 8,
	kMatPolyAlpha		= 1u << 9,
	kMatMicro			= 1u << 10
};

// std430, 64 bytes. Keep in sync with real3d.frag.
//
// This mirrors the uniform set New3D feeds its fragment shader. The texture is
// addressed exactly like the real hardware: baseTexInfo is a rectangle inside
// one of the two packed 2048x1024 texture pages (which also contain the packed
// mip pyramid), and the shader does texelFetch maths on it.
struct R3DMaterial
{
	uint32_t flags;
	int32_t  texX;             // baseTexInfo.x (page pixels, includes translator offset)
	int32_t  texY;             // baseTexInfo.y
	int32_t  texWidth;         // baseTexInfo.z (texels)
	int32_t  texHeight;        // baseTexInfo.w (texels)
	int32_t  texFormat;        // 0..11, see ExtractColour
	int32_t  texturePage;      // 0 or 1
	int32_t  wrapModeX;        // Mesh::TexWrapMode
	int32_t  wrapModeY;        // Mesh::TexWrapMode
	int32_t  microTextureID;   // 0..7
	float    microMinLOD;
	float    shininess;
	float    specularValue;
	float    fogIntensity;
	float    pad0;
	float    pad1;
};

// 64-byte vertex. The first 48 bytes match the planned layout; faceNormal is
// required so back-face rejection can use the polygon normal (flat shading
// stores that normal in "normal" as well, smooth shading does not).
struct R3DVertex
{
	float pos[3];
	float normal[3];
	float uv[2];
	uint32_t colour;
	uint32_t materialFlags;
	float fixedShade;
	float textureNP;
	float faceNormal[3];
	float pad;
};

// std140 push constants, 208 bytes.
// extra.x = cota, extra.y = modelScale, extra.z = discardAlpha (0 or 1),
// extra.w = material index bitcast to float.
struct R3DPush
{
	float modelView[16];
	float projection[16];
	float lightDir[4];
	float lightParams[4];
	float fogColour[4];
	float fogParams[4];
	float extra[4];
};

// std140 viewport uniform (spotlight). Dynamic offset into the ring buffer.
struct ViewportUBO
{
	float spotEllipse[4];
	float spotRange[4];
	float spotColor[4];
	float spotFogColor[4];
};

// std140 push constants for the ambient / scroll fog background pass. 64 bytes.
struct R3DFogPush
{
	float fogColour[4];			// rgb = fog colour, a = scroll fog amount
	float fogAmbientAtt[4];		// x = fog ambient, y = fog attenuation, zw unused
	float spotEllipse[4];		// spotlight ellipse: xy = centre, zw = size
	float spotFogColor[3];		// spotlight on fog colour
	float pad;
};

static_assert(sizeof(R3DMaterial) == 64, "R3DMaterial size");
static_assert(sizeof(R3DVertex) == 64, "R3DVertex size");
static_assert(sizeof(R3DPush) == 208, "R3DPush size");
static_assert(sizeof(ViewportUBO) == 64, "ViewportUBO size");
static_assert(sizeof(R3DFogPush) == 64, "R3DFogPush size");

}
#endif
