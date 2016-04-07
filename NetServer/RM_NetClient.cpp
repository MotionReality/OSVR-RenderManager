#include "RM_NetClient.h"

#include <Windows.h>
#include <d3d11.h>
#include <vector>
#include <algorithm>

#include "NetMessages.h"

#if defined(SendMessage)
#undef SendMessage
#endif

static HANDLE GetSharedHandle(ID3D11Texture2D * pD3DTex)
{
    IDXGIResource* pOtherResource(NULL);
    HRESULT hr = pD3DTex->QueryInterface(__uuidof(IDXGIResource), (void**)&pOtherResource);
    if (pOtherResource)
    {
        HANDLE sharedHandle;
        if (S_OK == pOtherResource->GetSharedHandle(&sharedHandle))
        {
            //fprintf( stderr, "Shared handle: %p [%s]\n", (void*)sharedHandle, pTex->GetName() );
            return sharedHandle;
        }
        else
        {
            D3D11_TEXTURE2D_DESC desc = { 0 };
            pD3DTex->GetDesc(&desc);
            fprintf(stderr, "Failed to get shared handle for %p with MiscFlags = %u\n",
                pOtherResource, desc.MiscFlags);
        }
    }
    else
    {
        fprintf(stderr, "Could not get IDXGIResource\n");
    }

    return INVALID_HANDLE_VALUE;
}

using Messages::MAX_RENDERINFO_COUNT;
using Messages::RENDER_INFO_SIZE;

struct RM_NetClient
{
    HANDLE m_hPipe;
    Messages::RequestRenderInfo renderParamsMsg;
    std::vector<RM_NetRenderInfo> m_renderInfo;
    
    RM_NetClient()
        : m_hPipe(INVALID_HANDLE_VALUE)
        , renderParamsMsg()
    {
        // Zeros mean ignore
        renderParamsMsg.nearClip = 0;
        renderParamsMsg.farClip = 0;
        renderParamsMsg.ipd = 0;
    }

    ~RM_NetClient()
    {
        this->Disconnect();
    }

    bool Connect(bool bIsPrimary)
    {
        Disconnect();

        const char * szPipeName = bIsPrimary
            ? "\\\\.\\pipe\\com.motionreality.rendermanagerserver.primary"
            : "\\\\.\\pipe\\com.motionreality.rendermanagerserver.secondary";

        m_hPipe = ::CreateFileA(szPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0 /* sharing = none */,
            nullptr /* security */,
            OPEN_EXISTING,
            0 /* attributes */,
            nullptr /* template */);

        if (m_hPipe == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "<OSVR> Failed to open pipe: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }

        DWORD dwMode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(m_hPipe, &dwMode, nullptr, nullptr))
        {
            fprintf(stderr, "<OSVR> Failed to put pipe in MESSAGE mode: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }

        if (!UpdateRenderInfo())
        {
            Disconnect();
            return false;
        }

        return true;
    }

    void Disconnect()
    {
        if (m_hPipe != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(m_hPipe);
            m_hPipe = INVALID_HANDLE_VALUE;
        }
        m_renderInfo.clear();
    }

    bool IsConnected() const { return m_hPipe != INVALID_HANDLE_VALUE;  }

    void SetNearClip(float nearClip)
    {
        renderParamsMsg.nearClip = nearClip;
    }

    void SetFarClip(float farClip)
    {
        renderParamsMsg.farClip = farClip;
    }

    void SetIPD(float ipd)
    {
        renderParamsMsg.ipd = ipd;
    }

    bool UpdateRenderInfo()
    {
        //std::cerr << "Updating render info..." << std::endl;

        if (!IsConnected())
        {
            fprintf(stderr, "Pipe not connected\n");
            return false;
        }           

        if (!SendMessage(&renderParamsMsg, sizeof(renderParamsMsg)))
        {
            return false;
        }

        //std::cerr << "Waiting for render info response..." << std::endl;

        uint8_t byteBuffer[sizeof(Messages::SendRenderInfo) + MAX_RENDERINFO_COUNT*RENDER_INFO_SIZE];
        size_t numBytes = 0;
        if (!ReceiveMessage(&byteBuffer[0], sizeof(byteBuffer), &numBytes))
        {
            return false;
        }

        if (numBytes < sizeof(Messages::SendRenderInfo))
        {
            fprintf(stderr, "Protocol error. Expected at least %u bytes, got %u\n", sizeof(Messages::SendRenderInfo), numBytes);
            Disconnect();
            return false;
        }

        auto const * const pMsg = reinterpret_cast<Messages::SendRenderInfo*>(&byteBuffer[0]);
        if (pMsg->messageId() != Messages::eMsgId_SendRenderInfo)
        {
            fprintf(stderr, "Protocol error. Expected id %u, got %u\n", Messages::eMsgId_SendRenderInfo, pMsg->messageId());
            Disconnect();
            return false;
        }

        if (pMsg->numRenderInfos > MAX_RENDERINFO_COUNT)
        {
            fprintf(stderr, "Protocol error. Too many render infos: %u\n", pMsg->numRenderInfos);
            Disconnect();
            return false;
        }

        if (pMsg->numRenderInfos == 0)
        {
            fprintf(stderr, "Protocol error. Zero render infos\n");
            Disconnect();
            return false;
        }

        size_t const reqBytes = sizeof(Messages::SendRenderInfo) + RENDER_INFO_SIZE*pMsg->numRenderInfos;
        if (numBytes != reqBytes)
        {
            fprintf(stderr, "Protocol error. Expected %u bytes, got %u\n", reqBytes, numBytes);
            Disconnect();
            return false;
        }

        m_renderInfo.resize(pMsg->numRenderInfos);
        uint8_t const * pData = &byteBuffer[sizeof(Messages::SendRenderInfo)];
        for (size_t i = 0; i < m_renderInfo.size(); ++i)
        {
            auto & info = m_renderInfo[i];
            memcpy(&info.viewport, pData, sizeof(info.viewport)); pData += sizeof(info.viewport); 
            memcpy(&info.pose, pData, sizeof(info.pose)); pData += sizeof(info.pose);
            memcpy(&info.projection, pData, sizeof(info.projection)); pData += sizeof(info.projection);
        }

        return true;
    }

    size_t GetRenderInfoCount() const
    {
        return m_renderInfo.size();
    }

    void GetRenderInfo(size_t idx, RM_NetRenderInfo * pRenderInfoOut)
    {
        if (idx < m_renderInfo.size() && pRenderInfoOut)
        {
            *pRenderInfoOut = m_renderInfo[idx];
        }
    }

    bool RegisterRenderBuffers(ID3D11Texture2D ** pTextures, size_t textureCount)
    {
        if (!IsConnected())
        {
            fprintf(stderr, "Pipe not connected\n");
            return false;
        }

        if ((textureCount % m_renderInfo.size()) != 0)
        {
            fprintf(stderr, "Must register a multiple of GetRenderInfoCount\n");
            return false;
        }

        std::vector<uint8_t> byteBuffer(sizeof(Messages::RegisterBuffers)
            + textureCount * sizeof(HANDLE));

        auto * pRegisterMsg = reinterpret_cast<Messages::RegisterBuffers*>(&byteBuffer[0]);
        new (pRegisterMsg)Messages::RegisterBuffers;
        pRegisterMsg->numBuffers = textureCount;
        auto * pHandles = reinterpret_cast<HANDLE*>(&pRegisterMsg[1]);
        for (size_t i = 0; i < textureCount; ++i)
        {
            pHandles[i] = ::GetSharedHandle(pTextures[i]);
            if (!pHandles[i])
            {
                fprintf(stderr, "Failed to get shared handle for texture: error = %u\n", ::GetLastError());
                return false;
            }
        }

        if (!SendMessage(&byteBuffer[0], byteBuffer.size()))
        {
            Disconnect();
            return false;
        }

        return true;
    }

    bool PresentRenderBuffers(size_t bufferSetIndex)
    {
        if (!IsConnected())
        {
            return false;
        }

        Messages::BeginPresent msg;

        msg.idxBufferSet = bufferSetIndex;
        //msg.qx = osvrQuatGetX(&m_renderInfo[0].pose.rotation);
        //msg.qy = osvrQuatGetY(&m_renderInfo[0].pose.rotation);
        //msg.qz = osvrQuatGetZ(&m_renderInfo[0].pose.rotation);
        //msg.qw = osvrQuatGetW(&m_renderInfo[0].pose.rotation);

        uint64_t ftSent, ftAck, ftResult;

        ::GetSystemTimeAsFileTime((FILETIME*)&ftSent);

        if (!SendMessage(&msg, sizeof(msg)))
        {
            fprintf(stderr, "<OSVR> Failed to write present message to pipe: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }

        // Block for ACK and present
        Messages::PresentAck ack;
        Messages::PresentResult result;

        if (!ReceiveMessage(&ack, sizeof(ack), nullptr))
        {
            fprintf(stderr, "<OSVR> Failed to read ack for present: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }
        ::GetSystemTimeAsFileTime((FILETIME*)&ftAck);

        if (!ReceiveMessage(&result, sizeof(result), nullptr))
        {
            fprintf(stderr, "<OSVR> Failed to read ack for present: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }
        ::GetSystemTimeAsFileTime((FILETIME*)&ftResult);

        static int counter = 0;
        static uint64_t totalAck = 0;
        static uint64_t totalPresent = 0;
        static uint64_t maxPresent = 0;
        ++counter;
        totalAck += (ftAck - ftSent);
        totalPresent += (ftResult - ftAck);
        maxPresent = (std::max)(maxPresent, (ftResult - ftAck));
        if ((counter % 60) == 0)
        {
            fprintf(stderr, "Avg present delay: Ack=%2.4f, Present=%2.4f, Max=%2.4f\n",
                (totalAck / (60 * 10000.)), (totalPresent / (60 * 10000.)), (maxPresent / (10000.)));
            counter = 0;
            totalAck = totalPresent = maxPresent = 0;
        }
        return true;
    }

protected:
    bool SendMessage(void * pData, size_t const lenBytes)
    {
        if (!IsConnected())
            return false;
        
        DWORD dwBytesWritten = 0;
        if (!::WriteFile(m_hPipe, pData, lenBytes, &dwBytesWritten, nullptr))
        {
            fprintf(stderr, "Pipe write failed with: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }

        if (dwBytesWritten != lenBytes)
        {
            fprintf(stderr, "Failed to write full message\n");
            Disconnect();
            return false;
        }

        return true;
    }

    bool ReceiveMessage(void * pBuffer, size_t const maxLenBytes, size_t * pLenBytesRecvd)
    {
        DWORD dwBytesRead = 0;
        if (!::ReadFile(m_hPipe, pBuffer, maxLenBytes, &dwBytesRead, nullptr))
        {
            fprintf(stderr, "<OSVR> Failed to read from pipe: %u\n", ::GetLastError());
            Disconnect();
            return false;
        }

        //fprintf(stderr, "Received message:");
        //for (size_t i = 0; i < dwBytesRead; ++i)
        //{
        //    fprintf(stderr, " %02X", ((uint8_t*)pBuffer)[i]);
        //}
        //fprintf(stderr, "\n");

        if (pLenBytesRecvd)
            *pLenBytesRecvd = dwBytesRead;
        return true;
    }
};

RM_NetClient * RM_NetClient_Create()
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl;
    return new RM_NetClient;
}

void RM_NetClient_Destroy(RM_NetClient * pClient)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    delete pClient;
}

bool RM_NetClient_Connect(RM_NetClient * pClient, bool bIsPrimary)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->Connect(bIsPrimary);
}

void RM_NetClient_Disconnect(RM_NetClient * pClient)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->Disconnect();
}

bool RM_NetClient_IsConnected(RM_NetClient * pClient)
{
    return pClient->IsConnected();
}

void RM_NetClient_SetRenderParams(RM_NetClient * pClient, float nearClip, float farClip, float ipd)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    pClient->SetNearClip(nearClip);
    pClient->SetFarClip(farClip);
    pClient->SetIPD(ipd);
}

bool RM_NetClient_UpdateRenderInfo(RM_NetClient * pClient)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->UpdateRenderInfo();
}

size_t RM_NetClient_GetRenderInfoCount(RM_NetClient * pClient)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->GetRenderInfoCount();
}

void RM_NetClient_GetRenderInfo(RM_NetClient * pClient, size_t idx, RM_NetRenderInfo * pRenderInfoOut)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl;
    pClient->GetRenderInfo(idx, pRenderInfoOut);
}

bool RM_NetClient_RegisterRenderBuffers(RM_NetClient * pClient, ID3D11Texture2D ** pTextures, size_t textureCount)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->RegisterRenderBuffers(pTextures, textureCount);
}

bool RM_NetClient_PresentRenderBuffers(RM_NetClient * pClient, size_t bufferSetIndex)
{
    //std::cerr << "RM_NetClient: line = " << __LINE__ << std::endl; 
    return pClient->PresentRenderBuffers(bufferSetIndex);
}
