/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2025 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "RmlUi_D3D11/ShadersCompiled.h"
#include "RmlUi_Renderer_D3D11.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/SystemInterface.h>

struct PixelShaderDefinition {
	PixelShaderId id;
	Rml::Span<const uint8_t> bytecode;

	const char* debug_name;
};

struct VertexShaderDefinition {
	VertexShaderId id;
	Rml::Span<const uint8_t> bytecode;
	const Rml::Vector<D3D11_INPUT_ELEMENT_DESC> input_layout;

	const char* debug_name;
};

struct BlendStateDefinition {
	BlendStateId id;

	D3D11_BLEND src;
	D3D11_BLEND dst;
	D3D11_BLEND_OP op;

	const char* debug_name;
};

struct DepthStencilStateDefinition {
	DepthStencilStateId id;

	uint8_t read_mask;
	uint8_t write_mask;

	D3D11_COMPARISON_FUNC comp_func;
	D3D11_STENCIL_OP op_fail;
	D3D11_STENCIL_OP op_depth_fail;
	D3D11_STENCIL_OP op_pass;

	const char* debug_name;
};

static const D3D11_INPUT_ELEMENT_DESC input_element_position = //
	{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0};

static const D3D11_INPUT_ELEMENT_DESC input_element_color = //
	{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour), D3D11_INPUT_PER_VERTEX_DATA, 0};

static const D3D11_INPUT_ELEMENT_DESC input_element_texcoord = //
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord), D3D11_INPUT_PER_VERTEX_DATA, 0};

// clang-format off
static const BlendStateDefinition blend_state_definitions[] = {
	{BlendStateId::Main, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, "main"},
	{BlendStateId::Passthrough, D3D11_BLEND_BLEND_FACTOR, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, "passthrough"},
};

static const DepthStencilStateDefinition depth_stencil_state_definitions[] = {
	{DepthStencilStateId::Disable, 0xFF, 0, D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, "disable"},
	{DepthStencilStateId::Set, 0xFF, 0xFF, D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, "set"},
	{DepthStencilStateId::Intersect, 0xFF, 0xFF, D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_INCR, "intersect"},
	{DepthStencilStateId::Test, 0xFF, 0x0, D3D11_COMPARISON_EQUAL, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, "test"},
};

static const PixelShaderDefinition pixel_shader_definitions[] = {
	{PixelShaderId::Color, {shader_pixel_color, sizeof(shader_pixel_color)}, "color"},
	{PixelShaderId::Texture, {shader_pixel_texture, sizeof(shader_pixel_texture)}, "texture"},
	{PixelShaderId::Passthrough, {shader_pixel_passthrough, sizeof(shader_pixel_passthrough)}, "passthrough"},
	{PixelShaderId::Gradient, {shader_pixel_gradient, sizeof(shader_pixel_gradient)}, "gradient"},
	{PixelShaderId::Creation, {shader_pixel_creation, sizeof(shader_pixel_creation)}, "creation"},
	{PixelShaderId::BlendMask, {shader_pixel_blend_mask, sizeof(shader_pixel_blend_mask)}, "blend_mask"},
	{PixelShaderId::ColorMatrix, {shader_pixel_color_matrix, sizeof(shader_pixel_color_matrix)}, "color_matrix"},
	{PixelShaderId::Blur, {shader_pixel_blur, sizeof(shader_pixel_blur)}, "blur"},
	{PixelShaderId::DropShadow, {shader_pixel_drop_shadow, sizeof(shader_pixel_drop_shadow)}, "drop_shadow"},
};

static const VertexShaderDefinition vertex_shader_definitions[] = {
	{VertexShaderId::Main, {shader_vertex_main, sizeof(shader_vertex_main)}, {input_element_position, input_element_color, input_element_texcoord},
		"main"},
	{VertexShaderId::Passthrough, {shader_vertex_passthrough, sizeof(shader_vertex_passthrough)}, {input_element_position, input_element_texcoord},
		"passthrough"},
	{VertexShaderId::Blur, {shader_vertex_blur, sizeof(shader_vertex_blur)}, {input_element_position, input_element_texcoord}, "blur"},
};
// clang-format on

D3D11Pipeline::D3D11Pipeline(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> device_context)
{
	m_device = device;
	m_device_context = device_context;

	Initialize();
}

D3D11Pipeline::~D3D11Pipeline() {}

bool D3D11Pipeline::Initialize()
{
	bool success = false;

	success &= Initialize_Buffers();
	success &= Initialize_States();
	success &= Initialize_Shaders();

	return success;
}

bool D3D11Pipeline::Initialize_Buffers()
{
	HRESULT hr;

	// TODO: refactor
	{
		CD3D11_BUFFER_DESC desc(sizeof(CBuffer_Vertex), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
		hr = m_device->CreateBuffer(&desc, nullptr, m_buffers[BufferId::VertexConstant].GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create vertex constant buffer");
	}

	{
		CD3D11_BUFFER_DESC desc(sizeof(CBuffer_Pixel), D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
		hr = m_device->CreateBuffer(&desc, nullptr, m_buffers[BufferId::PixelConstant].GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create pixel constant buffer");
	}

	return true;
}

bool D3D11Pipeline::Initialize_States()
{
	HRESULT hr;

	m_blend_states[BlendStateId::None] = nullptr;
	for (auto& def : blend_state_definitions)
	{
		D3D11_BLEND_DESC desc;

		desc.AlphaToCoverageEnable = false;
		desc.IndependentBlendEnable = false;
		const D3D11_RENDER_TARGET_BLEND_DESC render_target_blend_desc = {
			true,

			// color
			def.src,
			def.dst,
			def.op,

			// alpha
			def.src,
			def.dst,
			def.op,

			D3D11_COLOR_WRITE_ENABLE_ALL,
		};

		for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			desc.RenderTarget[i] = render_target_blend_desc;

		hr = m_device->CreateBlendState(&desc, m_blend_states[def.id].GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create blend state");
		D3D_SET_OBJECT_NAME(m_blend_states[def.id].Get(), "Blend state %s", def.debug_name);
	}

	for (auto& def : depth_stencil_state_definitions)
	{
		CD3D11_DEPTH_STENCIL_DESC desc(D3D11_DEFAULT);

		desc.DepthEnable = false;
		desc.StencilEnable = true;

		desc.StencilReadMask = def.read_mask;
		desc.StencilWriteMask = def.write_mask;
		desc.FrontFace.StencilFunc = desc.BackFace.StencilFunc = def.comp_func;
		desc.FrontFace.StencilFailOp = desc.BackFace.StencilFailOp = def.op_fail;
		desc.FrontFace.StencilDepthFailOp = desc.BackFace.StencilDepthFailOp = def.op_depth_fail;
		desc.FrontFace.StencilPassOp = desc.BackFace.StencilPassOp = def.op_pass;

		hr = m_device->CreateDepthStencilState(&desc, m_depth_stencil_states[def.id].GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create depth-stencil state");
		D3D_SET_OBJECT_NAME(m_depth_stencil_states[def.id].Get(), "Depth-stencil state %s", def.debug_name);
	}

	{
		CD3D11_SAMPLER_DESC desc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
			0.f, 0, D3D11_COMPARISON_ALWAYS, nullptr, 0.f, 0.f);

		hr = m_device->CreateSamplerState(&desc, m_sampler_state.GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create sampler state");
	}

	{
		CD3D11_RASTERIZER_DESC desc(D3D11_DEFAULT);
		desc.CullMode = D3D11_CULL_NONE;
		desc.DepthClipEnable = false;
		desc.ScissorEnable = true;
		desc.MultisampleEnable = true;

		hr = m_device->CreateRasterizerState(&desc, m_rasterizer_state.GetAddressOf());
		CHECK_HRESULT(hr, "Failed to create rasterizer state");
	}

	return true;
}

bool D3D11Pipeline::Initialize_Shaders()
{
	HRESULT hr;

	for (auto& def : pixel_shader_definitions)
	{
		auto vertex_shader = m_pixel_shaders[def.id].GetAddressOf();
		hr = m_device->CreatePixelShader(def.bytecode.data(), def.bytecode.size(), nullptr, vertex_shader);
		CHECK_HRESULT(hr, "Failed to create pixel shader");
		D3D_SET_OBJECT_NAME(m_pixel_shaders[def.id].Get(), "Pixel shader %s", def.debug_name);
	}

	for (auto& def : vertex_shader_definitions)
	{
		auto vertex_shader = m_vertex_shaders[def.id].GetAddressOf();
		hr = m_device->CreateVertexShader(def.bytecode.data(), def.bytecode.size(), nullptr, vertex_shader);
		CHECK_HRESULT(hr, "Failed to create vertex shader");
		D3D_SET_OBJECT_NAME(m_vertex_shaders[def.id].Get(), "Vertex shader %s", def.debug_name);

		auto input_layout = m_input_layouts[def.id].GetAddressOf();
		hr = m_device->CreateInputLayout(def.input_layout.data(), def.input_layout.size(), def.bytecode.data(), def.bytecode.size(), input_layout);
		CHECK_HRESULT(hr, "Failed to create input layout");
		D3D_SET_OBJECT_NAME(m_input_layouts[def.id].Get(), "Input layout %s", def.debug_name);
	}

	return true;
}

void D3D11Pipeline::BeginFrame(int width, int height)
{
	m_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_device_context->PSSetSamplers(0, 1, m_sampler_state.GetAddressOf());

	m_device_context->VSSetConstantBuffers(0, 1, m_buffers[BufferId::VertexConstant].GetAddressOf());
	m_device_context->PSSetConstantBuffers(0, 1, m_buffers[BufferId::PixelConstant].GetAddressOf());

	m_device_context->RSSetState(m_rasterizer_state.Get());
	PushViewport(Rml::Rectanglei::FromSize({width, height}));
}

void D3D11Pipeline::EndFrame()
{
	m_blend_state_stack = {};
	// m_depth_stencil_stack = {};
	m_pixel_shader_stack = {};
	m_vertex_shader_stack = {};
	PopViewport();
}

Rml::UniquePtr<D3D11Map> D3D11Pipeline::Map(BufferId id)
{
	auto map = Rml::MakeUnique<D3D11Map>(id, this);

	HRESULT hr = m_device_context->Map(m_buffers[id].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map->m_subresource);
	return map;
}

void D3D11Pipeline::Unmap(D3D11Map* map)
{
	m_device_context->Unmap(m_buffers[map->m_id].Get(), 0);
}

#pragma region Blend state
void D3D11Pipeline::UseBlendState(BlendState& state)
{
	m_device_context->OMSetBlendState(m_blend_states[state.id].Get(), state.factor, 0xFFFFFFFF);
}

void D3D11Pipeline::PushBlendState(BlendStateId id, Rml::Colourf factor)
{
	m_blend_state_stack.push({id, {factor.red, factor.green, factor.blue, factor.alpha}});
	if (!m_blend_state_stack.empty())
		UseBlendState(m_blend_state_stack.top());
}

void D3D11Pipeline::PopBlendState(int count)
{
	for (int i = 0; i < count; i++)
		m_blend_state_stack.pop();

	if (!m_blend_state_stack.empty())
		UseBlendState(m_blend_state_stack.top());
}
#pragma endregion

#pragma region Depth-stencil state
void D3D11Pipeline::UseDepthStencilState(DepthStencilStateId id, unsigned int stencil_ref)
{
	m_device_context->OMSetDepthStencilState(m_depth_stencil_states[id].Get(), stencil_ref);
}

/* void D3D11Pipeline::UseDepthStencilState(DepthStencilState& state)
{
    m_device_context->OMSetDepthStencilState(m_depth_stencil_states[state.id].Get(), state.stencil_ref);
}

void D3D11Pipeline::PushDepthStencilState(DepthStencilStateId id, unsigned int stencil_ref)
{
    m_depth_stencil_stack.push({id, stencil_ref});
    if (!m_depth_stencil_stack.empty())
        UseDepthStencilState(m_depth_stencil_stack.top());
}

void D3D11Pipeline::PopDepthStencilState(int count)
{
    for (int i = 0; i < count; i++)
        m_depth_stencil_stack.pop();

    if (!m_depth_stencil_stack.empty())
        UseDepthStencilState(m_depth_stencil_stack.top());
} */
#pragma endregion

#pragma region Pixel shader
void D3D11Pipeline::UsePixelShader(PixelShaderId id)
{
	m_device_context->PSSetShader(m_pixel_shaders.at(id).Get(), nullptr, 0);
}

void D3D11Pipeline::PushPixelShader(PixelShaderId id)
{
	m_pixel_shader_stack.push(id);
	if (!m_pixel_shader_stack.empty())
		UsePixelShader(m_pixel_shader_stack.top());
}

void D3D11Pipeline::PopPixelShader(int count)
{
	for (int i = 0; i < count; i++)
		m_pixel_shader_stack.pop();

	ID3D11ShaderResourceView* null[128] = {nullptr};
	m_device_context->PSSetShaderResources(0, 128, null);

	if (!m_pixel_shader_stack.empty())
		UsePixelShader(m_pixel_shader_stack.top());
}
#pragma endregion

#pragma region Vertex shader
void D3D11Pipeline::UseVertexShader(VertexShaderId id)
{
	m_device_context->IASetInputLayout(m_input_layouts.at(id).Get());
	m_device_context->VSSetShader(m_vertex_shaders.at(id).Get(), nullptr, 0);
}

void D3D11Pipeline::PushVertexShader(VertexShaderId id)
{
	m_vertex_shader_stack.push(id);
	if (!m_vertex_shader_stack.empty())
		UseVertexShader(m_vertex_shader_stack.top());
}

void D3D11Pipeline::PopVertexShader(int count)
{
	for (int i = 0; i < count; i++)
		m_vertex_shader_stack.pop();

	if (!m_vertex_shader_stack.empty())
		UseVertexShader(m_vertex_shader_stack.top());
}
#pragma endregion

#pragma region Viewport
void D3D11Pipeline::UseViewport(Rml::Rectanglei viewport)
{
	CD3D11_VIEWPORT d3d_viewport(viewport.p0.x, viewport.p0.y, viewport.Width(), viewport.Height());
	m_device_context->RSSetViewports(1, &d3d_viewport);
}

void D3D11Pipeline::PushViewport(Rml::Rectanglei viewport)
{
	m_viewport_stack.push(viewport);
	if (!m_viewport_stack.empty())
		UseViewport(m_viewport_stack.top());
}

void D3D11Pipeline::PopViewport(int count)
{
	for (int i = 0; i < count; i++)
		m_viewport_stack.pop();

	if (!m_viewport_stack.empty())
		UseViewport(m_viewport_stack.top());
}
#pragma endregion

RenderInterface_D3D11::Texture::Texture(ID3D11Device* device, Rml::Vector2i dimensions, DXGI_FORMAT format, unsigned int sample_count,
	D3D11_SUBRESOURCE_DATA* initial_data)
{
	RMLUI_ASSERT(dimensions.x > 0 && dimensions.y > 0 && sample_count > 0);
	HRESULT hr;

	{
		CD3D11_TEXTURE2D_DESC desc(format, dimensions.x, dimensions.y, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
		desc.SampleDesc = {sample_count, 0};

		hr = device->CreateTexture2D(&desc, initial_data, texture.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create texture");
	}

	{
		CD3D11_SHADER_RESOURCE_VIEW_DESC desc(sample_count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D, format);

		hr = device->CreateShaderResourceView(texture.Get(), &desc, srv.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create shader resource view");
	}

	{
		CD3D11_RENDER_TARGET_VIEW_DESC desc(sample_count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D, format);

		hr = device->CreateRenderTargetView(texture.Get(), &desc, rtv.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create render target view");
	}
}

/* RenderInterface_D3D11::Texture::Texture(ID3D11Device* device, ID3D11Texture2D* texture)
{
    HRESULT hr;

    this->texture = texture;

    D3D11_TEXTURE2D_DESC texture_desc;
    texture->GetDesc(&texture_desc);

    bool multisampled = texture_desc.SampleDesc.Count > 1;

    if (texture_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
    {
        CD3D11_SHADER_RESOURCE_VIEW_DESC desc{
            multisampled ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D,
            texture_desc.Format,
        };

        hr = device->CreateShaderResourceView(texture, &desc, srv.GetAddressOf());
        CHECK_HRESULT_VOID(hr, "Failed to create shader resource view");
    }

    if (texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET)
    {
        CD3D11_RENDER_TARGET_VIEW_DESC desc{
            multisampled ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D,
            texture_desc.Format,
        };

        hr = device->CreateRenderTargetView(texture, &desc, rtv.GetAddressOf());
        CHECK_HRESULT_VOID(hr, "Failed to create render target view");
    }
} */

RenderInterface_D3D11::DepthStencilTexture::DepthStencilTexture(ID3D11Device* device, Rml::Vector2i dimensions, DXGI_FORMAT format,
	unsigned int sample_count) : Texture(device, dimensions, format, sample_count)
{
	HRESULT hr;

	{
		CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_D24_UNORM_S8_UINT, dimensions.x, dimensions.y, 1, 1, D3D11_BIND_DEPTH_STENCIL);
		desc.SampleDesc = {sample_count, 0};

		hr = device->CreateTexture2D(&desc, nullptr, texture_depth_stencil.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create texture");
	}

	{
		hr = device->CreateDepthStencilView(texture_depth_stencil.Get(), nullptr, dsv.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create depth stencil view");
	}
}