#include "pch.h"
#include "Application.h"
#include "Camera.h"
#include "Graphics.h"

Graphics::Graphics() :
	m_outputWidth(800),
	m_outputHeight(600),
	m_featureLevel(D3D_FEATURE_LEVEL_9_1)
{
}


Graphics::~Graphics()
{
}

// Helper method to clear the back buffers.
void Graphics::Clear()
{
	using namespace DirectX;
	// Clear the views.
	m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), Colors::Black);
	m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

	// Set the viewport.
	CD3D11_VIEWPORT viewport(0.0f, 0.0f, static_cast<float>(m_outputWidth), static_cast<float>(m_outputHeight));
	m_d3dContext->RSSetViewports(1, &viewport);
}

// These are the resources that depend on the device.
void Graphics::CreateDevice()
{
	using Microsoft::WRL::ComPtr;
    UINT creationFlags = 0;

#ifdef _DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    static const D3D_FEATURE_LEVEL featureLevels [] =
    {
        // TODO: Modify for supported Direct3D feature levels (see code below related to 11.1 fallback handling).
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };

    // Create the DX11 API device object, and get a corresponding context.
    HRESULT hr = D3D11CreateDevice(
        nullptr,                                // specify nullptr to use the default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        m_d3dDevice.ReleaseAndGetAddressOf(),   // returns the Direct3D device created
        &m_featureLevel,                        // returns feature level of device created
        m_d3dContext.ReleaseAndGetAddressOf()   // returns the device immediate context
        );

    if (hr == E_INVALIDARG)
    {
        // DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it.
        hr = D3D11CreateDevice(nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            creationFlags,
            &featureLevels[1],
            _countof(featureLevels) - 1,
            D3D11_SDK_VERSION,
            m_d3dDevice.ReleaseAndGetAddressOf(),
            &m_featureLevel,
            m_d3dContext.ReleaseAndGetAddressOf()
            );
    }

    DX::ThrowIfFailed(hr);

#ifndef NDEBUG
    ComPtr<ID3D11Debug> d3dDebug;
    if (SUCCEEDED(m_d3dDevice.As(&d3dDebug)))
    {
        ComPtr<ID3D11InfoQueue> d3dInfoQueue;
        if (SUCCEEDED(d3dDebug.As(&d3dInfoQueue)))
        {
#ifdef _DEBUG
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
#endif
            D3D11_MESSAGE_ID hide [] =
            {
                D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
                // TODO: Add more message IDs here as needed.
            };
            D3D11_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
        }
    }
#endif

    // DirectX 11.1 if present
    if (SUCCEEDED(m_d3dDevice.As(&m_d3dDevice1)))
        (void)m_d3dContext.As(&m_d3dContext1);

    // TODO: Initialize device dependent objects here (independent of window size).
}


// Allocate all memory resources that change on a window SizeChanged event.
void Graphics::CreateResources()
{
	using Microsoft::WRL::ComPtr;

    // Clear the previous window size specific context.
    ID3D11RenderTargetView* nullViews [] = { nullptr };
    m_d3dContext->OMSetRenderTargets(_countof(nullViews), nullViews, nullptr);
    m_renderTargetView.Reset();
    m_depthStencilView.Reset();
    m_d3dContext->Flush();

    UINT backBufferWidth = static_cast<UINT>(m_outputWidth);
    UINT backBufferHeight = static_cast<UINT>(m_outputHeight);
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    UINT backBufferCount = 2;
	HWND hWindow = Application::Instance().GetWindowHandle();

	if(!hWindow)
	{
		/* Window hasn't been created yet */
		return;
	}

    // If the swap chain already exists, resize it, otherwise create one.
    if (m_swapChain)
    {
        HRESULT hr = m_swapChain->ResizeBuffers(backBufferCount, backBufferWidth, backBufferHeight, backBufferFormat, 0);

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            // If the device was removed for any reason, a new device and swap chain will need to be created.
            Reset();

            // Everything is set up now. Do not continue execution of this method. Reset will reenter this method 
            // and correctly set up the new device.
            return;
        }
        else
        {
            DX::ThrowIfFailed(hr);
        }
    }
    else
    {
        // First, retrieve the underlying DXGI Device from the D3D Device.
        ComPtr<IDXGIDevice1> dxgiDevice;
        DX::ThrowIfFailed(m_d3dDevice.As(&dxgiDevice));

        // Identify the physical adapter (GPU or card) this device is running on.
        ComPtr<IDXGIAdapter> dxgiAdapter;
        DX::ThrowIfFailed(dxgiDevice->GetAdapter(dxgiAdapter.GetAddressOf()));

        // And obtain the factory object that created it.
        ComPtr<IDXGIFactory1> dxgiFactory;
        DX::ThrowIfFailed(dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory.GetAddressOf())));

        ComPtr<IDXGIFactory2> dxgiFactory2;
        if (SUCCEEDED(dxgiFactory.As(&dxgiFactory2)))
        {
            // DirectX 11.1 or later

            // Create a descriptor for the swap chain.
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
            swapChainDesc.Width = backBufferWidth;
            swapChainDesc.Height = backBufferHeight;
            swapChainDesc.Format = backBufferFormat;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.BufferCount = backBufferCount;

            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = { 0 };
            fsSwapChainDesc.Windowed = TRUE;

            // Create a SwapChain from a Win32 window.
            DX::ThrowIfFailed(dxgiFactory2->CreateSwapChainForHwnd(
                m_d3dDevice.Get(),
				hWindow,
                &swapChainDesc,
                &fsSwapChainDesc,
                nullptr,
                m_swapChain1.ReleaseAndGetAddressOf()
                ));

            DX::ThrowIfFailed(m_swapChain1.As(&m_swapChain));
        }
        else
        {
            DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
            swapChainDesc.BufferCount = backBufferCount;
            swapChainDesc.BufferDesc.Width = backBufferWidth;
            swapChainDesc.BufferDesc.Height = backBufferHeight;
            swapChainDesc.BufferDesc.Format = backBufferFormat;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow = hWindow;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.Windowed = TRUE;

            DX::ThrowIfFailed(dxgiFactory->CreateSwapChain(m_d3dDevice.Get(), &swapChainDesc, m_swapChain.ReleaseAndGetAddressOf()));
        }

        // This template does not support exclusive fullscreen mode and prevents DXGI from responding to the ALT+ENTER shortcut.
		DX::ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hWindow, DXGI_MWA_NO_ALT_ENTER));
    }

    // Obtain the backbuffer for this window which will be the final 3D rendertarget.
    ComPtr<ID3D11Texture2D> backBuffer;
    DX::ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())));

    // Create a view interface on the rendertarget to use on bind.
    DX::ThrowIfFailed(m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, m_renderTargetView.ReleaseAndGetAddressOf()));

    // Allocate a 2-D surface as the depth/stencil buffer and
    // create a DepthStencil view on this surface to use on bind.
    CD3D11_TEXTURE2D_DESC depthStencilDesc(depthBufferFormat, backBufferWidth, backBufferHeight, 1, 1, D3D11_BIND_DEPTH_STENCIL);

    ComPtr<ID3D11Texture2D> depthStencil;
    DX::ThrowIfFailed(m_d3dDevice->CreateTexture2D(&depthStencilDesc, nullptr, depthStencil.GetAddressOf()));

    CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
    DX::ThrowIfFailed(m_d3dDevice->CreateDepthStencilView(depthStencil.Get(), &depthStencilViewDesc, m_depthStencilView.ReleaseAndGetAddressOf()));

    // TODO: Initialize windows-size dependent objects here.
}

Microsoft::WRL::ComPtr<ID3D11DeviceContext> Graphics::GetContext()
{
	return(m_d3dContext);
}

Microsoft::WRL::ComPtr<ID3D11Device> Graphics::GetDevice()
{
	return(m_d3dDevice);
}

IEffectFactory & Graphics::GetEffectsFactory()
{
	return(*m_effects_factory);
}

CommonStates & Graphics::GetRenderStates()
{
	return(*m_render_states);
}

void Graphics::PopRaster()
{
	assert(m_raster_stack.size());
	m_raster_desc = m_raster_stack.top();
	m_raster_stack.pop();
}

// Presents the back buffer contents to the screen.
void Graphics::Present()
{
	// The first argument instructs DXGI to block until VSync, putting the application
	// to sleep until the next VSync. This ensures we don't waste any cycles rendering
	// frames that will never be displayed to the screen.
	HRESULT hr = m_swapChain->Present(1, 0);

	// If the device was reset we must completely reinitialize the renderer.
	if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		Reset();
	}
	else
	{
		DX::ThrowIfFailed(hr);
	}
}

void Graphics::PushRaster()
{
	m_raster_stack.push(m_raster_desc);
}

void Graphics::Reset()
{
	Shutdown();
	Startup();
}

void Graphics::Resize(int width, int height)
{
	m_outputWidth  = std::max(width,  1);
	m_outputHeight = std::max(height, 1);

	CreateResources();

	Camera::Instance().SetAspect((float)m_outputWidth / (float)m_outputHeight);

}

void XM_CALLCONV Graphics::SetAmbientLightColor(FXMVECTOR value)
{
	m_light_color_ambient = SimpleMath::Vector4(value);
}

void XM_CALLCONV Graphics::SetFogColor(FXMVECTOR value)
{
	m_fog_color = SimpleMath::Vector4(value);
}

void __cdecl Graphics::SetFogEnabled(bool value)
{
	m_enable_fog = value;
}

void __cdecl Graphics::SetFogEnd(float value)
{
	m_fog_planes.second = value;
}

void __cdecl Graphics::SetFogStart(float value)
{
	m_fog_planes.first = value;
}

void XM_CALLCONV Graphics::SetLightDiffuseColor(int whichLight, FXMVECTOR value)
{
	m_direction_lights[whichLight].diffuse = SimpleMath::Vector4(value);
}

void XM_CALLCONV Graphics::SetLightDirection(int whichLight, FXMVECTOR value)
{
	m_direction_lights[whichLight].direction = SimpleMath::Vector4(value);
}

void XM_CALLCONV Graphics::SetLightSpecularColor(int whichLight, FXMVECTOR value)
{
	m_direction_lights[whichLight].specular = SimpleMath::Vector4(value);
}

void __cdecl Graphics::SetLightEnabled(int whichLight, bool value)
{
	m_direction_lights[whichLight].enable = value;

}

void __cdecl Graphics::SetLightingEnabled(bool value)
{
	m_enable_lighting = value;
}

void XM_CALLCONV Graphics::SetMatrices(FXMMATRIX world, CXMMATRIX view, CXMMATRIX projection)
{
	SetWorld(world);
	SetView(view);
	SetProjection(projection);
}

void __cdecl Graphics::SetPerPixelLighting(bool value)
{
	m_enable_pixel_lighting = value;
}

void XM_CALLCONV Graphics::SetProjection(FXMMATRIX value)
{
	m_mat_projection = SimpleMath::Matrix(value);
}

void XM_CALLCONV Graphics::SetView(FXMMATRIX value)
{
	m_mat_view = SimpleMath::Matrix(value);
}

void XM_CALLCONV Graphics::SetWorld(FXMMATRIX value)
{
	m_mat_world = SimpleMath::Matrix(value);
}

void Graphics::Shutdown()
{
	// TODO: Add Direct3D resource cleanup here.
	m_effects_factory.reset();
	m_render_states.reset();
	m_raster.Reset();
	m_depthStencilView.Reset();
	m_renderTargetView.Reset();
	m_swapChain1.Reset();
	m_swapChain.Reset();
	m_d3dContext1.Reset();
	m_d3dContext.Reset();
	m_d3dDevice1.Reset();
	m_d3dDevice.Reset();
}

void Graphics::Startup()
{
	CreateDevice();
	CreateResources();

	m_direction_lights.reserve(IEffectLights::MaxDirectionalLights);

	m_effects_factory = std::make_unique<EffectFactory>(m_d3dDevice.Get());
	m_render_states =   std::make_unique<CommonStates>(m_d3dDevice.Get());

	m_raster_desc.FillMode              = D3D11_FILL_WIREFRAME;
	m_raster_desc.CullMode              = D3D11_CULL_NONE;
	m_raster_desc.FrontCounterClockwise = FALSE;
	m_raster_desc.DepthBias             = D3D11_DEFAULT_DEPTH_BIAS;
	m_raster_desc.DepthBiasClamp        = D3D11_DEFAULT_DEPTH_BIAS_CLAMP;
	m_raster_desc.SlopeScaledDepthBias  = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	m_raster_desc.DepthClipEnable       = TRUE;
	m_raster_desc.ScissorEnable         = FALSE;
	m_raster_desc.MultisampleEnable     = FALSE;
	m_raster_desc.AntialiasedLineEnable = FALSE;

	UpdateRaster(); 

	Camera::Instance().SetFOV(Utils::DegreesToRadians(70.0f));

}

// Effects update callback procedure
void CALLBACK Graphics::UpdateEffects(IEffect *effect)
{
	Graphics::Instance().__UpdateEffects(effect);
}

// Effects update callback procedure
void Graphics::__UpdateEffects(IEffect *effect)
{
	/* lighting */
	auto lights = dynamic_cast<IEffectLights*>(effect);
	if(lights)
	{
		lights->SetLightingEnabled(m_enable_lighting);
		lights->SetPerPixelLighting(m_enable_pixel_lighting);
		lights->SetAmbientLightColor(m_light_color_ambient);

		for(auto i = 0; i < (int)m_direction_lights.size(); i++)
		{
			auto &dir = m_direction_lights[i];
			//lights->SetLightEnabled(      i, dir.enable);
			//lights->SetLightDirection(    i, dir.direction);
			//lights->SetLightDiffuseColor( i, dir.diffuse);
			//lights->SetLightSpecularColor(i, dir.specular);

			lights->SetLightingEnabled(true);
			lights->SetPerPixelLighting(true);
			lights->SetLightEnabled(0, true);
			lights->SetLightDiffuseColor(0, Colors::White);
			lights->SetLightEnabled(1, false);
			lights->SetLightEnabled(2, false);
		}
	}

	/* fog */
	auto fog = dynamic_cast<IEffectFog*>(effect);
	if(fog)
	{
		//fog->SetFogEnabled(m_enable_fog);
		//fog->SetFogColor(m_fog_color);
		//fog->SetFogStart(m_fog_planes.first);
		//fog->SetFogEnd(m_fog_planes.second);
	}
}

void Graphics::UpdateRaster()
{
	DX::ThrowIfFailed(m_d3dDevice->CreateRasterizerState(&m_raster_desc, m_raster.ReleaseAndGetAddressOf()));
}