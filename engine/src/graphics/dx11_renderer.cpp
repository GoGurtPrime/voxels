/**
 * @file dx11_renderer.cpp
 * @brief Direct3D 11 device, swap-chain, frame lifecycle, resize, and screenshot implementation.
 */

#if defined(_WIN32)

#include "voxels/graphics/dx11_renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <SDL.h>
#include <SDL_syswm.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "voxels/assets/texture_loader.hpp"
#include "voxels/core/logger.hpp"
#include "voxels/platform/platform.hpp"

namespace voxels::graphics {
namespace {
using Microsoft::WRL::ComPtr;

HWND GetWindowHandle(IPlatform& platform) {
    auto* window = static_cast<SDL_Window*>(platform.GetNativeWindowHandle());
    if (window == nullptr) return nullptr;
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE) return nullptr;
    return info.info.win.window;
}
} // namespace

class DX11Renderer::Impl {
public:
    bool CreateTargets(int requestedWidth, int requestedHeight) {
        if (!swapChain || !device || !context) return false;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        renderTarget.Reset();
        depthView.Reset();
        depthTexture.Reset();

        const UINT width = static_cast<UINT>(std::max(requestedWidth, 1));
        const UINT height = static_cast<UINT>(std::max(requestedHeight, 1));
        if (FAILED(swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) return false;

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
            FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget))) {
            return false;
        }

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, &depthTexture)) ||
            FAILED(device->CreateDepthStencilView(depthTexture.Get(), nullptr, &depthView))) {
            return false;
        }
        widthPixels = static_cast<int>(width);
        heightPixels = static_cast<int>(height);
        return true;
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11Texture2D> depthTexture;
    ComPtr<ID3D11DepthStencilView> depthView;
    int widthPixels = 1280;
    int heightPixels = 720;
    bool vSync = true;
    bool initialized = false;
};

DX11Renderer::DX11Renderer() : m_impl(std::make_unique<Impl>()) {}
DX11Renderer::~DX11Renderer() { Shutdown(); }

bool DX11Renderer::Initialize(IPlatform& platform, TextureAtlas&, bool vSync) {
    if (m_impl->initialized) return true;
    const HWND window = GetWindowHandle(platform);
    if (window == nullptr) return false;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL acquired{};
    HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       requested, static_cast<UINT>(std::size(requested)),
                                       D3D11_SDK_VERSION, &m_impl->device, &acquired, &m_impl->context);
#if defined(_DEBUG)
    if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   requested, static_cast<UINT>(std::size(requested)),
                                   D3D11_SDK_VERSION, &m_impl->device, &acquired, &m_impl->context);
    }
#endif
    if (FAILED(result) || acquired < D3D_FEATURE_LEVEL_11_0) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(m_impl->device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        Shutdown();
        return false;
    }

    const auto [width, height] = platform.GetDrawableSize();
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = static_cast<UINT>(std::max(width, 1));
    swapDesc.Height = static_cast<UINT>(std::max(height, 1));
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(m_impl->device.Get(), window, &swapDesc, nullptr,
                                               nullptr, &m_impl->swapChain))) {
        Shutdown();
        return false;
    }
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    m_impl->vSync = vSync;
    if (!m_impl->CreateTargets(width, height)) {
        Shutdown();
        return false;
    }
    m_impl->initialized = true;
    Logger logger;
    logger.Info("Direct3D 11 renderer initialized at feature level " +
                std::to_string(static_cast<unsigned int>(acquired)) + ".");
    return true;
}

void DX11Renderer::Shutdown() {
    if (!m_impl) return;
    if (m_impl->context) {
        m_impl->context->ClearState();
        m_impl->context->Flush();
    }
    m_impl->depthView.Reset();
    m_impl->depthTexture.Reset();
    m_impl->renderTarget.Reset();
    m_impl->swapChain.Reset();
    m_impl->context.Reset();
    m_impl->device.Reset();
    m_impl->initialized = false;
}

bool DX11Renderer::BeginFrame(const std::array<float, 4>& clearColor) {
    if (!m_impl->initialized || !m_impl->renderTarget || !m_impl->depthView) return false;
    ID3D11RenderTargetView* target = m_impl->renderTarget.Get();
    m_impl->context->OMSetRenderTargets(1, &target, m_impl->depthView.Get());
    m_impl->context->ClearRenderTargetView(target, clearColor.data());
    m_impl->context->ClearDepthStencilView(m_impl->depthView.Get(),
                                           D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_impl->widthPixels);
    viewport.Height = static_cast<float>(m_impl->heightPixels);
    viewport.MaxDepth = 1.0f;
    m_impl->context->RSSetViewports(1, &viewport);
    return true;
}

bool DX11Renderer::EndFrame() { return m_impl->initialized; }

bool DX11Renderer::Present() {
    if (!m_impl->initialized || !m_impl->swapChain) return false;
    const HRESULT result = m_impl->swapChain->Present(m_impl->vSync ? 1U : 0U, 0);
    return result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET && SUCCEEDED(result);
}

void DX11Renderer::SetViewport(int width, int height) {
    if (!m_impl->initialized || width <= 0 || height <= 0 ||
        (width == m_impl->widthPixels && height == m_impl->heightPixels)) {
        return;
    }
    if (!m_impl->CreateTargets(width, height)) {
        Logger logger;
        logger.Error("Direct3D 11 swap-chain resize failed.");
    }
}

bool DX11Renderer::CaptureScreenshot(const std::filesystem::path& path) const {
    if (!m_impl->initialized || !m_impl->swapChain) return false;
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_impl->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    D3D11_TEXTURE2D_DESC description{};
    backBuffer->GetDesc(&description);
    description.BindFlags = 0;
    description.MiscFlags = 0;
    description.Usage = D3D11_USAGE_STAGING;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(m_impl->device->CreateTexture2D(&description, nullptr, &staging))) return false;
    m_impl->context->CopyResource(staging.Get(), backBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_impl->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(description.Width) * description.Height * 4U);
    for (UINT y = 0; y < description.Height; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        auto* destination = rgba.data() + static_cast<std::size_t>(y) * description.Width * 4U;
        for (UINT x = 0; x < description.Width; ++x) {
            destination[x * 4U] = source[x * 4U + 2U];
            destination[x * 4U + 1U] = source[x * 4U + 1U];
            destination[x * 4U + 2U] = source[x * 4U];
            destination[x * 4U + 3U] = source[x * 4U + 3U];
        }
    }
    m_impl->context->Unmap(staging.Get(), 0);
    return TextureLoader::WritePngToFile(path, static_cast<int>(description.Width),
                                         static_cast<int>(description.Height), 4, rgba.data());
}

void* DX11Renderer::GetNativeDevice() const noexcept { return m_impl->device.Get(); }
void* DX11Renderer::GetNativeContext() const noexcept { return m_impl->context.Get(); }

} // namespace voxels::graphics

#endif
