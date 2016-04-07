/** @file
    @brief Example program that uses the OSVR direct-to-display interface
           and D3D to render a scene with low latency.

    @date 2015

    @author
    Russ Taylor working through ReliaSolve.com for Sensics, Inc.
    <http://sensics.com/osvr>
*/

// Copyright 2015 Sensics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Library/third-party includes
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>

// Standard includes
#include <iostream>
#include <string>
#include <stdlib.h> // For exit()
#include <chrono>

#include <osvr/RenderKit/RenderManager.h>

// Includes from our own directory
#include "../examples/pixelshader3d.h"
#include "../examples/vertexshader3d.h"

using namespace DirectX;

#include "../examples/D3DCube.h"
#include "../examples/D3DSimpleShader.h"

#include "RM_NetClient.h"

struct RM_NetClient_Deleter
{
    void operator()(RM_NetClient * const p) const { RM_NetClient_Destroy(p); }
};
using RM_NetClient_Ptr = std::unique_ptr<RM_NetClient, RM_NetClient_Deleter>;
RM_NetClient_Ptr pNetClient;

struct BufferInfo
{
    BufferInfo()
        : colorTexture(nullptr)
        , colorView(nullptr)
        , colorMutex(nullptr)
        , depthTexture(nullptr)
        , depthView(nullptr)
    {}

    ~BufferInfo()
    {
        SAFE_RELEASE(depthView);
        SAFE_RELEASE(depthTexture);
        SAFE_RELEASE(colorMutex);
        SAFE_RELEASE(colorView);
        SAFE_RELEASE(colorTexture);
    }

    ID3D11Texture2D * colorTexture;
    ID3D11RenderTargetView * colorView;
    IDXGIKeyedMutex * colorMutex;
    ID3D11Texture2D * depthTexture;
    ID3D11DepthStencilView * depthView;

private:
    template<typename T> void SAFE_RELEASE(T *& p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }
};

// Set to true when it is time for the application to quit.
// Handlers below that set it to true when the user causes
// any of a variety of events so that we shut down the system
// cleanly.  This only works on Windows, but so does D3D...
bool quit = false;
static Cube roomCube(5.0f, true);
static SimpleShader simpleShader;

#ifdef _WIN32
// Note: On Windows, this runs in a different thread from
// the main application.
static BOOL CtrlHandler(DWORD fdwCtrlType) {
    switch (fdwCtrlType) {
    // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
    // CTRL-CLOSE: confirm that the user wants to exit.
    case CTRL_CLOSE_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        std::cerr << "Got shutdown event" << std::endl;
        quit = true;
        if (pNetClient.get())
        {
            std::cerr << "Disconnecting client" << std::endl;
            RM_NetClient_Disconnect(pNetClient.get());
        }
        return TRUE;
    default:
        return FALSE;
    }
}
#endif


// Callbacks to draw things in world space, left-hand space, and right-hand
// space.
void RenderView(
    ID3D11Device * const device,
    ID3D11DeviceContext * context,
    RM_NetRenderInfo & renderInfo, //< Info needed to render
    BufferInfo & bufferInfo) {

    float projectionD3D[16];
    float viewD3D[16];
    XMMATRIX identity = XMMatrixIdentity();

    bufferInfo.colorMutex->AcquireSync(0, INFINITE);

    // Set up to render to the textures for this eye
    context->OMSetRenderTargets(1, &bufferInfo.colorView, bufferInfo.depthView);

    // Set up the viewport we're going to draw into.
    CD3D11_VIEWPORT viewport(static_cast<float>(renderInfo.viewport.left),
                             static_cast<float>(renderInfo.viewport.lower),
                             static_cast<float>(renderInfo.viewport.width),
                             static_cast<float>(renderInfo.viewport.height));
    context->RSSetViewports(1, &viewport);

    // Make a grey background
    FLOAT colorRgba[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
    context->ClearRenderTargetView(bufferInfo.colorView, colorRgba);
    context->ClearDepthStencilView(
        bufferInfo.depthView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    osvr::renderkit::OSVR_PoseState_to_D3D(viewD3D, (OSVR_PoseState&)renderInfo.pose);
    osvr::renderkit::OSVR_Projection_to_D3D(projectionD3D,
        *reinterpret_cast<osvr::renderkit::OSVR_ProjectionMatrix*>(&renderInfo.projection));

    XMMATRIX xm_projectionD3D(projectionD3D), xm_viewD3D(viewD3D);

    // draw room
    simpleShader.use(device, context, xm_projectionD3D, xm_viewD3D, identity);
    roomCube.draw(device, context);

    bufferInfo.colorMutex->ReleaseSync(0);
}

void Usage(std::string name) {
    std::cerr << "Usage: " << name << std::endl;
    exit(-1);
}

int main(int argc, char* argv[]) {
    // Parse the command line
    int realParams = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            Usage(argv[0]);
        } else
            switch (++realParams) {
            case 1:
            default:
                Usage(argv[0]);
            }
    }
    if (realParams != 0) {
        Usage(argv[0]);
    }

// Set up a handler to cause us to exit cleanly.
#ifdef _WIN32
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
#endif

    pNetClient.reset(RM_NetClient_Create());
    if (!pNetClient)
    {
        std::cerr << "Failed to create RM NetClient" << std::endl;
        return -1;
    }

    // Create a D3D11 device and context to be used, rather than
    // having RenderManager make one for us.  This is an example
    // of using an external one, which would be needed for clients
    // that already have a rendering pipeline, like Unity.
    ID3D11Device* myDevice = nullptr;         // Fill this in
    ID3D11DeviceContext* myContext = nullptr; // Fill this in.

    // Here, we open the device and context ourselves, but if you
    // are working with a render library that provides them for you,
    // just stick them into the values rather than constructing
    // them.  (This is a bit of a toy example, because we could
    // just let RenderManager do this work for us and use the library
    // it sends back.  However, it does let us set parameters on the
    // device and context construction the way that we want, so it
    // might be useful.  Be sure to get D3D11 and have set
    // D3D11_CREATE_DEVICE_BGRA_SUPPORT in the device/context
    // creation, however it is done).
    D3D_FEATURE_LEVEL acceptibleAPI = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL foundAPI;
    auto hr =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &acceptibleAPI, 1,
        D3D11_SDK_VERSION, &myDevice, &foundAPI, &myContext);
    if (FAILED(hr)) {
        std::cerr << "Could not create D3D11 device and context" << std::endl;
        return -1;
    }

    if (!RM_NetClient_Connect(pNetClient.get(),true) ||
        !RM_NetClient_UpdateRenderInfo(pNetClient.get()))
    {
        std::cerr << "Failed to connect to RM NetClient" << std::endl;
        return -1;
    }

    std::cerr << "Connected to RM NetServer" << std::endl;

    // Do a call to get the information we need to construct our
    // color and depth render-to-texture buffers.
    std::vector<RM_NetRenderInfo> renderInfo;
    renderInfo.resize(RM_NetClient_GetRenderInfoCount(pNetClient.get()));
    for (size_t i = 0; i < renderInfo.size(); ++i)
        RM_NetClient_GetRenderInfo(pNetClient.get(), i, &renderInfo[i]);
    
    std::cerr << "Got render info: count = " << renderInfo.size() << std::endl;

    // Set up the vector of textures to render to and any framebuffer
    // we need to group them.
    std::vector<BufferInfo> renderBufferInfos(renderInfo.size());

    for (size_t i = 0; i < renderBufferInfos.size(); i++) {

        // The color buffer for this eye.  We need to put this into
        // a generic structure for the Present function, but we only need
        // to fill in the Direct3D portion.
        //  Note that this texture format must be RGBA and unsigned byte,
        // so that we can present it to Direct3D for DirectMode.
        ID3D11Texture2D* D3DTexture = nullptr;
        unsigned width = static_cast<int>(renderInfo[i].viewport.width);
        unsigned height = static_cast<int>(renderInfo[i].viewport.height);

        // Initialize a new render target texture description.
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        // textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        // We need it to be both a render target and a shader resource
        textureDesc.BindFlags =
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        textureDesc.CPUAccessFlags = 0;
        textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        // Create a new render target texture to use.
        hr = myDevice->CreateTexture2D(&textureDesc, nullptr, &D3DTexture);
        if (FAILED(hr)) {
            std::cerr << "Can't create texture for eye " << i << std::endl;
            return -1;
        }

        // Fill in the resource view for your render texture buffer here
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
        // This must match what was created in the texture to be rendered
        // @todo Figure this out by introspection on the texture?
        // renderTargetViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        renderTargetViewDesc.Texture2D.MipSlice = 0;

        // Create the render target view.
        ID3D11RenderTargetView* renderTargetView; //< Pointer to our render target view
        hr = myDevice->CreateRenderTargetView( D3DTexture, &renderTargetViewDesc, &renderTargetView);
        if (FAILED(hr)) {
            std::cerr << "Could not create render target for eye " << i
                      << std::endl;
            return -2;
        }

        // Push the filled-in RenderBuffer onto the vector.
        renderBufferInfos[i].colorTexture = D3DTexture;
        renderBufferInfos[i].colorView = renderTargetView;
        renderBufferInfos[i].colorTexture->QueryInterface(__uuidof(IDXGIKeyedMutex),
            (void**)&renderBufferInfos[i].colorMutex);

        //==================================================================
        // Create a depth buffer

        // Make the depth/stencil texture.
        D3D11_TEXTURE2D_DESC textureDescription = {};
        textureDescription.SampleDesc.Count = 1;
        textureDescription.SampleDesc.Quality = 0;
        textureDescription.Usage = D3D11_USAGE_DEFAULT;
        textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.CPUAccessFlags = 0;
        textureDescription.MiscFlags = 0;
        /// @todo Make this a parameter
        textureDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        ID3D11Texture2D* depthStencilBuffer;
        hr = myDevice->CreateTexture2D( &textureDescription, NULL, &depthStencilBuffer);
        if (FAILED(hr)) {
            std::cerr << "Could not create depth/stencil texture for eye " << i
                      << std::endl;
            return -4;
        }
        renderBufferInfos[i].depthTexture = depthStencilBuffer;

        // Create the depth/stencil view description
        D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription = {};
        depthStencilViewDescription.Format = textureDescription.Format;
        depthStencilViewDescription.ViewDimension =
            D3D11_DSV_DIMENSION_TEXTURE2D;
        depthStencilViewDescription.Texture2D.MipSlice = 0;

        ID3D11DepthStencilView* depthStencilView;
        hr = myDevice->CreateDepthStencilView(
            depthStencilBuffer, &depthStencilViewDescription,
            &depthStencilView);
        if (FAILED(hr)) {
            std::cerr << "Could not create depth/stencil view for eye " << i
                      << std::endl;
            return -5;
        }
        renderBufferInfos[i].depthView = depthStencilView;
    }

    // Create depth stencil state.
    // Describe how depth and stencil tests should be performed.
    D3D11_DEPTH_STENCIL_DESC depthStencilDescription = {};

    depthStencilDescription.DepthEnable = true;
    depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS;

    depthStencilDescription.StencilEnable = true;
    depthStencilDescription.StencilReadMask = 0xFF;
    depthStencilDescription.StencilWriteMask = 0xFF;

    // Front-facing stencil operations
    depthStencilDescription.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDescription.FrontFace.StencilDepthFailOp =
        D3D11_STENCIL_OP_INCR;
    depthStencilDescription.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDescription.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // Back-facing stencil operations
    depthStencilDescription.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDescription.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    depthStencilDescription.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDescription.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    ID3D11DepthStencilState* depthStencilState;
    hr = myDevice->CreateDepthStencilState(
        &depthStencilDescription, &depthStencilState);
    if (FAILED(hr)) {
        std::cerr << "Could not create depth/stencil state" << std::endl;
        return -3;
    }

    std::cerr << "Created textures. Registering textures with RM" << std::endl;

    // Register our constructed buffers so that we can use them for
    // presentation.
    {
        std::vector<ID3D11Texture2D*> textures(renderBufferInfos.size());
        for (size_t i = 0; i < renderBufferInfos.size(); ++i)
        {
            textures[i] = renderBufferInfos[i].colorTexture;
        }
        if (!RM_NetClient_RegisterRenderBuffers(pNetClient.get(), &textures[0], textures.size()))
        {
            std::cerr << "RegisterRenderBuffers() returned false, cannot continue"
                << std::endl;
            quit = true;
        }
    }
    
    std::cerr << "Starting main loop" << std::endl;

    // Timing of frame rates
    size_t count = 0;
    std::chrono::time_point<std::chrono::system_clock> start, end;
    start = std::chrono::system_clock::now();
    
    // Continue rendering until it is time to quit.
    while (!quit) {
        ::Sleep(8);

        RM_NetClient_UpdateRenderInfo(pNetClient.get());
        {
            size_t const count = RM_NetClient_GetRenderInfoCount(pNetClient.get());
            assert(renderInfo.size() == count);
        }
        for (size_t i = 0; i < renderInfo.size(); ++i)
        {
            RM_NetClient_GetRenderInfo(pNetClient.get(), i, &renderInfo[i]);
        }

        // Render into each buffer using the specified information.
        for (size_t i = 0; i < renderInfo.size(); i++) {
            myContext->OMSetDepthStencilState(depthStencilState, 1);
            RenderView(myDevice, myContext, renderInfo[i], renderBufferInfos[i]);
        }

        //myContext->Flush();

        // Send the rendered results to the screen
        if (!RM_NetClient_PresentRenderBuffers(pNetClient.get(), 0)) {
            std::cerr << "PresentRenderBuffers() returned false, maybe because "
                         "it was asked to quit"
                      << std::endl;
            quit = true;
        }

        // Timing information
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_sec = end - start;
        if (elapsed_sec.count() >= 2) {
            std::chrono::duration<double, std::micro> elapsed_usec =
                end - start;
            double usec = elapsed_usec.count();
            std::cout << "******************************" << std::endl
                      << "Rendering at " << count / (usec * 1e-6) << " fps"
                      << std::endl;
            auto const & info = renderInfo[0];
            fprintf(stderr, "Head: %2.6f %2.6f %2.6f %2.6f\n",
                osvrQuatGetX((OSVR_Quaternion*)&info.pose.rotation),
                osvrQuatGetY((OSVR_Quaternion*)&info.pose.rotation),
                osvrQuatGetZ((OSVR_Quaternion*)&info.pose.rotation),
                osvrQuatGetW((OSVR_Quaternion*)&info.pose.rotation));
            fprintf(stderr, "View: %2.6f %2.6f %2.6f %2.6f\n",
                info.viewport.left, info.viewport.lower,
                info.viewport.width, info.viewport.height);
            fprintf(stderr, "Proj: %2.6f %2.6f %2.6f %2.6f %2.6f %2.6f\n",
                info.projection.nearClip, info.projection.farClip,
                info.projection.left, info.projection.bottom,
                info.projection.right, info.projection.top);
            start = end;
            count = 0;
        }
        count++;
    }

    pNetClient.reset();

    myContext->Release();
    myDevice->Release();

    return 0;
}
