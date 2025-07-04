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

#include "RmlUi_Renderer_D3D11.h"
#include "RmlUi/Core/Mesh.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi_Include_Windows.h"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Platform.h>
#include <RmlUi/Core/SystemInterface.h>

// Determines the anti-aliasing quality when creating layers. Enables better-looking visuals, especially when transforms are applied.
static constexpr unsigned int NUM_MSAA_SAMPLES = 2;

static const DXGI_FORMAT back_buffer_format = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT depth_stencil_buffer_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
static const DXGI_SAMPLE_DESC sample_desc{1, 0};
static const DXGI_SAMPLE_DESC msaa_sample_desc{NUM_MSAA_SAMPLES, 0};

static const float clear_color[4] = {0.f, 0.f, 0.f, 0.f};

RenderInterface_D3D11::RenderLayerStack::RenderLayerStack(int width, int height, ComPtr<ID3D11Device> device)
{
	m_width = width;
	m_height = height;

	m_device = device;

	HRESULT hr;
	{
		CD3D11_TEXTURE2D_DESC desc(DXGI_FORMAT_D24_UNORM_S8_UINT, m_width, m_height, 1, 1, D3D11_BIND_DEPTH_STENCIL);
		desc.SampleDesc = msaa_sample_desc;

		hr = m_device->CreateTexture2D(&desc, nullptr, m_layers_depth_stencil.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create depth stencil");
	}

	{
		hr = m_device->CreateDepthStencilView(m_layers_depth_stencil.Get(), nullptr, m_layers_dsv.GetAddressOf());
		CHECK_HRESULT_VOID(hr, "Failed to create depth stencil view");
	}

	int i = 0;
	for (auto& fb : fb_postprocess)
	{
		fb = DepthStencilTexture{m_device.Get(), {m_width, m_height}, back_buffer_format, 1};

		D3D_SET_OBJECT_NAME(fb.texture.Get(), "PostProcess #%d (texture)", i);
		D3D_SET_OBJECT_NAME(fb.srv.Get(), "PostProcess #%d (srv)", i);
		D3D_SET_OBJECT_NAME(fb.rtv.Get(), "PostProcess #%d (rtv)", i);

		D3D_SET_OBJECT_NAME(fb.texture_depth_stencil.Get(), "PostProcess #%d (depthstencil)", layers_size);
		D3D_SET_OBJECT_NAME(fb.dsv.Get(), "PostProcess #%d (dsv)", layers_size);

		i++;
	}
}

Rml::LayerHandle RenderInterface_D3D11::RenderLayerStack::PushLayer()
{
	RMLUI_ASSERT(layers_size <= static_cast<int>(fb_layers.size()));

	if (layers_size == static_cast<int>(fb_layers.size()))
	{
		fb_layers.push_back(Texture{m_device.Get(), {m_width, m_height}, back_buffer_format, NUM_MSAA_SAMPLES});

		D3D_SET_OBJECT_NAME(fb_layers.back().texture.Get(), "Layer #%d (texture)", layers_size);
		D3D_SET_OBJECT_NAME(fb_layers.back().srv.Get(), "Layer #%d (srv)", layers_size);
		D3D_SET_OBJECT_NAME(fb_layers.back().rtv.Get(), "Layer #%d (rtv)", layers_size);
	}

	layers_size += 1;

	return GetTopLayerHandle();
}

void RenderInterface_D3D11::RenderLayerStack::PopLayer()
{
	RMLUI_ASSERT(layers_size > 0);
	layers_size -= 1;
}

const RenderInterface_D3D11::Texture& RenderInterface_D3D11::RenderLayerStack::GetLayer(Rml::LayerHandle layer) const
{
	RMLUI_ASSERT(static_cast<size_t>(layer) < static_cast<size_t>(layers_size));
	return fb_layers.at(layer);
}

const RenderInterface_D3D11::Texture& RenderInterface_D3D11::RenderLayerStack::GetTopLayer() const
{
	return GetLayer(GetTopLayerHandle());
}

Rml::LayerHandle RenderInterface_D3D11::RenderLayerStack::GetTopLayerHandle() const
{
	RMLUI_ASSERT(layers_size > 0);
	return layers_size - 1;
}

void RenderInterface_D3D11::RenderLayerStack::SwapPostprocessPrimarySecondary()
{
	std::swap(fb_postprocess[0], fb_postprocess[1]);
}

RenderInterface_D3D11::RenderInterface_D3D11() {}

RenderInterface_D3D11::~RenderInterface_D3D11() {}

bool RenderInterface_D3D11::Initialize(ComPtr<IDXGISwapChain1> swap_chain, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> device_context)
{
	m_swap_chain = swap_chain;
	m_device = device;
	m_device_context = device_context;
	m_device_context.As(&m_annotation);

	Rml::Mesh mesh;
	Rml::Vector2f origin;
	Rml::Vector2f dimensions;
	Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1, 1), Rml::Vector2f(2, -2), {255, 127, 0});

	m_fullscreen_quad_geometry = CompileGeometry(mesh.vertices, mesh.indices);

	return true;
}

void RenderInterface_D3D11::Shutdown()
{
	if (m_fullscreen_quad_geometry)
	{
		ReleaseGeometry(m_fullscreen_quad_geometry);
		m_fullscreen_quad_geometry = {};
	}

	delete m_pipeline;
	delete m_render_layers;
}

void RenderInterface_D3D11::BeginFrame()
{
	m_pipeline->BeginFrame(m_viewport.x, m_viewport.y); // TODO move m_viewport to pipeline, it is pipeline's job to track the viewport

	m_pipeline->PushBlendState(BlendStateId::Main, {});
	m_pipeline->PushVertexShader(VertexShaderId::Main);

	m_stencil_ref = 0;
	m_pipeline->UseDepthStencilState(DepthStencilStateId::Disable, m_stencil_ref);

	EnableScissorRegion(false);
	EnableClipMask(false);
	SetTransform(nullptr);

	m_render_layers->PushLayer();
	BEGIN_EVENT("Layer #%d", 0); // TODO move to RenderLayerStack

	auto& layer = m_render_layers->GetTopLayer();
	m_device_context->OMSetRenderTargets(1, layer.rtv.GetAddressOf(), m_render_layers->m_layers_dsv.Get());
	// m_device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_STENCIL, 1.0, 0);
	m_device_context->ClearRenderTargetView(layer.rtv.Get(), clear_color);
}

void RenderInterface_D3D11::EndFrame()
{
	auto& layer = m_render_layers->GetTopLayer();

	m_device_context->ResolveSubresource(m_back_buffer.Get(), 0, layer.texture.Get(), 0, back_buffer_format);
	m_swap_chain->Present(1, 0);

	m_render_layers->PopLayer();

	m_pipeline->EndFrame();
}

void RenderInterface_D3D11::SetViewport(int width, int height)
{
	m_viewport.x = Rml::Math::Max(width, 1);
	m_viewport.y = Rml::Math::Max(height, 1);
	m_projection = Rml::Matrix4f::ProjectOrtho(0, m_viewport.x, m_viewport.y, 0, -10000, 10000);

	HRESULT hr;

	if (!m_swap_chain)
		return;

	delete m_pipeline;
	delete m_render_layers;

	m_back_buffer.Reset();

	hr = m_swap_chain->ResizeBuffers(0, m_viewport.x, m_viewport.y, back_buffer_format, 0);
	CHECK_HRESULT_VOID(hr, "Failed to resize buffers");

	hr = m_swap_chain->GetBuffer(0, IID_PPV_ARGS(m_back_buffer.GetAddressOf()));
	CHECK_HRESULT_VOID(hr, "Failed to get back buffer");

	CD3D11_TEXTURE2D_DESC texture_desc(DXGI_FORMAT_UNKNOWN, m_viewport.x, m_viewport.y, 1, 1);
	texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

	m_render_layers = new RenderLayerStack(m_viewport.x, m_viewport.y, m_device);
	m_pipeline = new D3D11Pipeline(m_device, m_device_context);
}

void RenderInterface_D3D11::GetViewport(int& width, int& height)
{
	width = m_viewport.x;
	height = m_viewport.y;
}

Rml::Image RenderInterface_D3D11::CaptureScreen()
{
	MARK_EVENT;

	CD3D11_TEXTURE2D_DESC desc(back_buffer_format, m_viewport.x, m_viewport.y, 1, 1, 0);
	desc.SampleDesc = sample_desc;

	ComPtr<ID3D11Texture2D> default_texture;
	{
		desc.Usage = D3D11_USAGE_DEFAULT;

		CHECK_HRESULT_EMPTY(m_device->CreateTexture2D(&desc, nullptr, default_texture.GetAddressOf()), "Failed to create texture");
	}

	ComPtr<ID3D11Texture2D> staging_texture;
	{
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.Usage = D3D11_USAGE_STAGING;

		CHECK_HRESULT_EMPTY(m_device->CreateTexture2D(&desc, nullptr, staging_texture.GetAddressOf()), "Failed to create staging texture");
	}

	m_device_context->ResolveSubresource(default_texture.Get(), 0, m_back_buffer.Get(), 0, back_buffer_format);
	m_device_context->CopyResource(staging_texture.Get(), default_texture.Get());

	Rml::Image image;
	image.num_components = 3;
	// TODO replace to Vector2i
	image.width = m_viewport.x;
	image.height = m_viewport.y;

	if (image.width < 1 || image.height < 1)
		return {};

	const int byte_size = image.width * image.height * image.num_components;
	image.data = Rml::UniquePtr<Rml::byte[]>(new Rml::byte[byte_size]);

	{
		D3D11_MAPPED_SUBRESOURCE res;
		CHECK_HRESULT_EMPTY(m_device_context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &res), "Failed to map staging texture");

		for (int y = 0; y < m_viewport.y; y++)
		{
			for (int x = 0; x < m_viewport.x; x++)
			{
				int src_i = (y * res.RowPitch) + (x * 4);     // rgba
				int dst_i = (y * m_viewport.x * 3) + (x * 3); // rgb

				image.data[dst_i] = reinterpret_cast<Rml::byte*>(res.pData)[src_i];
				image.data[dst_i + 1] = reinterpret_cast<Rml::byte*>(res.pData)[src_i + 1];
				image.data[dst_i + 2] = reinterpret_cast<Rml::byte*>(res.pData)[src_i + 2];
			}
		}

		m_device_context->Unmap(staging_texture.Get(), 0);
	}

	return image;
}

Rml::CompiledGeometryHandle RenderInterface_D3D11::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	CompiledGeometry* geometry = new CompiledGeometry();

	geometry->index_count = indices.size();

	D3D11_SUBRESOURCE_DATA subresource_data{nullptr, 0, 0};

	{
		CD3D11_BUFFER_DESC desc(sizeof(Rml::Vertex) * vertices.size(), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE);
		subresource_data.pSysMem = vertices.data();
		m_device->CreateBuffer(&desc, &subresource_data, geometry->vertex_buffer.GetAddressOf());
	}

	{
		CD3D11_BUFFER_DESC desc(sizeof(int) * indices.size(), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE);
		subresource_data.pSysMem = indices.data();
		m_device->CreateBuffer(&desc, &subresource_data, geometry->index_buffer.GetAddressOf());
	}

	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RenderInterface_D3D11::RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture_handle)
{
	CompiledGeometry* compiled_geometry = reinterpret_cast<CompiledGeometry*>(geometry);
	Texture* texture = nullptr;

	PixelShaderId shader_id;

	if (texture_handle == TexturePostprocess)
	{
		shader_id = PixelShaderId::None;
	}
	else if (texture_handle)
	{
		shader_id = PixelShaderId::Texture;
		if (texture_handle != TextureEnableWithoutBinding)
		{
			texture = reinterpret_cast<Texture*>(texture_handle);
			m_device_context->PSSetShaderResources(0, 1, texture->srv.GetAddressOf());
		}
	}
	else
	{
		shader_id = PixelShaderId::Color;
	}

	// TODO probably do smth like m_pipeline->CurrentVertexShader
	if (shader_id == PixelShaderId::Texture || shader_id == PixelShaderId::Color)
	{
		auto map = m_pipeline->Map(BufferId::VertexConstant);

		auto buffer = map->Get<CBuffer_Vertex_Main>();
		buffer->transform = m_transform;
		buffer->translate = translation;
	}

	if (shader_id != PixelShaderId::None)
		m_pipeline->PushPixelShader(texture ? PixelShaderId::Texture : PixelShaderId::Color);

	// RenderGeometry(compiled_geometry, translation);
	{
		unsigned int stride = sizeof(Rml::Vertex);
		unsigned int offset = 0;
		m_device_context->IASetVertexBuffers(0, 1, compiled_geometry->vertex_buffer.GetAddressOf(), &stride, &offset);
		m_device_context->IASetIndexBuffer(compiled_geometry->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

		m_device_context->DrawIndexed(compiled_geometry->index_count, 0, 0);
	}

	if (shader_id != PixelShaderId::None)
		m_pipeline->PopPixelShader();
}

// void RenderInterface_D3D11::RenderGeometry(CompiledGeometry* geometry, Rml::Vector2f translation)
// {
// 	{
// 		auto map = m_pipeline->Map(BufferId::VertexConstant);

// 		auto buffer = map->Get<CBuffer_Vertex_Main>();
// 		buffer->transform = m_transform;
// 		buffer->translate = translation;
// 	}

// 	unsigned int stride = sizeof(Rml::Vertex);
// 	unsigned int offset = 0;
// 	m_device_context->IASetVertexBuffers(0, 1, geometry->vertex_buffer.GetAddressOf(), &stride, &offset);
// 	m_device_context->IASetIndexBuffer(geometry->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

// 	m_device_context->DrawIndexed(geometry->index_count, 0, 0);
// }

void RenderInterface_D3D11::DrawFullscreenQuad()
{
	RenderGeometry(m_fullscreen_quad_geometry, {}, RenderInterface_D3D11::TexturePostprocess);
}

void RenderInterface_D3D11::DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling)
{
	Rml::Mesh mesh;
	Rml::MeshUtilities::GenerateQuad(mesh, Rml::Vector2f(-1, 1), Rml::Vector2f(2, -2), {});
	if (uv_offset != Rml::Vector2f() || uv_scaling != Rml::Vector2f(1.f))
	{
		for (Rml::Vertex& vertex : mesh.vertices)
			vertex.tex_coord = (vertex.tex_coord * uv_scaling) + uv_offset;
	}
	const Rml::CompiledGeometryHandle geometry = CompileGeometry(mesh.vertices, mesh.indices);
	RenderGeometry(geometry, {}, RenderInterface_D3D11::TexturePostprocess);
	ReleaseGeometry(geometry);
}

void RenderInterface_D3D11::ReleaseGeometry(Rml::CompiledGeometryHandle geometry)
{
	CompiledGeometry* compiled_geometry = reinterpret_cast<CompiledGeometry*>(geometry);
	delete compiled_geometry;
}

// Set to byte packing, or the compiler will expand our struct, which means it won't read correctly from file
#pragma pack(1)
struct TGAHeader {
	char idLength;
	char colourMapType;
	char dataType;
	short int colourMapOrigin;
	short int colourMapLength;
	char colourMapDepth;
	short int xOrigin;
	short int yOrigin;
	short int width;
	short int height;
	char bitsPerPixel;
	char imageDescriptor;
};
// Restore packing
#pragma pack()

Rml::TextureHandle RenderInterface_D3D11::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	Rml::FileInterface* file_interface = Rml::GetFileInterface();
	Rml::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
	{
		return false;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	if (buffer_size <= sizeof(TGAHeader))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Texture file size is smaller than TGAHeader, file is not a valid TGA image.");
		file_interface->Close(file_handle);
		return false;
	}

	using Rml::byte;
	Rml::UniquePtr<byte[]> buffer(new byte[buffer_size]);
	file_interface->Read(buffer.get(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	TGAHeader header;
	memcpy(&header, buffer.get(), sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	const size_t image_size = header.width * header.height * 4; // We always make 32bit textures

	if (header.dataType != 2)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported.");
		return false;
	}

	// Ensure we have at least 3 colors
	if (color_mode < 3)
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "Only 24 and 32bit textures are supported.");
		return false;
	}

	const byte* image_src = buffer.get() + sizeof(TGAHeader);
	Rml::UniquePtr<byte[]> image_dest_buffer(new byte[image_size]);
	byte* image_dest = image_dest_buffer.get();

	// Targa is BGR, swap to RGB, flip Y axis, and convert to premultiplied alpha.
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : (header.height - y - 1) * header.width * 4;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
			{
				const byte alpha = image_src[read_index + 3];
				for (size_t j = 0; j < 3; j++)
					image_dest[write_index + j] = byte((image_dest[write_index + j] * alpha) / 255);
				image_dest[write_index + 3] = alpha;
			}
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	texture_dimensions.x = header.width;
	texture_dimensions.y = header.height;

	return GenerateTexture({image_dest, image_size}, texture_dimensions);
}

Rml::TextureHandle RenderInterface_D3D11::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions)
{
	D3D11_SUBRESOURCE_DATA* initial_data = nullptr;

	if (!source.empty())
	{
		D3D11_SUBRESOURCE_DATA data{
			source.data(),
			source_dimensions.x * 4U,
			0,
		};
		initial_data = &data;
	}

	auto texture = new Texture(m_device.Get(), source_dimensions, DXGI_FORMAT_R8G8B8A8_UNORM, 1, initial_data);

	return reinterpret_cast<Rml::TextureHandle>(texture);
}

void RenderInterface_D3D11::ReleaseTexture(Rml::TextureHandle texture)
{
	delete reinterpret_cast<Texture*>(texture);
}

void RenderInterface_D3D11::EnableScissorRegion(bool enable)
{
	if (!enable)
	{
		SetScissorRegion(Rml::Rectanglei::MakeInvalid());
	}
}

void RenderInterface_D3D11::SetScissorRegion(Rml::Rectanglei region)
{
	D3D11_RECT scissor;
	if (region.Valid())
	{
		scissor.left = region.Left();
		scissor.top = region.Top();
		scissor.right = region.Right();
		scissor.bottom = region.Bottom();

		m_scissor = region;
	}
	else
	{
		scissor.left = scissor.top = 0;
		scissor.right = m_viewport.x;
		scissor.bottom = m_viewport.y;

		m_scissor = Rml::Rectanglei::FromSize(m_viewport);
	}

	m_device_context->RSSetScissorRects(1, &scissor);
}

void RenderInterface_D3D11::EnableClipMask(bool enable)
{
	m_pipeline->UseDepthStencilState(                                      //
		enable ? DepthStencilStateId::Test : DepthStencilStateId::Disable, //
		enable ? m_stencil_ref : 0                                         //
	);
}

void RenderInterface_D3D11::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation)
{
	// TODO move to pipeline
	ComPtr<ID3D11RenderTargetView> prev_rtv;
	ComPtr<ID3D11DepthStencilView> prev_dsv;
	m_device_context->OMGetRenderTargets(1, prev_rtv.GetAddressOf(), prev_dsv.GetAddressOf());

	bool clear_stencil = false;
	DepthStencilStateId depth_stencil_state_id;

	switch (operation)
	{
	case Rml::ClipMaskOperation::Set:
	{
		m_stencil_ref = 1;
		clear_stencil = true;
		depth_stencil_state_id = DepthStencilStateId::Set;
		break;
	}
	case Rml::ClipMaskOperation::SetInverse:
	{
		m_stencil_ref = 0;
		clear_stencil = true;
		depth_stencil_state_id = DepthStencilStateId::Set;
		break;
	}
	case Rml::ClipMaskOperation::Intersect:
	{
		m_stencil_ref += 1;
		clear_stencil = false;
		depth_stencil_state_id = DepthStencilStateId::Intersect;
		break;
	}
	}

	if (clear_stencil)
	{
		m_device_context->ClearDepthStencilView(m_render_layers->m_layers_dsv.Get(), D3D11_CLEAR_STENCIL, 1.0, 0);
	}

	m_device_context->OMSetRenderTargets(0, nullptr, m_render_layers->m_layers_dsv.Get());

	m_pipeline->UseDepthStencilState(depth_stencil_state_id, 1);

	RenderGeometry(geometry, translation, {});

	m_pipeline->UseDepthStencilState(DepthStencilStateId::Test, m_stencil_ref);
	m_device_context->OMSetRenderTargets(1, prev_rtv.GetAddressOf(), prev_dsv.Get()); // TODO move to pipeline
}

void RenderInterface_D3D11::SetTransform(const Rml::Matrix4f* transform)
{
	m_transform = (transform ? (m_projection * (*transform)) : m_projection);
}

Rml::LayerHandle RenderInterface_D3D11::PushLayer()
{
	const Rml::LayerHandle layer_handle = m_render_layers->PushLayer();
	BEGIN_EVENT("Layer #%d", m_render_layers->layers_size - 1);

	const auto& render_target = m_render_layers->GetLayer(layer_handle);

	m_device_context->OMSetRenderTargets(1, render_target.rtv.GetAddressOf(), m_render_layers->m_layers_dsv.Get());
	m_device_context->ClearRenderTargetView(render_target.rtv.Get(), clear_color);

	return layer_handle;
}

// TODO: move to some helpers idk
static Rml::Colourf ConvertToColorf(Rml::ColourbPremultiplied c0)
{
	Rml::Colourf result;
	for (int i = 0; i < 4; i++)
		result[i] = (1.f / 255.f) * float(c0[i]);
	return result;
}

static Rml::Pair<Rml::Vector2f, Rml::Vector2f> CalcTexCoordLimits(Rml::Rectanglei rectangle, Rml::Vector2i framebuffer_size)
{
	// Offset by half-texel values so that texture lookups are clamped to fragment centers, thereby avoiding color
	// bleeding from neighboring texels due to bilinear interpolation.
	const Rml::Vector2f min = (Rml::Vector2f(rectangle.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);
	const Rml::Vector2f max = (Rml::Vector2f(rectangle.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);

	return {min, max};
}

void RenderInterface_D3D11::CompositeLayers(Rml::LayerHandle source_handle, Rml::LayerHandle destination_handle, Rml::BlendMode blend_mode,
	Rml::Span<const Rml::CompiledFilterHandle> filters)
{
	// Blit source layer to postprocessing buffer. Do this regardless of whether we actually have any filters to be
	// applied, because we need to resolve the multi-sampled framebuffer in any case.
	// @performance If we have BlendMode::Replace and no filters or mask then we can just blit directly to the destination.

	{
		auto& layer_pp = m_render_layers->GetPostprocessPrimary();
		auto& layer_top = m_render_layers->GetLayer(source_handle);

		// TODO restrict to scissor rect
		// see RenderInterface_GL3::BlitLayerToPostprocessPrimary
		m_device_context->ResolveSubresource(layer_pp.texture.Get(), 0, layer_top.texture.Get(), 0, back_buffer_format);
	}

	// Render the filters, the PostprocessPrimary framebuffer is used for both input and output.
	for (const Rml::CompiledFilterHandle filter : filters)
	{
		CompiledFilter* compiled_filter_ptr = reinterpret_cast<CompiledFilter*>(filter);

		if (auto compiled_filter = rmlui_dynamic_cast<CompiledFilter_MaskImage*>(compiled_filter_ptr); compiled_filter)
		{
			m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
			m_pipeline->PushPixelShader(PixelShaderId::BlendMask);
			m_pipeline->PushBlendState(BlendStateId::None, {});

			auto& source = m_render_layers->GetPostprocessPrimary();
			auto& destination = m_render_layers->GetPostprocessSecondary();
			auto& blend_mask = m_render_layers->GetBlendMask();

			ID3D11ShaderResourceView* textures[] = {source.srv.Get(), blend_mask.srv.Get()};
			m_device_context->PSSetShaderResources(0, 2, textures);
			m_device_context->OMSetRenderTargets(1, destination.rtv.GetAddressOf(), destination.dsv.Get());

			DrawFullscreenQuad();
			m_render_layers->SwapPostprocessPrimarySecondary();
			m_device_context->OMSetRenderTargets(0, nullptr, nullptr); // TODO ????

			m_pipeline->PopVertexShader();
			m_pipeline->PopPixelShader();
			m_pipeline->PopBlendState();
		}
		else if (auto compiled_filter = rmlui_dynamic_cast<CompiledFilter_Passthrough*>(compiled_filter_ptr); compiled_filter)
		{
			m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
			m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
			m_pipeline->PushBlendState(BlendStateId::Passthrough,
				{
					compiled_filter->blend_factor,
					compiled_filter->blend_factor,
					compiled_filter->blend_factor,
					compiled_filter->blend_factor,
				});

			auto& source = m_render_layers->GetPostprocessPrimary();
			auto& destination = m_render_layers->GetPostprocessSecondary();
			m_device_context->PSSetShaderResources(0, 1, source.srv.GetAddressOf());
			m_device_context->OMSetRenderTargets(1, destination.rtv.GetAddressOf(), destination.dsv.Get());

			DrawFullscreenQuad();
			m_render_layers->SwapPostprocessPrimarySecondary();
			m_device_context->OMSetRenderTargets(0, nullptr, nullptr); // TODO ???

			m_pipeline->PopVertexShader();
			m_pipeline->PopPixelShader();
			m_pipeline->PopBlendState();
		}
		// must go above Blur becuase of inheritance
		else if (auto compiled_filter = rmlui_dynamic_cast<CompiledFilter_DropShadow*>(compiled_filter_ptr); compiled_filter)
		{
			m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
			m_pipeline->PushPixelShader(PixelShaderId::DropShadow);
			m_pipeline->PushBlendState(BlendStateId::None, {});

			auto& primary = m_render_layers->GetPostprocessPrimary();
			auto& secondary = m_render_layers->GetPostprocessSecondary();
			m_device_context->PSSetShaderResources(0, 1, primary.srv.GetAddressOf());
			m_device_context->OMSetRenderTargets(1, secondary.rtv.GetAddressOf(), secondary.dsv.Get());

			const auto tex_coord_limits = CalcTexCoordLimits(m_scissor, m_viewport);

			{
				auto map = m_pipeline->Map(BufferId::PixelConstant);
				auto buffer = map->Get<CBuffer_Pixel_DropShadow>();

				buffer->color = ConvertToColorf(compiled_filter->color);
				buffer->tex_coord_min = tex_coord_limits.first;
				buffer->tex_coord_max = tex_coord_limits.second;
			}

			const Rml::Vector2f uv_offset = compiled_filter->offset / -Rml::Vector2f(m_viewport);
			DrawFullscreenQuad(uv_offset);

			if (compiled_filter->sigma >= 0.5f)
			{
				auto& tertiary = m_render_layers->GetPostprocessTertiary();
				RenderBlur(compiled_filter->sigma, secondary, tertiary);
			}

			m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
			m_device_context->PSSetShaderResources(0, 1, primary.srv.GetAddressOf());
			m_pipeline->PushBlendState(BlendStateId::Main, {});
			DrawFullscreenQuad();

			m_render_layers->SwapPostprocessPrimarySecondary();

			m_pipeline->PopVertexShader();
			m_pipeline->PopPixelShader(2);
			m_pipeline->PopBlendState(2);
		}
		// must go below DropShadow becuase of inheritance
		else if (auto compiled_filter = rmlui_dynamic_cast<CompiledFilter_Blur*>(compiled_filter_ptr); compiled_filter)
		{
			m_pipeline->PushBlendState(BlendStateId::None, {});

			auto& primary = m_render_layers->GetPostprocessPrimary();
			auto& secondary = m_render_layers->GetPostprocessSecondary();
			RenderBlur(compiled_filter->sigma, primary, secondary);

			m_pipeline->PopBlendState();
		}
		else if (auto compiled_filter = rmlui_dynamic_cast<CompiledFilter_ColorMatrix*>(compiled_filter_ptr); compiled_filter)
		{
			m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
			m_pipeline->PushPixelShader(PixelShaderId::ColorMatrix);
			m_pipeline->PushBlendState(BlendStateId::None, {});

			{
				auto map = m_pipeline->Map(BufferId::PixelConstant);
				auto buffer = map->Get<CBuffer_Pixel_ColorMatrix>();

				// buffer->matrix = Rml::RowMajorMatrix4f::FromColumnMajor(compiled_filter->color_matrix.data());
				buffer->matrix = compiled_filter->color_matrix;
			}

			auto& source = m_render_layers->GetPostprocessPrimary();
			auto& destination = m_render_layers->GetPostprocessSecondary();

			m_device_context->PSSetShaderResources(0, 1, source.srv.GetAddressOf());
			m_device_context->OMSetRenderTargets(1, destination.rtv.GetAddressOf(), destination.dsv.Get());

			DrawFullscreenQuad();
			m_render_layers->SwapPostprocessPrimarySecondary();
			m_device_context->OMSetRenderTargets(0, nullptr, nullptr); // TODO ???

			m_pipeline->PopVertexShader();
			m_pipeline->PopPixelShader();
			m_pipeline->PopBlendState();
		}
		else
		{
			Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render filter.");
		}
	}

	auto& layer_destination = m_render_layers->GetLayer(destination_handle);

	// Render to the destination layer.
	m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
	m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
	if (blend_mode == Rml::BlendMode::Replace)
		m_pipeline->PushBlendState(BlendStateId::None, {});

	m_device_context->OMSetRenderTargets(1, layer_destination.rtv.GetAddressOf(), m_render_layers->m_layers_dsv.Get());
	m_device_context->PSSetShaderResources(0, 1, m_render_layers->GetPostprocessPrimary().srv.GetAddressOf());

	DrawFullscreenQuad();

	if (destination_handle != m_render_layers->GetTopLayerHandle())
		m_device_context->OMSetRenderTargets(1, m_render_layers->GetTopLayer().rtv.GetAddressOf(), m_render_layers->m_layers_dsv.Get());

	m_pipeline->PopPixelShader();
	m_pipeline->PopVertexShader();
	if (blend_mode == Rml::BlendMode::Replace)
		m_pipeline->PopBlendState();
}

void RenderInterface_D3D11::PopLayer()
{
	END_EVENT;
	m_render_layers->PopLayer();

	const auto& layer = m_render_layers->GetTopLayer();
	m_device_context->OMSetRenderTargets(1, layer.rtv.GetAddressOf(), m_render_layers->m_layers_dsv.Get());
}

static void SigmaToParameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
	constexpr int max_num_passes = 10;
	static_assert(max_num_passes < 31, "");
	constexpr float max_single_pass_sigma = 3.0f;
	out_pass_level = Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
	out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

void RenderInterface_D3D11::BlitRenderTarget(const Texture& source, const Texture& dest, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0,
	int dstY0, int dstX1, int dstY1)
{
	int src_width = srcX1 - srcX0;
	int src_height = srcY1 - srcY0;
	int dest_width = dstX1 - dstX0;
	int dest_height = dstY1 - dstY0;

	bool is_flipped = src_width < 0 || src_height < 0 || dest_width < 0 || dest_height < 0;
	bool is_stretched = src_width != dest_width || src_height != dest_height;
	bool is_full_copy = src_width == dest_width && src_height == dest_height && srcX0 == 0 && srcY0 == 0 && dstX0 == 0 && dstY0 == 0;

	if (is_flipped || is_stretched || !is_full_copy)
	{
		// Unbind existing textures to prevent warning spam
		ID3D11ShaderResourceView* null_shader_resource_views[2] = {nullptr, nullptr};
		m_device_context->PSSetShaderResources(0, 2, null_shader_resource_views);

		// Disable blending
		m_pipeline->PushBlendState(BlendStateId::None, {});

		// Resolve from source to the temporary first
		m_device_context->ResolveSubresource(dest.texture.Get(), 0, source.texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);

		// Bind destination as our final render texture
		m_device_context->OMSetRenderTargets(1, dest.rtv.GetAddressOf(), nullptr);

		// Bind texture
		m_device_context->PSSetShaderResources(0, 1, source.srv.GetAddressOf());
		m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
		m_pipeline->PushVertexShader(VertexShaderId::Passthrough);

		// Draw a quad into temporary with source bound as the texture, and the UVs lerping to match the new dimensions
		// We want to map UV   0 -  1 to src min-max
		// We want to map pos -1 - +1 to dst min max

		float uv_x_min = float(srcX0) / float(m_viewport.x); // Map to 0
		float uv_y_max = float(srcY0) / float(m_viewport.y); // Map to 0
		float uv_x_max = float(srcX1) / float(m_viewport.x); // Map to +1
		float uv_y_min = float(srcY1) / float(m_viewport.y); // Map to +1

		float pos_x_min = (dstX0 / float(m_viewport.x)) * 2.0f - 1.0f;
		float pos_y_min = ((m_viewport.y - dstY0 - dest_height) / float(m_viewport.y)) * 2.0f - 1.0f;
		float pos_x_max = ((dstX0 + dest_width) / float(m_viewport.x)) * 2.0f - 1.0f;
		float pos_y_max = ((m_viewport.y - dstY0) / float(m_viewport.y)) * 2.0f - 1.0f;

		Rml::Mesh mesh;
		Rml::MeshUtilities::GenerateQuad(mesh,              //
			{pos_x_min, pos_y_min},                         //
			{pos_x_max - pos_x_min, pos_y_max - pos_y_min}, //
			{},                                             //
			{uv_x_min, uv_y_min},                           //
			{uv_x_max, uv_y_max}                            //
		);

		const Rml::CompiledGeometryHandle geometry = CompileGeometry(mesh.vertices, mesh.indices);
		RenderGeometry(geometry, {}, TexturePostprocess);
		ReleaseGeometry(geometry);

		m_pipeline->PopBlendState();
		m_pipeline->PopPixelShader();
		m_pipeline->PopVertexShader();
	}
	else
	{
		// Resolve and move on
		m_device_context->ResolveSubresource(dest.texture.Get(), 0, source.texture.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
	}
}

void RenderInterface_D3D11::RenderBlur(float sigma, const Texture& source_destination, const Texture& temp)
{
	RMLUI_ASSERT(&source_destination != &temp);

	int pass_level = 0;
	SigmaToParameters(sigma, pass_level, sigma);

	const Rml::Rectanglei original_scissor = m_scissor;
	Rml::Rectanglei scissor = m_scissor;

	// Begin by downscaling so that the blur pass can be done at a reduced resolution for large sigma.
	m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
	m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
	SetScissorRegion(scissor);

	// Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
	m_pipeline->PushViewport(Rml::Rectanglei::FromSize(m_viewport / 2));

	// Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing a 50% blend of
	// neighboring texels.
	const Rml::Vector2f uv_scaling = {(m_viewport.x % 2 == 1) ? (1.f - 1.f / float(m_viewport.x)) : 1.f,
		(m_viewport.y % 2 == 1) ? (1.f - 1.f / float(m_viewport.y)) : 1.f};

	// TODO m_pipeline->Unbind
	void* null_view[1] = {nullptr};

	// TODO god we are unbinding way too much i think, optimize this??? (or measure perf impact of those unbinds)
	m_device_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(null_view));
	m_device_context->OMSetRenderTargets(0, reinterpret_cast<ID3D11RenderTargetView**>(null_view), nullptr);

	for (int i = 0; i < pass_level; i++)
	{
		scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
		scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
		const bool from_source = (i % 2 == 0);

		m_device_context->PSSetShaderResources(0, 1, (from_source ? source_destination : temp).srv.GetAddressOf());
		m_device_context->OMSetRenderTargets(1, (from_source ? temp : source_destination).rtv.GetAddressOf(), nullptr);

		SetScissorRegion(scissor);
		DrawFullscreenQuad({}, uv_scaling);

		m_device_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(null_view));
		m_device_context->OMSetRenderTargets(0, reinterpret_cast<ID3D11RenderTargetView**>(null_view), nullptr);
	}

	m_device_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(null_view));
	m_device_context->OMSetRenderTargets(0, reinterpret_cast<ID3D11RenderTargetView**>(null_view), nullptr);

	m_pipeline->PopViewport();

	// Ensure texture data end up in the temp buffer. Depending on the last downscaling, we might need to move it from the source_destination buffer.
	const bool transfer_to_temp_buffer = (pass_level % 2 == 0);
	if (transfer_to_temp_buffer)
	{
		m_device_context->PSSetShaderResources(0, 1, source_destination.srv.GetAddressOf());
		m_device_context->OMSetRenderTargets(1, temp.rtv.GetAddressOf(), nullptr);
		DrawFullscreenQuad();

		m_device_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(null_view));
		m_device_context->OMSetRenderTargets(0, reinterpret_cast<ID3D11RenderTargetView**>(null_view), nullptr);
	}

	// Set up uniforms.
	m_pipeline->PushVertexShader(VertexShaderId::Blur);
	m_pipeline->PushPixelShader(PixelShaderId::Blur);

	{
		auto map = m_pipeline->Map(BufferId::PixelConstant);
		auto buffer = map->Get<CBuffer_Pixel_Blur>();

		// SetBlurWeights
		{
			float normalization = 0.0f;
			for (int i = 0; i < BLUR_NUM_WEIGHTS; i++)
			{
				if (Rml::Math::Absolute(sigma) < 0.1f)
					buffer->weights[i].data = float(i == 0);
				else
					buffer->weights[i].data =
						Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) / (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);

				normalization += (i == 0 ? 1.f : 2.0f) * buffer->weights[i].data;
			}
			for (int i = 0; i < BLUR_NUM_WEIGHTS; i++)
				buffer->weights[i].data /= normalization;
		}

		const auto tex_coord_limits = CalcTexCoordLimits(scissor, m_viewport);
		buffer->tex_coord_min = tex_coord_limits.first;
		buffer->tex_coord_max = tex_coord_limits.second;
	}

	auto SetTexelOffset = [this](Rml::Vector2f blur_direction, float texture_dimension) {
		auto map = m_pipeline->Map(BufferId::VertexConstant);
		auto buffer = map->Get<CBuffer_Vertex_Blur>();

		buffer->texel_offset = blur_direction * (1.0f / texture_dimension);
	};

	// Blur render pass - vertical.
	m_device_context->PSSetShaderResources(0, 1, temp.srv.GetAddressOf());
	m_device_context->OMSetRenderTargets(1, source_destination.rtv.GetAddressOf(), nullptr);

	SetTexelOffset({0.f, 1.f}, m_viewport.y);
	DrawFullscreenQuad();

	m_device_context->PSSetShaderResources(0, 1, reinterpret_cast<ID3D11ShaderResourceView**>(null_view));
	m_device_context->OMSetRenderTargets(0, reinterpret_cast<ID3D11RenderTargetView**>(null_view), nullptr);

	// Blur render pass - horizontal.
	m_device_context->PSSetShaderResources(0, 1, source_destination.srv.GetAddressOf());
	m_device_context->OMSetRenderTargets(1, temp.rtv.GetAddressOf(), nullptr);

	// Add a 1px transparent border around the blur region by first clearing with a padded scissor. This helps prevent
	// artifacts when upscaling the blur result in the later step. On Intel and AMD, we have observed that during
	// blitting with linear filtering, pixels outside the 'src' region can be blended into the output. On the other
	// hand, it looks like Nvidia clamps the pixels to the source edge, which is what we really want. Regardless, we
	// work around the issue with this extra step.
	SetScissorRegion(scissor.Extend(1));
	m_device_context->ClearRenderTargetView(temp.rtv.Get(), clear_color);
	SetScissorRegion(scissor);

	SetTexelOffset({1.f, 0.f}, m_viewport.x);
	DrawFullscreenQuad();

	// Blit the blurred image to the scissor region with upscaling.
	SetScissorRegion(original_scissor);

	const Rml::Vector2i src_min = scissor.p0;
	const Rml::Vector2i src_max = scissor.p1;
	const Rml::Vector2i dst_min = original_scissor.p0;
	const Rml::Vector2i dst_max = original_scissor.p1;
	BlitRenderTarget(temp, source_destination, src_min.x, src_min.y, src_max.x, src_max.y, dst_min.x, dst_min.y, dst_max.x, dst_max.y);

	// The above upscale blit might be jittery at low resolutions (large pass levels). This is especially noticeable when moving an element with
	// backdrop blur around or when trying to click/hover an element within a blurred region since it may be rendered at an offset. For more stable
	// and accurate rendering we next upscale the blur image by an exact power-of-two. However, this may not fill the edges completely so we need to
	// do the above first. Note that this strategy may sometimes result in visible seams. Alternatively, we could try to enlarge the window to the
	// next power-of-two size and then downsample and blur that.
	const Rml::Vector2i target_min = src_min * (1 << pass_level);
	const Rml::Vector2i target_max = src_max * (1 << pass_level);
	if (target_min != dst_min || target_max != dst_max)
	{
		BlitRenderTarget(temp, source_destination, src_min.x, src_min.y, src_max.x, src_max.y, target_min.x, target_min.y, target_max.x,
			target_max.y);
	}

	// Restore render state.
	m_pipeline->PopVertexShader(2);
	m_pipeline->PopPixelShader(2);
}

Rml::TextureHandle RenderInterface_D3D11::SaveLayerAsTexture()
{
	MARK_EVENT;

	RMLUI_ASSERT(m_scissor.Valid());

	auto& layer_pp = m_render_layers->GetPostprocessPrimary();
	auto& layer_top = m_render_layers->GetTopLayer();
	m_device_context->ResolveSubresource(layer_pp.texture.Get(), 0, layer_top.texture.Get(), 0, back_buffer_format);

	CD3D11_BOX box(m_scissor.Left(), m_scissor.Top(), 0, m_scissor.Right(), m_scissor.Bottom(), 1);
	auto texture = new Texture(m_device.Get(), m_scissor.Size(), DXGI_FORMAT_R8G8B8A8_UNORM, 1);
	m_device_context->CopySubresourceRegion(texture->texture.Get(), 0, 0, 0, 0, layer_pp.texture.Get(), 0, &box);

	return reinterpret_cast<Rml::TextureHandle>(texture);
}

Rml::CompiledFilterHandle RenderInterface_D3D11::SaveLayerAsMaskImage()
{
	MARK_EVENT;

	auto& layer_pp = m_render_layers->GetPostprocessPrimary();
	auto& layer_top = m_render_layers->GetTopLayer();
	auto& layer_blend_mask = m_render_layers->GetBlendMask();

	m_device_context->ResolveSubresource(layer_blend_mask.texture.Get(), 0, layer_top.texture.Get(), 0, back_buffer_format);
	// m_device_context->ResolveSubresource(layer_pp.texture.Get(), 0, layer_top.texture.Get(), 0, back_buffer_format);

	// m_pipeline->PushVertexShader(VertexShaderId::Passthrough);
	// m_pipeline->PushPixelShader(PixelShaderId::Passthrough);
	// m_pipeline->PushBlendState(BlendStateId::None, {});

	// m_device_context->PSSetShaderResources(0, 1, layer_pp.srv.GetAddressOf()); // TODO: texture->bind()
	// m_device_context->OMSetRenderTargets(1, layer_blend_mask.rtv.GetAddressOf(), nullptr);

	// DrawFullscreenQuad();

	// m_pipeline->PopVertexShader();
	// m_pipeline->PopPixelShader();
	// m_pipeline->PopBlendState();

	// m_device_context->OMSetRenderTargets(1, layer_top.rtv.GetAddressOf(), nullptr);

	auto filter = new CompiledFilter_MaskImage();
	return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
}

Rml::CompiledFilterHandle RenderInterface_D3D11::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters)
{
	if (name == "opacity")
	{
		auto filter = new CompiledFilter_Passthrough();
		filter->blend_factor = Rml::Get(parameters, "value", 1.0f);

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "blur")
	{
		auto filter = new CompiledFilter_Blur();
		filter->sigma = Rml::Get(parameters, "sigma", 1.0f);

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "drop-shadow")
	{
		auto filter = new CompiledFilter_DropShadow();
		filter->sigma = Rml::Get(parameters, "sigma", 0.f);
		filter->color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
		filter->offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "brightness")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		filter->color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "contrast")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float grayness = 0.5f - 0.5f * value;
		filter->color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
		filter->color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "invert")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
		const float inverted = 1.f - 2.f * value;
		filter->color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
		filter->color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "grayscale")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
		// clang-format off
		filter->color_matrix = Rml::Matrix4f::FromRows(
			{gray.x + rev_value, gray.y,             gray.z,             0.f},
			{gray.x,             gray.y + rev_value, gray.z,             0.f},
			{gray.x,             gray.y,             gray.z + rev_value, 0.f},
			{0.f,                0.f,                0.f,                1.f}
		);
		// clang-format on

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "sepia")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float rev_value = 1.f - value;
		const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
		const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
		const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
		// clang-format off
		filter->color_matrix = Rml::Matrix4f::FromRows(
			{r_mix.x + rev_value, r_mix.y,             r_mix.z,             0.f},
			{g_mix.x,             g_mix.y + rev_value, g_mix.z,             0.f},
			{b_mix.x,             b_mix.y,             b_mix.z + rev_value, 0.f},
			{0.f,                 0.f,                 0.f,                 1.f}
		);
		// clang-format on

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "hue-rotate")
	{
		// Hue-rotation and saturation values based on: https://www.w3.org/TR/filter-effects-1/#attr-valuedef-type-huerotate
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		const float s = Rml::Math::Sin(value);
		const float c = Rml::Math::Cos(value);
		// clang-format off
		filter->color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * c - 0.213f * s,  0.715f - 0.715f * c - 0.715f * s,  0.072f - 0.072f * c + 0.928f * s,  0.f},
			{0.213f - 0.213f * c + 0.143f * s,  0.715f + 0.285f * c + 0.140f * s,  0.072f - 0.072f * c - 0.283f * s,  0.f},
			{0.213f - 0.213f * c - 0.787f * s,  0.715f - 0.715f * c + 0.715f * s,  0.072f + 0.928f * c + 0.072f * s,  0.f},
			{0.f,                               0.f,                               0.f,                               1.f}
		);
		// clang-format on

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}
	else if (name == "saturate")
	{
		auto filter = new CompiledFilter_ColorMatrix();
		const float value = Rml::Get(parameters, "value", 1.0f);
		// clang-format off
		filter->color_matrix = Rml::Matrix4f::FromRows(
			{0.213f + 0.787f * value,  0.715f - 0.715f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f + 0.285f * value,  0.072f - 0.072f * value,  0.f},
			{0.213f - 0.213f * value,  0.715f - 0.715f * value,  0.072f + 0.928f * value,  0.f},
			{0.f,                      0.f,                      0.f,                      1.f}
		);
		// clang-format on

		return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
	}

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported filter type '%s'.", name.c_str());
	return {};
}

void RenderInterface_D3D11::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
	delete reinterpret_cast<CompiledFilter*>(filter);
}

Rml::CompiledShaderHandle RenderInterface_D3D11::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters)
{
	bool is_linear_gradient = name == "linear-gradient";
	bool is_radial_gradient = name == "radial-gradient";
	bool is_conic_gradient = name == "conic-gradient";

	if (is_linear_gradient || is_radial_gradient || is_conic_gradient)
	{
		auto shader = new CompiledShader_Gradient();

		const bool repeating = Rml::Get(parameters, "repeating", false);
		if (is_linear_gradient)
		{
			shader->gradient_function = (repeating ? GRADIENT_REPEATING_LINEAR : GRADIENT_LINEAR);
			shader->p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
			shader->v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader->p;
		}
		else if (is_radial_gradient)
		{
			shader->gradient_function = (repeating ? GRADIENT_REPEATING_RADIAL : GRADIENT_RADIAL);
			shader->p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
			shader->v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
		}
		else if (is_conic_gradient)
		{
			shader->gradient_function = (repeating ? GRADIENT_REPEATING_CONIC : GRADIENT_CONIC);
			shader->p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
			const float angle = Rml::Get(parameters, "angle", 0.f);
			shader->v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
		}

		auto it = parameters.find("color_stop_list");
		RMLUI_ASSERT(it != parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
		const Rml::ColorStopList& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
		const int num_stops = Rml::Math::Min((int)color_stop_list.size(), MAX_NUM_STOPS);

		shader->stop_positions.resize(num_stops);
		shader->stop_colors.resize(num_stops);
		for (int i = 0; i < num_stops; i++)
		{
			const Rml::ColorStop& stop = color_stop_list[i];
			RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
			shader->stop_positions[i] = stop.position.number;
			shader->stop_colors[i] = ConvertToColorf(stop.color);
		}

		return reinterpret_cast<Rml::CompiledShaderHandle>(shader);
	}
	else if (name == "shader")
	{
		auto shader = new CompiledShader_Shader();

		const Rml::String value = Rml::Get(parameters, "value", Rml::String());
		shader->dimensions = Rml::Get(parameters, "dimensions", Rml::Vector2f(0.f));

		if (value == "creation")
			shader->shader_id = PixelShaderId::Creation;

		return reinterpret_cast<Rml::CompiledShaderHandle>(shader);
	}

	Rml::Log::Message(Rml::Log::LT_WARNING, "Unsupported shader type '%s'.", name.c_str());
	return {};
}

void RenderInterface_D3D11::RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
	Rml::TextureHandle /* texture */)
{
	RMLUI_ASSERT(shader && geometry);
	CompiledShader* compiled_shader_ptr = reinterpret_cast<CompiledShader*>(shader);
	CompiledGeometry* compiled_geometry = reinterpret_cast<CompiledGeometry*>(geometry);

	auto map = m_pipeline->Map(BufferId::PixelConstant);

	PixelShaderId shader_id = PixelShaderId::None;

	if (auto compiled_shader = rmlui_dynamic_cast<CompiledShader_Gradient*>(compiled_shader_ptr); compiled_shader)
	{
		shader_id = PixelShaderId::Gradient;

		RMLUI_ASSERT(compiled_shader->stop_positions.size() == compiled_shader->stop_colors.size());
		const int num_stops = compiled_shader->stop_positions.size();

		auto buffer = map->Get<CBuffer_Pixel_Gradient>();
		buffer->func = compiled_shader->gradient_function;
		buffer->p = compiled_shader->p;
		buffer->v = compiled_shader->v;
		buffer->num_stops = num_stops;

		memcpy(buffer->stop_colors, compiled_shader->stop_colors.data(), compiled_shader->stop_colors.size() * sizeof(Rml::Colourf));

		for (size_t i = 0; i < compiled_shader->stop_positions.size(); i++)
		{
			auto& a = compiled_shader->stop_positions[i];
			buffer->stop_positions[i].data = a;
		}
	}
	else if (auto compiled_shader = rmlui_dynamic_cast<CompiledShader_Shader*>(compiled_shader_ptr); compiled_shader)
	{
		shader_id = compiled_shader->shader_id;

		auto buffer = map->Get<CBuffer_Pixel_Creation>();
		buffer->value = Rml::GetSystemInterface()->GetElapsedTime();
		buffer->dimensions = compiled_shader->dimensions;
	}
	else
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Unhandled render shader.");
	}

	map.reset();

	if (shader_id != PixelShaderId::None)
	{
		m_pipeline->PushPixelShader(shader_id);
		// RenderGeometry(compiled_geometry, translation);
		// TODO ts so ass :sob::pray:
		{
			{
				auto map = m_pipeline->Map(BufferId::VertexConstant);

				auto buffer = map->Get<CBuffer_Vertex_Main>();
				buffer->transform = m_transform;
				buffer->translate = translation;
			}

			unsigned int stride = sizeof(Rml::Vertex);
			unsigned int offset = 0;
			m_device_context->IASetVertexBuffers(0, 1, compiled_geometry->vertex_buffer.GetAddressOf(), &stride, &offset);
			m_device_context->IASetIndexBuffer(compiled_geometry->index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);

			m_device_context->DrawIndexed(compiled_geometry->index_count, 0, 0);
		}
		m_pipeline->PopPixelShader();
	}
}

void RenderInterface_D3D11::ReleaseShader(Rml::CompiledShaderHandle shader)
{
	delete reinterpret_cast<CompiledShader*>(shader);
}