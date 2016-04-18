/** @file
@brief Header file describing the OSVR direct-to-device rendering interface for
D3D

@date 2015

@author
Sensics, Inc.
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

#pragma once
#include <osvr/ClientKit/Context.h>
#include <osvr/ClientKit/Interface.h>
#include "RenderManager.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl.h>
#include <DirectXMath.h>
#endif

#include <vector>
#include <string>

namespace osvr {
namespace renderkit {

    class RenderManagerD3D11Base : public RenderManager {
      public:
        virtual ~RenderManagerD3D11Base();

        // Is the renderer currently working?
        bool doingOkay() override { return m_doingOkay; }

        // Creates the D3D11 device and context to be used
        // to draw things into our window unless they have
        // already been filled in.
        virtual bool SetDeviceAndContext();

        // Opens the D3D renderer we're going to use.
        OpenResults OpenDisplay() override;

      protected:
        /// Construct a D3D RenderManager.
        RenderManagerD3D11Base(
            OSVR_ClientContext context,
            ConstructorParameters p);

        virtual bool UpdateDistortionMeshesInternal(
            DistortionMeshType type //< Type of mesh to produce
            ,
            std::vector<DistortionParameters> const&
                distort //< Distortion parameters
            ) override;

        /// Call before calling OpenDisplay() to set the DXGIAdapter if you
        /// don't want the default one.
        void setAdapter(Microsoft::WRL::ComPtr<IDXGIAdapter> const& adapter);

        /// Get the D3D11 Device as a IDXGIDevice
        Microsoft::WRL::ComPtr<IDXGIDevice> getDXGIDevice();

        /// Get the adapter, whether manually specified or automatically
        /// determined.
        Microsoft::WRL::ComPtr<IDXGIAdapter> getDXGIAdapter();

        /// Get the DXGIFactor1 corresponding to the adapter.
        Microsoft::WRL::ComPtr<IDXGIFactory1> getDXGIFactory();

        bool m_doingOkay;   //< Are we doing okay?
        bool m_displayOpen; //< Has our display been opened?

        /// The adapter, if and only if explicitly set.
        Microsoft::WRL::ComPtr<IDXGIAdapter> m_adapter;

        // D3D-related state information
        /// @todo Release these pointers in destructor
        ID3D11Device* m_D3D11device; //< Pointer to the D3D11 device to use.
        ID3D11DeviceContext*
            m_D3D11Context; //< Pointer to the D3D11 context to use.

        ID3D11Query* m_completionQuery;
        bool m_completionQueryPending;

        //============================================================================
        // Information needed to provide render and depth/stencil buffers for
        // each of the eyes we give to the user to use when rendering.  This is
        // for user code to render into.
        //   This is only used in the non-present-mode interface.
        std::vector<osvr::renderkit::RenderBuffer> m_renderBuffers;
        ID3D11DepthStencilState* m_depthStencilStateForRender;

        /// Construct the buffers we're going to use in Render() mode, which
        /// we use to actually use the Presentation mode.  This gives us the
        /// main Presentation path as the basic approach which we can build on
        /// top of, and also lets us make the intermediate buffers the correct
        /// size we need for Asychronous Time Warp and distortion, and keeps
        /// them from being in the same window and so bleeding together.
        bool constructRenderBuffers();

        //============================================================================
        // Information needed to render to the final output buffer.  Render
        // state and geometries needed to go from the presented buffers to the
        // screen.
        struct XMFLOAT3 {
            float x;
            float y;
            float z;
        };
        struct XMFLOAT2 {
            float x;
            float y;
        };
        struct DistortionVertex {
            XMFLOAT3 Pos;
            XMFLOAT2 TexR;
            XMFLOAT2 TexG;
            XMFLOAT2 TexB;
        };
        // @todo release these in destructor
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_renderTextureSamplerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_cbPerObjectBuffer;
        ID3D11InputLayout* m_vertexLayout;
        // @todo One per eye/display combination in case of multiple displays
        // per eye
        std::vector<ID3D11Buffer*>
            m_quadVertexBuffer; //< Used to render quads for present mode
        std::vector<UINT>
            m_quadVertexCount; //< How many vertices in our quad array
        std::vector<DistortionVertex*>
            m_triangleBuffer; //< Points to our triangle array buffers
        // @todo Remove as redundant with m_quadVertexCount, here and in OpenGL
        std::vector<size_t>
            m_numTriangles; //< Number of triangles in our array buffers

        ID3D11DepthStencilState* m_depthStencilStateForPresent; // Depth/stencil
                                                                // state that
                                                                // disables both

        // Type of matrices that we pass as uniform parameters to the shader.
        struct cbPerObject {
            DirectX::XMMATRIX projection;
            DirectX::XMMATRIX modelView;
            DirectX::XMMATRIX texture;
        };

        /// We can't use an OpenGL-compliant texture warp matrix, so need to
        /// override it here.
        bool
        ComputeAsynchronousTimeWarps(std::vector<RenderInfo> usedRenderInfo,
                                     std::vector<RenderInfo> currentRenderInfo,
                                     float assumedDepth = 2.0f) override;

        //===================================================================
        // Overloaded render functions from the base class.  Not all of the
        // ones that need overloading are here; derived classes must decide
        // what to do for those.
        bool RenderPathSetup() override;
        bool RenderEyeInitialize(size_t eye) override;
        bool RenderSpace(size_t whichSpace //< Index into m_callbacks vector
                         ,
                         size_t whichEye //< Which eye are we rendering for?
                         ,
                         OSVR_PoseState pose //< ModelView transform to use
                         ,
                         OSVR_ViewportDescription viewport //< Viewport to use
                         ,
                         OSVR_ProjectionMatrix projection //< Projection to use
                         ) override;

        bool RenderFrameInitialize() override;
        bool RenderDisplayFinalize(size_t display) override;
        bool RenderFrameFinalize() override;

        bool PresentFrameInitialize() override;
        bool PresentEye(PresentEyeParameters params) override;

        bool PresentDisplayCommit(size_t display) override;
        bool PresentFrameCommit() override;
        bool PresentFrameFinalize() override = 0;

        void WaitForFrameCompletion();

        friend class RenderManagerD3D11OpenGL;
        friend class RenderManagerD3D11ATW;
    };

} // namespace renderkit
} // namespace osvr
