#pragma once
#ifndef RM_NETCLIENT_H_920385092345234
#define RM_NETCLIENT_H_920385092345234

#include <NetServer/RM_NetClient_Export.h>
#include <NetServer/RM_NetRenderInfo.h>

#include <Windows.h>

struct ID3D11Texture2D;
struct RM_NetClient;

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT RM_NetClient *
	RM_NetClient_Create();
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT void
	RM_NetClient_Destroy(RM_NetClient * pClient);

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
	RM_NetClient_Connect(RM_NetClient * pClient, bool bIsPrimary);
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT void
	RM_NetClient_Disconnect(RM_NetClient * pClient);
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
    RM_NetClient_IsConnected(RM_NetClient * pClient);

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT void
	RM_NetClient_SetRenderParams(RM_NetClient * pClient, float nearClip, float farClip, float ipd);

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
	RM_NetClient_UpdateRenderInfo(RM_NetClient * pClient);
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT size_t
	RM_NetClient_GetRenderInfoCount(RM_NetClient * pClient);
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT void
RM_NetClient_GetRenderInfo(RM_NetClient * pClient, size_t idx, RM_NetRenderInfo * pRenderInfoOut);

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
    RM_NetClient_RegisterRenderBuffers(RM_NetClient * pClient, ID3D11Texture2D ** pTextures, size_t textureCount);

//! Present the set of render buffers indexed by @a bufferSetIndex
//! @param [in] pRenderPose The pose used to render the provided images (optional)
EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
	RM_NetClient_PresentRenderBuffers(RM_NetClient * pClient, size_t bufferSetIndex, RM_NetClient_PoseState const * pRenderPose);


#endif //RM_NETCLIENT_H_920385092345234