/** @file
    @brief Example program that uses the OSVR direct-to-display interface
           and D3D to render a scene with low latency.

    @date 2015

    @author
    Russ Taylor working through ReliaSolve.com for Sensics, Inc.
    <http://sensics.com/osvr>
*/

#include <windows.h> // For handle
#include <cstdint>

#include <osvr/RenderKit/RenderManagerC.h>

#pragma pack(push,4)
namespace Messages
{
    enum eMsgId
    {
        eMsgId_INVALID = 0,
        eMsgId_RequestRenderInfo, 
        eMsgId_RegisterBuffers,        
        eMsgId_SendRenderInfo,
        eMsgId_BeginPresent,
        eMsgId_PresentAck,
        eMsgId_PresentResult,
    };

    struct MessageBase
    {
        MessageBase(eMsgId id) : m_messageId(id) { }
        uint32_t m_messageId;
        eMsgId messageId() const { return static_cast<eMsgId>(m_messageId); }
    };

    struct RegisterBuffers : MessageBase
    {
        RegisterBuffers() : MessageBase(eMsgId_RegisterBuffers) {}
        uint32_t numBuffers;
    };

    struct RequestRenderInfo : MessageBase
    {
        RequestRenderInfo() : MessageBase(eMsgId_RequestRenderInfo) {}
        float nearClip;
        float farClip;
        float ipd;
    };

    struct SendRenderInfo : MessageBase
    {
        SendRenderInfo() : MessageBase(eMsgId_SendRenderInfo) {}
        uint32_t numRenderInfos;
    };

    enum {
        RENDER_INFO_SIZE = sizeof(OSVR_ViewportDescription) + sizeof(OSVR_PoseState) + sizeof(OSVR_ProjectionMatrix),
        MAX_RENDERINFO_COUNT = 8
    };

    struct BeginPresent : MessageBase
    {
        BeginPresent() : MessageBase(eMsgId_BeginPresent) {}
        uint32_t idxBufferSet;
        //double qx, qy, qz, qw; // Head pose at time of render
    };

    struct PresentAck : MessageBase
    {
        PresentAck() : MessageBase(eMsgId_PresentAck) {}
    };

    struct PresentResult : MessageBase
    {
        PresentResult() : MessageBase(eMsgId_PresentResult) {}
        uint32_t resultCode;
    };
}
#pragma pack(pop)
