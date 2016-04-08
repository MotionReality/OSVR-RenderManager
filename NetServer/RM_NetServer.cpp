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

#include "NetMessages.h"

#include <osvr/RenderKit/RenderManager.h>
#include <osvr/RenderKit/RenderManagerC.h>
#include <osvr/RenderKit/GraphicsLibraryD3D11.h>

#pragma optimize("",off)

using namespace DirectX;

void OSVR_Init();
std::vector<osvr::renderkit::RenderInfo> OSVR_GetRenderInfo(osvr::renderkit::RenderManager::RenderParams const & renderParams);
void OSVR_Register(HANDLE * pHandles, size_t const count);
int OSVR_Present(size_t idxBufPair, OSVR_Quaternion * pQuat);
void OSVR_Shutdown();

#if defined(SendMessage)
#undef SendMessage
#endif

class RenderManagerServer
{
public:
    RenderManagerServer();
    ~RenderManagerServer();

    void Run();
    void Shutdown();

private:
    bool m_bShutdown;
    HANDLE m_hPipe;

    void Init();
    void RunOnce();

    bool SendMessage(void * pBuffer, size_t lenBytes);
    bool ReceiveMessage(void * pBuffer, size_t maxLenBytes, size_t * pActualLenBytes = nullptr);
};

static RenderManagerServer * s_pServer = nullptr;

RenderManagerServer::RenderManagerServer()
    : m_bShutdown(false)
    , m_hPipe(INVALID_HANDLE_VALUE)
{
    s_pServer = this;
}

RenderManagerServer::~RenderManagerServer()
{
    Shutdown();
    s_pServer = nullptr;
}

void RenderManagerServer::Shutdown()
{
    std::cerr << "RenderManagerServer shutting down" << std::endl;

    s_pServer = nullptr;
    m_bShutdown = true;
    if (m_hPipe != INVALID_HANDLE_VALUE)
    {
        ::CancelIoEx(m_hPipe, nullptr);
        ::CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }

    std::cerr << "Closed pipe" << std::endl;
}

void RenderManagerServer::Init()
{
    const char * szPipeName = "\\\\.\\pipe\\com.motionreality.rendermanagerserver.primary";
    HANDLE hPipe = ::CreateNamedPipeA(
        szPipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1 /*nMaxInstances*/,
        1024 /* Out buffer size */,
        1024 /* In buffer size */,
        0 /* Default Timeout */,
        NULL
        );

    if (hPipe == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Failed to open pipe: " << ::GetLastError() << std::endl;
        return;
    }

    m_hPipe = hPipe;

    // Handle the race condition of Shutdown being called during Init
    if (m_bShutdown)
    {
        Shutdown();
    }
}

void RenderManagerServer::Run()
{
    std::cerr << "Running RM server" << std::endl;

    Init();
    while (!m_bShutdown && m_hPipe != INVALID_HANDLE_VALUE)
    {
        std::cerr << "Waiting for a new connection..." << std::endl;
        DWORD dwError = 0;
        if (!::ConnectNamedPipe(m_hPipe, nullptr))
        {
            DWORD const dwError = ::GetLastError();
            if (dwError != ERROR_PIPE_CONNECTED)
            {
                std::cerr << "ConnectNamedPipe loop exited with: " << dwError << std::endl;
                break;
            }
        }
        
        std::cerr << "Got new connection" << std::endl;
        RunOnce();

        OSVR_Shutdown();

        std::cerr << "Disconnecting..." << std::endl;
        if (!::DisconnectNamedPipe(m_hPipe))
        {
            std::cerr << "DisconnectNamedPipe returned error " << ::GetLastError() << std::endl;
        }
    }
}

void RenderManagerServer::RunOnce()
{
    using namespace Messages;

    size_t presentCounter = 0;
    uint8_t byteBuffer[128];
    while (!m_bShutdown && m_hPipe != INVALID_HANDLE_VALUE)
    {
        size_t msgLen = 0;
        if (!ReceiveMessage(&byteBuffer[0], sizeof(byteBuffer), &msgLen))
        {
            std::cerr << "Failed to read pipe: " << ::GetLastError() << std::endl;
            return; // Causes disconnect
        }

        if (msgLen < sizeof(MessageBase))
        {
            std::cerr << "Runt message: " << msgLen << std::endl;
            return; // Causes disconnect
        }

        auto const messageId = reinterpret_cast<MessageBase*>(&byteBuffer[0])->messageId();
        //std::cerr << "Received message: " << messageId << std::endl;

        switch (messageId)
        {
        case eMsgId_RequestRenderInfo:
            {
                if (msgLen < sizeof(Messages::RequestRenderInfo))
                {
                    std::cerr << "Runt message: id = " << messageId << " size = " << msgLen << std::endl;
                    return; // Causes disconnect
                }
                auto * const pMsg = reinterpret_cast<RequestRenderInfo*>(&byteBuffer[0]);

                OSVR_Init();

                osvr::renderkit::RenderManager::RenderParams renderParams;
                if (pMsg->farClip > 0)
                    renderParams.farClipDistanceMeters = pMsg->farClip;
                if (pMsg->nearClip > 0)
                    renderParams.nearClipDistanceMeters = pMsg->nearClip;
                if (pMsg->ipd > 0)
                    renderParams.IPDMeters = pMsg->ipd;

                std::vector<osvr::renderkit::RenderInfo> const renderInfo = OSVR_GetRenderInfo(renderParams);
                
                size_t const count = (std::min)(renderInfo.size(), (size_t)MAX_RENDERINFO_COUNT); 
                uint8_t byteBuffer[sizeof(Messages::SendRenderInfo) + MAX_RENDERINFO_COUNT*RENDER_INFO_SIZE];
                size_t const numBytes = sizeof(Messages::SendRenderInfo) + count*RENDER_INFO_SIZE;
                auto * const pSendMsg = reinterpret_cast<Messages::SendRenderInfo*>(&byteBuffer[0]);
                new (pSendMsg)Messages::SendRenderInfo();
                pSendMsg->numRenderInfos = count;
                uint8_t * pData = &byteBuffer[sizeof(Messages::SendRenderInfo)];
                for (size_t i = 0; i < count; ++i)
                {
                    auto const  & info = renderInfo[i];
                    memcpy(pData, &info.viewport, sizeof(info.viewport)); pData += sizeof(info.viewport);
                    memcpy(pData, &info.pose, sizeof(info.pose)); pData += sizeof(info.pose);
                    memcpy(pData, &info.projection, sizeof(info.projection)); pData += sizeof(info.projection);
                }
                
                if (!SendMessage(&byteBuffer[0], numBytes))
                {
                    std::cerr << "Failed to send SendRenderInfo message: " << ::GetLastError() << std::endl;
                    return; // Causes disconnect
                }
            }
            break; 
        case eMsgId_RegisterBuffers:
            {
                std::cerr << "Got RegisterBuffers message" << std::endl;
                if (msgLen < sizeof(Messages::RegisterBuffers))
                {
                    std::cerr << "Runt message: id = " << messageId << " size = " << msgLen << std::endl;
                    return; // Causes disconnect
                }
                auto * const pMsg = reinterpret_cast<RegisterBuffers*>(&byteBuffer[0]);
                if (pMsg->numBuffers > 16)
                {
                    std::cerr << "Too many buffers: " << pMsg->numBuffers << std::endl;
                    return; // Causes disconnect
                }
                if (msgLen != sizeof(RegisterBuffers) + pMsg->numBuffers*sizeof(HANDLE))
                {
                    std::cerr << "Invalid message size: id = " << messageId << " size = " << msgLen << std::endl;
                    return; // Causes disconnect
                }
                std::cerr << "RegisterBuffers has " << pMsg->numBuffers << " buffer handles" << std::endl;
                auto * const pHandles = reinterpret_cast<HANDLE*>(&pMsg[1]);
                OSVR_Init();
                OSVR_Register(&pHandles[0], pMsg->numBuffers);
            }
            break;
        case eMsgId_BeginPresent:
            {
                if (msgLen < sizeof(Messages::BeginPresent))
                {
                    std::cerr << "Runt message: id = " << messageId << " size = " << msgLen << std::endl;
                    return; // Causes disconnect
                }
                auto * const pMsg = reinterpret_cast<BeginPresent*>(&byteBuffer[0]);

                PresentAck presentAck;
                if (!SendMessage(&presentAck, sizeof(presentAck)))
                {
                    std::cerr << "Failed to write present ACK message" << std::endl;
                    return; // Causes disconnect
                }

                int presentResult = 0;
                if (pMsg->qHeadValid)
                {
                    OSVR_Quaternion qHead;
                    osvrQuatSetW(&qHead, pMsg->qw);
                    osvrQuatSetX(&qHead, pMsg->qx);
                    osvrQuatSetY(&qHead, pMsg->qy);
                    osvrQuatSetZ(&qHead, pMsg->qz);
                    presentResult = OSVR_Present(pMsg->idxBufferSet, &qHead);
                }
                else
                {
                    presentResult = OSVR_Present(pMsg->idxBufferSet, nullptr);
                }

                PresentResult presentResultMsg; 
                presentResultMsg.resultCode = presentResult;
                if (!SendMessage(&presentResultMsg, sizeof(presentResultMsg)))
                {
                    std::cerr << "Failed to write present result message" << std::endl;
                    return; // Causes disconnect
                }

                ++presentCounter; 
                if ((presentCounter % 60) == 0 || presentCounter == 1)
                {
                    std::cerr << "Presented " << presentCounter << " frames" << std::endl;
                }                
            }
            break;
        default:
            std::cerr << "Unknown message id: " << messageId << std::endl;
            return; // Causes disconnect
        };
    }
}

bool RenderManagerServer::SendMessage(void * pBuffer, size_t lenBytes)
{
    DWORD dwBytesWritten = 0;
    //fprintf(stderr, "Sending message:");
    //for (size_t i = 0; i < lenBytes; ++i)
    //{
    //    fprintf(stderr, " %02X", ((uint8_t*)pBuffer)[i]);
    //}
    //fprintf(stderr, "\n");
    if (!::WriteFile(m_hPipe, pBuffer, lenBytes, &dwBytesWritten, nullptr) ||
        lenBytes != dwBytesWritten)
    {
        std::cerr << "Failed to send message: " << ::GetLastError() << std::endl;
        return false;
    }
}

bool RenderManagerServer::ReceiveMessage(void * pBuffer, size_t maxLenBytes, size_t * pActualLenBytes)
{
    DWORD dwBytesRead = 0;
    if (!::ReadFile(m_hPipe, pBuffer, maxLenBytes, &dwBytesRead, nullptr))
    {
        std::cerr << "Failed to read message: " << ::GetLastError() << std::endl;
        return false;
    }

    if (pActualLenBytes)
        *pActualLenBytes = dwBytesRead;

    return true;
}

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
        std::cerr << "Caught shutdown event" << std::endl;
        if (s_pServer)
        {
            s_pServer->Shutdown();
        }
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

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

    RenderManagerServer server;
    server.Run();
    server.Shutdown();

    std::cerr << "Exiting" << std::endl;
    return 0;
}
