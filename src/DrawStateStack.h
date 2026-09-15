#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <algorithm>

// Faithful port of Luma's DrawStateStack<FullGraphics> (Source/Core/utils/draw.hpp).
//
// Luma caches the full graphics pipeline state before running an injected
// draw, then restores it after — so the engine's subsequent draws see no
// state drift. This is critical: injected passes (ModulateLighting, SMAA)
// rebind RTVs, SRVs, shaders, viewports, samplers, blend/depth-stencil state,
// and the engine would malfunction if any of that leaked.
//
// Cache() captures: all RTVs + DSV, all UAVs (from the first valid RTV slot
// onward), blend state + factor + sample mask, primitive topology, scissor
// rects, viewports, PS SRVs (all slots), PS + VS constant buffers (all slots,
// with first/num constants if DeviceContext1), depth-stencil state + ref,
// VS + PS shaders, PS samplers (all slots), IA input layout, rasterizer state.
//
// Restore() rebinds them in the same order Luma does: output targets first
// (so SRV bindings of the same resource get nulled correctly), then the rest.
//
// Heaped State (not inlined on the stack) because the SRV/sampler/CB arrays
// are large — same reasoning as Luma's std::unique_ptr<State>.

class DrawStateStack {
public:
    static constexpr UINT kSamplersNum = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;   // 128
    static constexpr UINT kSrvNum = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; // 128
    static constexpr UINT kUavNum = D3D11_1_UAV_SLOT_COUNT;                       // 64

    void Cache(ID3D11DeviceContext* ctx, UINT deviceMaxUavNum) {
        state = std::make_unique<State>();
        state->uav_num = deviceMaxUavNum ? deviceMaxUavNum : kUavNum;

        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ctx1;
        ctx->QueryInterface(IID_PPV_ARGS(&ctx1));

        // OMGetRenderTargets for RTVs + DSV, then OMGetRenderTargetsAndUnorderedAccessViews
        // for the UAV tail (matching Luma's two-call pattern).
        ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                &state->render_target_views[0], &state->depth_stencil_view);
        for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) {
            bool rtvEmpty = state->render_target_views[i].Get() == nullptr;
            if (!rtvEmpty) {
                state->render_target_views[i].Reset(); // re-set below via the AndUnorderedAccessViews call
                state->valid_render_target_views_bound = i + 1;
            }
        }
        state->depth_stencil_view.Reset();
        ctx->OMGetRenderTargetsAndUnorderedAccessViews(
            state->valid_render_target_views_bound, &state->render_target_views[0],
            &state->depth_stencil_view, state->valid_render_target_views_bound,
            state->uav_num - state->valid_render_target_views_bound,
            &state->unordered_access_views[0]);

        ctx->OMGetBlendState(&state->blend_state, state->blend_factor, &state->blend_sample_mask);
        ctx->IAGetPrimitiveTopology(&state->primitive_topology);
        ctx->RSGetScissorRects(&state->scissor_rects_num, nullptr);
        if (state->scissor_rects_num > 0)
            ctx->RSGetScissorRects(&state->scissor_rects_num, &state->scissor_rects[0]);
        ctx->RSGetViewports(&state->viewports_num, nullptr);
        if (state->viewports_num > 0)
            ctx->RSGetViewports(&state->viewports_num, &state->viewports[0]);
        ctx->PSGetShaderResources(0, kSrvNum, &state->shader_resource_views[0]);
        if (ctx1) {
            ctx1->PSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                &state->constant_buffers[0], state->constant_buffers_first_constant,
                state->constant_buffers_num_constant);
            ctx1->VSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                &state->vs_constant_buffers[0], state->vs_constant_buffers_first_constant,
                state->vs_constant_buffers_num_constant);
        } else {
            ctx->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                &state->constant_buffers[0]);
            ctx->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                &state->vs_constant_buffers[0]);
        }
        ctx->OMGetDepthStencilState(&state->depth_stencil_state, &state->stencil_ref);
        ctx->VSGetShader(&state->vs, nullptr, 0);
        ctx->PSGetShader(&state->ps, nullptr, 0);
        ctx->PSGetSamplers(0, kSamplersNum, &state->samplers_state[0]);
        ctx->IAGetInputLayout(&state->input_layout);
        ctx->RSGetState(&state->rasterizer_state);
    }

    void Restore(ID3D11DeviceContext* ctx, bool outputTextures = true, bool shaders = true) {
        if (!state) return;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ctx1;
        ctx->QueryInterface(IID_PPV_ARGS(&ctx1));

        if (outputTextures) {
            // Output targets first: rebinding an RTV nulls any SRV of the same
            // resource, so doing RTVs before SRVs avoids a brief conflict.
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                rtvs[i] = state->render_target_views[i].Get();
            ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT];
            for (UINT i = 0; i < state->uav_num; i++)
                uavs[i] = state->unordered_access_views[i].Get();
            UINT uavInitialCounts[D3D11_1_UAV_SLOT_COUNT];
            std::fill(std::begin(uavInitialCounts), std::end(uavInitialCounts), UINT(-1));
            ctx->OMSetRenderTargetsAndUnorderedAccessViews(
                state->valid_render_target_views_bound, rtvs, state->depth_stencil_view.Get(),
                state->valid_render_target_views_bound,
                state->uav_num - state->valid_render_target_views_bound, uavs, uavInitialCounts);
        }
        ctx->OMSetBlendState(state->blend_state.Get(), state->blend_factor, state->blend_sample_mask);
        ctx->IASetPrimitiveTopology(state->primitive_topology);
        if (state->scissor_rects_num > 0)
            ctx->RSSetScissorRects(state->scissor_rects_num, &state->scissor_rects[0]);
        if (state->viewports_num > 0)
            ctx->RSSetViewports(state->viewports_num, &state->viewports[0]);
        ID3D11ShaderResourceView* srvs[kSrvNum];
        for (UINT i = 0; i < kSrvNum; i++) srvs[i] = state->shader_resource_views[i].Get();
        ctx->PSSetShaderResources(0, kSrvNum, srvs);
        ID3D11Buffer* cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            cbs[i] = state->constant_buffers[i].Get();
        ID3D11Buffer* vscbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            vscbs[i] = state->vs_constant_buffers[i].Get();
        if (ctx1) {
            ctx1->PSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                cbs, state->constant_buffers_first_constant, state->constant_buffers_num_constant);
            ctx1->VSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,
                vscbs, state->vs_constant_buffers_first_constant, state->vs_constant_buffers_num_constant);
        } else {
            ctx->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, cbs);
            ctx->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, vscbs);
        }
        ctx->OMSetDepthStencilState(state->depth_stencil_state.Get(), state->stencil_ref);
        if (shaders) {
            ctx->VSSetShader(state->vs.Get(), nullptr, 0);
            ctx->PSSetShader(state->ps.Get(), nullptr, 0);
        }
        ID3D11SamplerState* smps[kSamplersNum];
        for (UINT i = 0; i < kSamplersNum; i++) smps[i] = state->samplers_state[i].Get();
        ctx->PSSetSamplers(0, kSamplersNum, smps);
        ctx->IASetInputLayout(state->input_layout.Get());
        ctx->RSSetState(state->rasterizer_state.Get());
    }

    bool IsValid() const { return state != nullptr; }

private:
    struct State {
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state;
        FLOAT blend_factor[4] = {1.f, 1.f, 1.f, 1.f};
        UINT blend_sample_mask = 0xFFFFFFFF;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;
        D3D11_PRIMITIVE_TOPOLOGY primitive_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_stencil_state;
        UINT stencil_ref = 0;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_stencil_view;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> samplers_state[kSamplersNum];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shader_resource_views[kSrvNum];
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> unordered_access_views[kUavNum];
        Microsoft::WRL::ComPtr<ID3D11Buffer> constant_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        UINT constant_buffers_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        UINT constant_buffers_num_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        Microsoft::WRL::ComPtr<ID3D11Buffer> vs_constant_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
        UINT vs_constant_buffers_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        UINT vs_constant_buffers_num_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
        D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT scissor_rects_num = 0;
        D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT viewports_num = 1;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> input_layout;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state;
        UINT valid_render_target_views_bound = 0;
        UINT uav_num = kUavNum;
    };
    std::unique_ptr<State> state;
};
