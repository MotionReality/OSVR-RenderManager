#pragma once
#ifndef RM_NETCLIENT_H_920385092345234
#define RM_NETCLIENT_H_920385092345234

#include <NetServer/RM_NetClient_Export.h>

#include <Windows.h>

struct ID3D11Texture2D;
struct RM_NetClient;

/// Description needed to construct an off-axes projection matrix
typedef struct RM_NetClient_ProjectionMatrix {
    double left;
    double right;
    double top;
    double bottom;
    double nearClip; //< Cannot name "near" because Visual Studio keyword
    double farClip;
} RM_NetClient_ProjectionMatrix;

//=========================================================================
/// Viewport description with lower-left corner of the screen as (0,0)
typedef struct RM_NetClient_ViewportDescription {
    double left;   //< Left side of the viewport in pixels
    double lower;  //< First pixel in the viewport at the bottom.
    double width;  //< Last pixel in the viewport at the top
    double height; //< Last pixel on the right of the viewport in pixels
} RM_NetClient_ViewportDescription;

typedef struct RM_NetClient_PoseState
{
    double translation[3]; // OSVR_Vec3
    double rotation[4]; // OSVR_Quaternion
} RM_NetClient_PoseState;

typedef struct
{
    RM_NetClient_ViewportDescription viewport;
    RM_NetClient_PoseState pose;
    RM_NetClient_ProjectionMatrix projection;
} RM_NetRenderInfo;

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

EXTERN_C OSVR_RENDERMANAGER_NETCLIENT_EXPORT bool
	RM_NetClient_PresentRenderBuffers(RM_NetClient * pClient, size_t bufferSetIndex);


#endif //RM_NETCLIENT_H_920385092345234