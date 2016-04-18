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

static ID3D11RenderTargetView * MakeRenderTargetView(ID3D11Device * pDevice, ID3D11Texture2D * pTex)
{
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
    return pRenderTargetView;
}

template<typename T> void SAFE_RELEASE(T * const p)
{
    if (p)
        p->Release();
}

struct AppState
{
    AppState()
        : idxActiveBufferSet(-1)
        , context(osvrClientInit("com.motionreality.RM_Server"))
    {   }

    ~AppState()
    {
        if (context)
        {
            osvrClientShutdown(context);
            context = nullptr;
        }
    }

    struct BufferSet
    {
        BufferSet() { }
        BufferSet(BufferSet && rhs)
        {
            *this = rhs;
        }

        ~BufferSet()
        {
            for (auto & pKeyedMutex : mutexes)
            {
                SAFE_RELEASE(pKeyedMutex);
            }
            for (auto & buf : buffers)
            {
                if (buf.D3D11)
                {
                    SAFE_RELEASE(buf.D3D11->colorBuffer);
                    SAFE_RELEASE(buf.D3D11->colorBufferView);
                    SAFE_RELEASE(buf.D3D11->depthStencilBuffer);
                    SAFE_RELEASE(buf.D3D11->depthStencilView);
                    delete buf.D3D11;
                }
            }
        }

        BufferSet & operator=(BufferSet && rhs)
        {
            mutexes = std::move(rhs.mutexes);
            buffers = std::move(rhs.buffers);
        }

        std::vector<IDXGIKeyedMutex*> mutexes;
        std::vector<RenderBuffer> buffers;
    };
    std::vector<BufferSet> bufferSets;
    int idxActiveBufferSet; 
    
    OSVR_ClientContext context;
    std::unique_ptr<RenderManager> pRenderManager;
    osvr::renderkit::RenderManager::RenderParams renderParams;
    std::vector<osvr::renderkit::RenderInfo> renderInfo;    
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

    std::cerr << "Opened OSVR ClientContext" << std::endl;
    appState->pRenderManager.reset(createRenderManager(appState->context, "Direct3D11"));
    if (appState->pRenderManager == nullptr) {
        std::cerr << "Could not create RenderManager" << std::endl;
        return;
    }

    if (!appState->pRenderManager->doingOkay()) {
        std::cerr << "RenderManager not doing okay. Aborting." << std::endl;
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
            //fprintf(stderr, "Head: %2.6f %2.6f %2.6f %2.6f\n",
            //    osvrQuatGetX(&info.pose.rotation),
            //    osvrQuatGetY(&info.pose.rotation),
            //    osvrQuatGetZ(&info.pose.rotation),
            //    osvrQuatGetW(&info.pose.rotation));
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
    if (s_pAppState.get() && !s_pAppState->bufferSets.empty()) {
        // Force a reset if we try to register twice
        OSVR_Shutdown();
    }

    OSVR_Init();

    if (!s_pAppState.get())
        return;

    auto const & info = s_pAppState->renderInfo[0];
    auto * const pDevice = info.library.D3D11->device;

    
    size_t const setSize = s_pAppState->renderInfo.size();
    size_t const setCount = handleCount / setSize;

    std::cerr << "Cloning device handles: " << handleCount << " handles, "
        << setCount << " sets" << std::endl;

    s_pAppState->idxActiveBufferSet = -1;
    s_pAppState->bufferSets.clear();
    s_pAppState->bufferSets.resize(setCount);
    for (size_t iSet = 0; iSet < setCount; ++iSet )
    {
        auto & bufSet = s_pAppState->bufferSets[iSet];
        bufSet.buffers.resize(setSize);
        bufSet.mutexes.resize(setSize);
        for (size_t i = 0; i < setSize; ++i)
        {
            HANDLE const h = pHandles[iSet * setSize + i];
            std::cerr << "    Handle: " << std::hex << h << std::endl;
            ID3D11Texture2D * pTex = CloneToDev(pDevice, h);
            if (!pTex)
            {
                std::cerr << "Failed to open shared texture" << std::endl;
                return; // AppState dtor will cleanup
            }
            auto & rb = bufSet.buffers[i];
            memset(&rb, 0, sizeof(rb));
            rb.D3D11 = new RenderBufferD3D11;
            memset(rb.D3D11, 0, sizeof(*rb.D3D11));
            rb.D3D11->colorBuffer = pTex;

            auto hr = pTex->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&bufSet.mutexes[i]);
            if (FAILED(hr))
            {
                std::cerr << "Failed to get KeyedMutex for shared texture" << hr << std::endl;
                return;
            }
        }

        for (auto * pMutex : bufSet.mutexes)
        {
            auto hr = pMutex->AcquireSync(0, INFINITE);
            if (FAILED(hr))
            {
                std::cerr << "Failed to acquire mutex for texture: " << hr << std::endl;
            }
        }
        bool const bSuccess = s_pAppState->pRenderManager->RegisterRenderBuffers(bufSet.buffers, true);
        for (auto * pMutex : bufSet.mutexes)
        {
            auto hr = pMutex->ReleaseSync(0);
            if (FAILED(hr))
            {
                std::cerr << "Failed to acquire mutex for texture: " << hr << std::endl;
            }
        }

        if (!bSuccess)
        {
            std::cerr << "Failed to register render buffers: " << std::endl;
            return;
        }
    }
}

int OSVR_Present(size_t idxBufSet, OSVR_Quaternion * pQuat)
{
    if (!s_pAppState || !s_pAppState->pRenderManager )
        return -1;

    if (!(idxBufSet < s_pAppState->bufferSets.size()))
    {
        std::cerr << "Invalid buffer set index: " << idxBufSet << std::endl;
        return -1;
    }

    //std::cerr << "OSVR_Present( " << idxBufSet << ")" << std::endl;

    //static int counter = 0;
    //++counter;
    //if ( (counter % 60) == 0 )
    //{
    //    fprintf(stderr, "PRE: %2.6f, %2.6f, %2.6f, %2.6f (counter = %u)\n",
    //        quat_xyzw[0], quat_xyzw[1], quat_xyzw[2], quat_xyzw[3], counter);
    //}

    auto * const pRM = s_pAppState->pRenderManager.get();

    auto renderInfoUsed = s_pAppState->renderInfo;
    if (pQuat)
    {
        auto tempParams = s_pAppState->renderParams;
        OSVR_PoseState state = { 0 };
        state.rotation = *pQuat;
        tempParams.roomFromHeadReplace = &state;
        renderInfoUsed = s_pAppState->pRenderManager->GetRenderInfo(tempParams);
    }

    auto const & bufSet = s_pAppState->bufferSets[idxBufSet];

    for (auto * pMutex : bufSet.mutexes)
    {
        auto hr = pMutex->AcquireSync(0, INFINITE);
        if (FAILED(hr))
        {
            std::cerr << "Failed to acquire mutex for texture: " << hr << std::endl;
        }
    }

    bool bSuccess = pRM->PresentRenderBuffers(bufSet.buffers, renderInfoUsed, s_pAppState->renderParams);

    if (s_pAppState->idxActiveBufferSet >= 0 && s_pAppState->idxActiveBufferSet < s_pAppState->bufferSets.size())
    {
        auto & lastBufSet = s_pAppState->bufferSets[s_pAppState->idxActiveBufferSet];
        for (auto * pMutex : lastBufSet.mutexes)
        {
            auto hr = pMutex->ReleaseSync(0);
            if (FAILED(hr))
            {
                std::cerr << "Failed to release mutex for texture: " << hr << std::endl;
            }
        }
    }

    s_pAppState->idxActiveBufferSet = idxBufSet;

    return bSuccess ? 0 : -2;
}

