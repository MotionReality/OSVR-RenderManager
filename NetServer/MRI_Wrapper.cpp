// OSVR_RenderManager_Test.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include <cstdio>
#include <memory>
#include <string>

#include <Windows.h>
#include <D3D11.h>

#include <osvr/ClientKit/ContextC.h>
#include <osvr/RenderKit/RenderManager.h>
#include <osvr/RenderKit/GraphicsLibraryD3D11.h>


#pragma optimize("",off)

using namespace osvr::renderkit;


static HANDLE GetSharedHandle(ID3D11Texture2D * pTex)
{
    HANDLE sharedHandle = 0;

    if (pTex)
    {
        IDXGIResource* pOtherResource(nullptr);
        HRESULT hr = pTex->QueryInterface(__uuidof(IDXGIResource), (void**)&pOtherResource);

        if (pOtherResource)
        {
            pOtherResource->GetSharedHandle(&sharedHandle);
        }
    }

    return sharedHandle;
}

static ID3D11Texture2D * CloneToDev(ID3D11Device * pTargetDev, HANDLE hTex)
{
    ID3D11Texture2D * pResult = nullptr;
    if (hTex)
    {
        pTargetDev->OpenSharedResource(hTex, __uuidof(ID3D11Texture2D), (LPVOID*)&pResult);
    }
    return pResult;
}

static ID3D11Texture2D * CloneToDev(ID3D11Device * pTargetDev, ID3D11Texture2D * pSourceTex)
{
    return CloneToDev(pTargetDev, GetSharedHandle(pSourceTex));
}

static RenderBufferD3D11 * MakeRenderBuffer(ID3D11Device * pDevice, ID3D11Texture2D * pTex)
{
    RenderBufferD3D11 * pResult = nullptr;

    D3D11_TEXTURE2D_DESC desc;
    pTex->GetDesc(&desc);

    // Fill in the resource view for your render texture buffer here
    // This must match what was created in the texture to be rendered
    D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc = {};
    renderTargetViewDesc.Format = desc.Format;
    renderTargetViewDesc.ViewDimension = desc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;;
    renderTargetViewDesc.Texture2D.MipSlice = 0;

    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    HRESULT hr = pDevice->CreateRenderTargetView(pTex, &renderTargetViewDesc, &pRenderTargetView);
    if (FAILED(hr) || pRenderTargetView == nullptr) {
        std::cerr << "Could not create render target for eye " << std::endl;
    }
    else
    {
        pResult = new RenderBufferD3D11;
        memset(pResult, 0, sizeof(*pResult));
        pResult->colorBuffer = pTex;
        pResult->colorBufferView = pRenderTargetView;
    }

    return pResult;
}

template<typename T> void SAFE_RELEASE(T * const p)
{
    if (p)
        p->Release();
}

struct AppState
{
    AppState()
        : context(nullptr)
    {
        context = osvrClientInit("com.motionreality.RM_Server");
    }

    ~AppState()
    {
        for (auto itPair = bufferPairs.begin(); itPair != bufferPairs.end(); ++itPair)
        {
            for (auto it = itPair->begin(); it != itPair->end(); ++it)
            {
                if (it->D3D11)
                {
                    SAFE_RELEASE(it->D3D11->colorBuffer);
                    SAFE_RELEASE(it->D3D11->colorBufferView);
                    SAFE_RELEASE(it->D3D11->depthStencilBuffer);
                    SAFE_RELEASE(it->D3D11->depthStencilView);
                    delete it->D3D11;
                }
            }
        }

        if (context)
        {
            osvrClientShutdown(context);
            context = nullptr;
        }
    }

    OSVR_ClientContext context;
    std::unique_ptr<RenderManager> pRenderManager;
    osvr::renderkit::RenderManager::RenderParams renderParams;
    std::vector<osvr::renderkit::RenderInfo> renderInfo;
    typedef std::vector<RenderBuffer> BufferPair;
    std::vector<BufferPair> bufferPairs;
};

static std::unique_ptr<AppState> s_pAppState;

void OSVR_Shutdown()
{
    s_pAppState.reset();
}

void OSVR_Init()
{
    if (s_pAppState.get())
        return;

    // Get an OSVR client context to use to access the devices that we need.
    std::unique_ptr<AppState> appState(new AppState());

    osvrClientUpdate(appState->context);
    if (osvrClientCheckStatus(appState->context) != OSVR_RETURN_SUCCESS)
    {
        std::cerr << "Waiting for client context...\n";
        ::Sleep(500);
        osvrClientUpdate(appState->context);
        if (osvrClientCheckStatus(appState->context) != OSVR_RETURN_SUCCESS)
        {
            std::cerr << "OSVR ClientContext failed" << std::endl;
            return;
        }
    }

    appState->pRenderManager.reset(createRenderManager(appState->context, "Direct3D11"));
    if ((appState->pRenderManager == nullptr) || (!appState->pRenderManager->doingOkay())) {
        std::cerr << "Could not create RenderManager" << std::endl;
        return;
    }

    auto const openResults = appState->pRenderManager->OpenDisplay();
    std::cerr << "Open display = " << openResults.status << std::endl;
    if (openResults.status != RenderManager::COMPLETE)
    {
        std::cerr << "Failed to open render manager" << std::endl;
        return;
    }
    if (!openResults.library.D3D11)
    {
        std::cerr << "Attempted to run a Direct3D11 program with a config file "
            << "that specified a different renderling library."
            << std::endl;
        return;
    }

    appState->renderInfo = appState->pRenderManager->GetRenderInfo(appState->renderParams); // provides the viewport sizes
    std::cerr << "Found " << appState->renderInfo.size() << " render infos" << std::endl;
    for (size_t i = 0; i < appState->renderInfo.size(); ++i)
    {
        std::cerr << "    Eye " << i << ": " << appState->renderInfo[i].viewport.width << " x " << appState->renderInfo[i].viewport.height << std::endl;
    }

    s_pAppState.swap(appState);
}

std::vector<osvr::renderkit::RenderInfo> OSVR_GetRenderInfo(osvr::renderkit::RenderManager::RenderParams const & renderParams)
{
    std::vector<osvr::renderkit::RenderInfo> ret;
    if (s_pAppState.get())
    {
        s_pAppState->renderParams = renderParams;
        s_pAppState->renderInfo = s_pAppState->pRenderManager->GetRenderInfo(renderParams);
        ret = s_pAppState->renderInfo;

        static size_t counter = 0;
        ++counter;
        if ((counter % 60) == 0)
        {
            auto const & info = s_pAppState->renderInfo[0];
            //fprintf(stderr, "************************\n");
            fprintf(stderr, "Head: %2.6f %2.6f %2.6f %2.6f\n",
                osvrQuatGetX(&info.pose.rotation),
                osvrQuatGetY(&info.pose.rotation),
                osvrQuatGetZ(&info.pose.rotation),
                osvrQuatGetW(&info.pose.rotation));
            //fprintf(stderr, "View: %2.6f %2.6f %2.6f %2.6f\n",
            //    info.viewport.left, info.viewport.lower,
            //    info.viewport.width, info.viewport.height);
            //fprintf(stderr, "Proj: %2.6f %2.6f %2.6f %2.6f %2.6f %2.6f\n",
            //    info.projection.nearClip, info.projection.farClip,
            //    info.projection.left, info.projection.bottom,
            //    info.projection.right, info.projection.top);
        }
    }
    return ret;
}

void OSVR_Register(HANDLE * pHandles, size_t const handleCount)
{
    OSVR_Init();

    if (!s_pAppState.get())
        return;

    auto const & info = s_pAppState->renderInfo[0];
    auto * const pDevice = info.library.D3D11->device;

    std::cerr << "Cloning device handles: " << handleCount << std::endl;
    bool bFail = false;
    size_t const pairCount = handleCount / 2;
    s_pAppState->bufferPairs.resize(pairCount);
    for (size_t iPair = 0; iPair < pairCount; ++iPair)
    {
        auto & bufPair = s_pAppState->bufferPairs[iPair];
        bufPair.resize(2);
        for (size_t eye = 0; eye < 2; ++eye)
        {
            HANDLE const h = pHandles[iPair * 2 + eye];
            std::cerr << "    Handle: " << std::hex << h << std::endl;
            ID3D11Texture2D * pTex = CloneToDev(pDevice, h);
            if (!pTex)
            {
                std::cerr << "Failed to open shared texture" << std::endl;
                return; // AppState dtor will cleanup
            }
            auto & rb = bufPair[eye];
            memset(&rb, 0, sizeof(rb));
            rb.D3D11 = MakeRenderBuffer(pDevice, pTex);
        }

        if (!s_pAppState->pRenderManager->RegisterRenderBuffers(bufPair, false))
        {
            std::cerr << "Failed to register render buffers: " << std::endl;
            bFail = true;
            break;
        }
    }
}

int OSVR_Present(size_t idxBufPair, OSVR_Quaternion * pQuat)
{
    if (!s_pAppState || !s_pAppState->pRenderManager )
        return -1;

    if (!(idxBufPair < s_pAppState->bufferPairs.size()))
    {
        std::cerr << "Invalid buffer pair index: " << idxBufPair << std::endl;
        return -1;
    }

    //OSVR_PoseState poseRoomFromHead;
    //osvrVec3Zero(&poseRoomFromHead.translation);
    //osvrQuatSetX(&poseRoomFromHead.rotation, quat_xyzw[0]);
    //osvrQuatSetY(&poseRoomFromHead.rotation, quat_xyzw[1]);
    //osvrQuatSetZ(&poseRoomFromHead.rotation, quat_xyzw[2]);
    //osvrQuatSetW(&poseRoomFromHead.rotation, quat_xyzw[3]);

    //RenderManager::RenderParams params;
    //params.roomFromHeadReplace = &poseRoomFromHead;

    //static int counter = 0;
    //++counter;
    //if ( (counter % 60) == 0 )
    //{
    //    fprintf(stderr, "PRE: %2.6f, %2.6f, %2.6f, %2.6f (counter = %u)\n",
    //        quat_xyzw[0], quat_xyzw[1], quat_xyzw[2], quat_xyzw[3], counter);
    //}
    
    //std::cerr << "Present quat: "
    //    << osvrQuatGetX(&poseRoomFromHead.rotation) << ", "
    //    << osvrQuatGetY(&poseRoomFromHead.rotation) << ", "
    //    << osvrQuatGetZ(&poseRoomFromHead.rotation) << ", "
    //    << osvrQuatGetW(&poseRoomFromHead.rotation) << std::endl;

    auto * const pRM = s_pAppState->pRenderManager.get();
    //auto renderInfo = pRM->GetRenderInfo(params);

    auto const & buffers = s_pAppState->bufferPairs[idxBufPair];
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        IDXGIKeyedMutex * pMutex = nullptr;
        buffers[i].D3D11->colorBuffer->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&pMutex);
        if (pMutex)
        {
            pMutex->AcquireSync(0, INFINITE);
            pMutex->Release();
        }
    }

    auto renderInfoUsed = s_pAppState->renderInfo;
    if (pQuat)
    {
        auto tempParams = s_pAppState->renderParams;
        OSVR_PoseState state = { 0 };
        state.rotation = *pQuat;
        tempParams.roomFromHeadReplace = &state;
        renderInfoUsed = s_pAppState->pRenderManager->GetRenderInfo(tempParams);
    }

    int result = 0;
    if (pRM->PresentRenderBuffers(buffers, renderInfoUsed, s_pAppState->renderParams))
        result = 0;
    else
        result = -2;

    for (size_t i = 0; i < buffers.size(); ++i)
    {
        IDXGIKeyedMutex * pMutex = nullptr;
        buffers[i].D3D11->colorBuffer->QueryInterface(_uuidof(IDXGIKeyedMutex), (void**)&pMutex);
        if (pMutex)
        {
            pMutex->ReleaseSync(0);
            pMutex->Release();
        }
    }

    return result;
}

