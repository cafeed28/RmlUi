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

#ifndef RMLUI_BACKENDS_RENDERER_D3D11_H
#define RMLUI_BACKENDS_RENDERER_D3D11_H

#include "RmlUi_D3D11/shared.h"
#include <RmlUi/Core/RenderInterface.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class D3D11Mark {
private:
	ID3DUserDefinedAnnotation* m_annotation;

public:
	static void Begin(ID3DUserDefinedAnnotation* annotation, const char* str)
	{
		size_t len = mbstowcs(nullptr, &str[0], 0);
		std::wstring wstr(len, 0);
		mbstowcs(&wstr[0], &str[0], wstr.size());

		annotation->BeginEvent(wstr.c_str());
	}

	static void End(ID3DUserDefinedAnnotation* annotation) { annotation->EndEvent(); }

	D3D11Mark(ID3DUserDefinedAnnotation* annotation, const char* str)
	{
		m_annotation = annotation;
		Begin(m_annotation, str);
	}

	~D3D11Mark() { End(m_annotation); }
};

#define MARK_EVENT_NAME(x) D3D11Mark mark(m_annotation.Get(), x);

#define MARK_EVENT MARK_EVENT_NAME(__FUNCTION__)

#define BEGIN_EVENT(fmt, ...)                         \
	{                                                 \
		char buffer[1024] = {0};                      \
		snprintf(buffer, 1024, "" fmt, __VA_ARGS__);  \
		D3D11Mark::Begin(m_annotation.Get(), buffer); \
	}

#define END_EVENT D3D11Mark::End(m_annotation.Get())

#define D3D_SET_OBJECT_NAME(object, fmt, ...)        \
	{                                                \
		char buffer[1024] = {0};                     \
		snprintf(buffer, 1024, "" fmt, __VA_ARGS__); \
		D3D_SET_OBJECT_NAME_A(object, buffer);       \
	}

#define CHECK_HRESULT(x, msg)                                        \
	if (FAILED(x))                                                   \
	{                                                                \
		Rml::Log::Message(Rml::Log::LT_ERROR, msg " (0x%#08lx)", x); \
		DebugBreak();                                                \
		return false;                                                \
	}

#define CHECK_HRESULT_EMPTY(x, msg)                                  \
	if (FAILED(x))                                                   \
	{                                                                \
		Rml::Log::Message(Rml::Log::LT_ERROR, msg " (0x%#08lx)", x); \
		DebugBreak();                                                \
		return {};                                                   \
	}

#define CHECK_HRESULT_VOID(x, msg)                                   \
	if (FAILED(x))                                                   \
	{                                                                \
		Rml::Log::Message(Rml::Log::LT_ERROR, msg " (0x%#08lx)", x); \
		DebugBreak();                                                \
		return;                                                      \
	}

enum class BufferId {
	None,
	PixelConstant,
	VertexConstant,
};

enum class BlendStateId {
	None,
	Main,
	Passthrough,
};

enum class DepthStencilStateId {
	None,
	Disable,
	Set,
	Intersect,
	Test,
};

enum class PixelShaderId {
	None,
	Color,
	Texture,
	Passthrough,
	Gradient,
	Creation,
	BlendMask,
	ColorMatrix,
	Blur,
	DropShadow,
};

enum class VertexShaderId {
	None,
	Main,
	Passthrough,
	Blur,
};

template <typename T, int ALIGN>
struct AlignAs {
	T data;
	char pad[ALIGN - sizeof(T)];
};

// useful tool: https://maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer

struct CBuffer_Pixel_Gradient {
	Rml::Vector2f p;
	Rml::Vector2f v;

	int func;
	int num_stops;

	int pad[2];

	Rml::Vector4f stop_colors[MAX_NUM_STOPS];
	AlignAs<float, 16> stop_positions[MAX_NUM_STOPS];
};

struct CBuffer_Pixel_Creation {
	float value;
	Rml::Vector2f dimensions;
};

struct CBuffer_Pixel_ColorMatrix {
	Rml::Matrix4f matrix;
};

struct CBuffer_Pixel_Blur {
	Rml::Vector2f tex_coord_min;
	Rml::Vector2f tex_coord_max;
	AlignAs<float, 16> weights[BLUR_NUM_WEIGHTS];
};

struct CBuffer_Pixel_DropShadow {
	Rml::Vector2f tex_coord_min;
	Rml::Vector2f tex_coord_max;
	Rml::Colourf color;
};

struct CBuffer_Pixel {
	union {
		CBuffer_Pixel_Gradient gradient;
		CBuffer_Pixel_Creation creation;
		CBuffer_Pixel_ColorMatrix color_matrix;
		CBuffer_Pixel_Blur blur;
		CBuffer_Pixel_DropShadow drop_shadow;
	};
};
static_assert(sizeof(CBuffer_Pixel) % 16 == 0, "constant buffers must be aligned to 16-byte boundary.");

struct CBuffer_Vertex_Main {
	Rml::Matrix4f transform;
	Rml::Vector2f translate;

	int pad[2];
};

struct CBuffer_Vertex_Blur {
	Rml::Vector2f texel_offset;
};

struct CBuffer_Vertex {
	union {
		CBuffer_Vertex_Main main;
		CBuffer_Vertex_Blur blur;
	};
};
static_assert(sizeof(CBuffer_Vertex) % 16 == 0, "constant buffers must be aligned to 16-byte boundary.");

class D3D11Map;

class D3D11Pipeline {
private:
	struct BlendState {
		BlendStateId id;
		float factor[4];
	};

	struct DepthStencilState {
		DepthStencilStateId id;
		unsigned int stencil_ref;
	};

private:
	void UseBlendState(BlendState& state);
	// void UseDepthStencilState(DepthStencilState& state);
	void UsePixelShader(PixelShaderId id);
	void UseVertexShader(VertexShaderId id);
	void UseViewport(Rml::Rectanglei viewport);

	bool Initialize_Buffers();
	bool Initialize_States();
	bool Initialize_Shaders();

	friend class D3D11Map;
	void Unmap(D3D11Map* map);

public:
	D3D11Pipeline(ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> device_context);
	~D3D11Pipeline();

	bool Initialize();

	void BeginFrame(int width, int height);
	void EndFrame();

	Rml::UniquePtr<D3D11Map> Map(BufferId id);

	void UseDepthStencilState(DepthStencilStateId id, unsigned int stencil_ref);

	void PushBlendState(BlendStateId id, Rml::Colourf factor);
	// void PushDepthStencilState(DepthStencilStateId id, unsigned int stencil_ref);
	void PushPixelShader(PixelShaderId id);
	void PushVertexShader(VertexShaderId id);
	void PushViewport(Rml::Rectanglei viewport);

	void PopBlendState(int count = 1);
	// void PopDepthStencilState(int count = 1);
	void PopPixelShader(int count = 1);
	void PopVertexShader(int count = 1);
	void PopViewport(int count = 1);

private:
	ComPtr<ID3D11Device> m_device = {};
	ComPtr<ID3D11DeviceContext> m_device_context = {};

	ComPtr<ID3D11SamplerState> m_sampler_state = {};
	ComPtr<ID3D11RasterizerState> m_rasterizer_state = {};

	Rml::SmallUnorderedMap<BlendStateId, ComPtr<ID3D11BlendState>> m_blend_states = {};
	Rml::SmallUnorderedMap<DepthStencilStateId, ComPtr<ID3D11DepthStencilState>> m_depth_stencil_states = {};

	Rml::SmallUnorderedMap<BufferId, ComPtr<ID3D11Buffer>> m_buffers = {};
	Rml::SmallUnorderedMap<PixelShaderId, ComPtr<ID3D11PixelShader>> m_pixel_shaders = {};
	Rml::SmallUnorderedMap<VertexShaderId, ComPtr<ID3D11VertexShader>> m_vertex_shaders = {};
	Rml::SmallUnorderedMap<VertexShaderId, ComPtr<ID3D11InputLayout>> m_input_layouts = {};

	Rml::Stack<BlendState> m_blend_state_stack = {};
	// Rml::Stack<DepthStencilState> m_depth_stencil_stack = {};
	Rml::Stack<PixelShaderId> m_pixel_shader_stack = {};
	Rml::Stack<VertexShaderId> m_vertex_shader_stack = {};
	Rml::Stack<Rml::Rectanglei> m_viewport_stack = {};
};

class D3D11Map {
private:
	friend class D3D11Pipeline;

	BufferId m_id;
	D3D11Pipeline* m_pipeline;
	D3D11_MAPPED_SUBRESOURCE m_subresource;

public:
	D3D11Map(BufferId id, D3D11Pipeline* pipeline)
	{
		m_id = id;
		m_pipeline = pipeline;
	}

	~D3D11Map() { m_pipeline->Unmap(this); }

	template <typename T>
	T* Get()
	{
		return reinterpret_cast<T*>(m_subresource.pData);
	}
};

class RenderInterface_D3D11 : public Rml::RenderInterface {
public:
	// Can be passed to RenderGeometry() to enable texture rendering without changing the bound texture.
	static constexpr Rml::TextureHandle TextureEnableWithoutBinding = Rml::TextureHandle(-1);
	// Can be passed to RenderGeometry() to leave the bound texture and used program unchanged.
	static constexpr Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

	RenderInterface_D3D11();
	~RenderInterface_D3D11();

	bool Initialize(ComPtr<IDXGISwapChain1> swap_chain, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> device_context);
	void Shutdown();

	void BeginFrame();
	void EndFrame();

	void SetViewport(int width, int height);
	void GetViewport(int& width, int& height);

	// -- Inherited from Rml::RenderInterface --

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;

	void EnableClipMask(bool enable) override;
	void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

	void SetTransform(const Rml::Matrix4f* transform) override;

	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters) override;
	void PopLayer() override;

	Rml::TextureHandle SaveLayerAsTexture() override;

	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

	Rml::Image CaptureScreen() override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) override;
	void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
		Rml::TextureHandle texture) override;
	void ReleaseShader(Rml::CompiledShaderHandle shader) override;

private: // Geometry
	struct CompiledGeometry {
		ComPtr<ID3D11Buffer> vertex_buffer;
		ComPtr<ID3D11Buffer> index_buffer;
		unsigned int index_count;
	};

	struct Texture {
		ComPtr<ID3D11Texture2D> texture = {};
		ComPtr<ID3D11RenderTargetView> rtv = {};
		ComPtr<ID3D11ShaderResourceView> srv = {};

		Texture() = default;

		// Texture(ID3D11Device* device, ID3D11Texture2D* texture);
		Texture(ID3D11Device* device, Rml::Vector2i dimensions, DXGI_FORMAT format, unsigned int sample_count,
			D3D11_SUBRESOURCE_DATA* initial_data = nullptr);
	};

	struct DepthStencilTexture : public Texture {
		ComPtr<ID3D11Texture2D> texture_depth_stencil = {};
		ComPtr<ID3D11DepthStencilView> dsv = {};

		DepthStencilTexture() = default;

		DepthStencilTexture(ID3D11Device* device, Rml::Vector2i dimensions, DXGI_FORMAT format, unsigned int sample_count);
	};

private: // Filters
	struct CompiledFilter {
		RMLUI_RTTI_Define(CompiledFilter)

		virtual ~CompiledFilter() {}
	};

	struct CompiledFilter_MaskImage : public CompiledFilter {
		RMLUI_RTTI_DefineWithParent(CompiledFilter_MaskImage, CompiledFilter)

		CompiledFilter_MaskImage() {}
	};

	struct CompiledFilter_Passthrough : public CompiledFilter {
		RMLUI_RTTI_DefineWithParent(CompiledFilter_Passthrough, CompiledFilter)

		float blend_factor = 0.f;

		CompiledFilter_Passthrough() {}
	};

	struct CompiledFilter_ColorMatrix : public CompiledFilter {
		RMLUI_RTTI_DefineWithParent(CompiledFilter_ColorMatrix, CompiledFilter)

		Rml::Matrix4f color_matrix = {};

		CompiledFilter_ColorMatrix() {}
	};

	struct CompiledFilter_Blur : public CompiledFilter {
		RMLUI_RTTI_DefineWithParent(CompiledFilter_Blur, CompiledFilter)

		float sigma = 0.f;

		CompiledFilter_Blur() {}
	};

	struct CompiledFilter_DropShadow : public CompiledFilter_Blur {
		RMLUI_RTTI_DefineWithParent(CompiledFilter_DropShadow, CompiledFilter_Blur)

		Rml::Vector2f offset = {};
		Rml::ColourbPremultiplied color = {};

		CompiledFilter_DropShadow() {}
	};

private: // Shaders
	struct CompiledShader {
		RMLUI_RTTI_Define(CompiledShader)

		virtual ~CompiledShader() {}
	};

	struct CompiledShader_Gradient : public CompiledShader {
		RMLUI_RTTI_DefineWithParent(CompiledShader_Gradient, CompiledShader)

		int gradient_function = 0; // GRADIENT_* defines in RmlUi_D3D11/shared.h
		Rml::Vector2f p = {};
		Rml::Vector2f v = {};
		Rml::Vector<float> stop_positions = {};
		Rml::Vector<Rml::Colourf> stop_colors = {};

		CompiledShader_Gradient() {}
	};

	struct CompiledShader_Shader : public CompiledShader {
		RMLUI_RTTI_DefineWithParent(CompiledShader_Shader, CompiledShader)

		PixelShaderId shader_id = PixelShaderId::None;
		Rml::Vector2f dimensions = {};

		CompiledShader_Shader() {}
	};

private: // Layers
	/*
	    Manages render targets, including the layer stack and postprocessing framebuffers.

	    Layers can be pushed and popped, creating new framebuffers as needed. Typically, geometry is rendered to the top
	    layer. The layer framebuffers may have MSAA enabled.

	    Postprocessing framebuffers are separate from the layers, and are commonly used to apply texture-wide effects
	    such as filters. They are used both as input and output during rendering, and do not use MSAA.
	*/
	class RenderLayerStack {
	public:
		RenderLayerStack(int width, int height, ComPtr<ID3D11Device> device);

		// Push a new layer. All references to previously retrieved layers are invalidated.
		Rml::LayerHandle PushLayer();

		// Pop the top layer. All references to previously retrieved layers are invalidated.
		void PopLayer();

		const Texture& GetLayer(Rml::LayerHandle layer) const;
		const Texture& GetTopLayer() const;
		Rml::LayerHandle GetTopLayerHandle() const;

		const DepthStencilTexture& GetPostprocessPrimary() { return fb_postprocess.at(0); }
		const DepthStencilTexture& GetPostprocessSecondary() { return fb_postprocess.at(1); }
		const DepthStencilTexture& GetPostprocessTertiary() { return fb_postprocess.at(2); }
		const DepthStencilTexture& GetBlendMask() { return fb_postprocess.at(3); }
		const DepthStencilTexture& GetTemporary() { return fb_postprocess.at(4); }

		void SwapPostprocessPrimarySecondary();

		// private:
		// The number of active layers is manually tracked since we re-use the framebuffers stored in the fb_layers stack.
		int layers_size = 0;

		int m_width = 0;
		int m_height = 0;

		Rml::Vector<Texture> fb_layers;
		Rml::Array<DepthStencilTexture, 5> fb_postprocess;

		ComPtr<ID3D11Device> m_device = {};
		ComPtr<ID3D11Texture2D> m_layers_depth_stencil = {};
		ComPtr<ID3D11DepthStencilView> m_layers_dsv = {};
	};

private:
	void DrawFullscreenQuad();
	void DrawFullscreenQuad(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling = Rml::Vector2f(1.f));

	void BlitRenderTarget(const Texture& source, const Texture& dest, int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1,
		int dstY1);

	// void RenderGeometry(CompiledGeometry* geometry, Rml::Vector2f translation);
	void RenderBlur(float sigma, const Texture& source_destination, const Texture& temp);

private:
	ComPtr<IDXGISwapChain1> m_swap_chain = {};

	ComPtr<ID3D11Device> m_device = {};
	ComPtr<ID3D11DeviceContext> m_device_context = {};
	ComPtr<ID3DUserDefinedAnnotation> m_annotation = {};

	D3D11Pipeline* m_pipeline = {};
	RenderLayerStack* m_render_layers = {};

	ComPtr<ID3D11Texture2D> m_back_buffer = {};

	// ComPtr<ID3D11Texture2D> m_depth_stencil = {};
	// ComPtr<ID3D11DepthStencilView> m_depth_stencil_view = {};

	Rml::Matrix4f m_transform = {};
	Rml::Matrix4f m_projection = {};
	Rml::Rectanglei m_scissor = {};
	Rml::Vector2i m_viewport = {};
	Rml::CompiledGeometryHandle m_fullscreen_quad_geometry = {};

	int m_stencil_ref = 0;
};

#endif
