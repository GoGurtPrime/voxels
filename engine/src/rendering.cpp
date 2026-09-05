/**
 * @file rendering.cpp
 * @brief Translation unit anchor for the backend-neutral runtime renderer contract.
 */

#include "voxels/graphics/renderer.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels::graphics {

std::string_view RendererBackendName(RendererBackend backend) noexcept {
	switch (backend) {
		case RendererBackend::Automatic: return "Automatic";
		case RendererBackend::OpenGL: return "OpenGL 3.3 Core";
		case RendererBackend::Direct3D11: return "Direct3D 11";
	}
	return "Unknown";
}

std::vector<RendererBackend> AvailableRendererBackends(PlatformType platform) {
	std::vector<RendererBackend> backends;
#if defined(VOXELS_HAS_DX11)
	if (platform == PlatformType::Windows) backends.push_back(RendererBackend::Direct3D11);
#else
	(void)platform;
#endif
	if (platform != PlatformType::Dreamcast) backends.push_back(RendererBackend::OpenGL);
	return backends;
}

} // namespace voxels::graphics
