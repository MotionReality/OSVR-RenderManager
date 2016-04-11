#pragma once
#ifndef RM_NETRENDERINFO_H_920385092345234
#define RM_NETRENDERINFO_H_920385092345234

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
    double translation[3]; //!< OSVR_Vec3
    struct //!< OSVR_Quaternion
    {
        double w; 
        double x;
        double y;
        double z;        
    } rotation;
} RM_NetClient_PoseState;

typedef struct
{
    RM_NetClient_ViewportDescription viewport;
    RM_NetClient_PoseState pose;
    RM_NetClient_ProjectionMatrix projection;
} RM_NetRenderInfo;

#endif //RM_NETRENDERINFO_H_920385092345234