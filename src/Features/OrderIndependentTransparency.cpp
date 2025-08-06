#include "OrderIndependentTransparency.h"
#include "ShaderCache.h"
#include "State.h"
#include "Upscaling.h"
#include "Features/TerrainBlending.h"
#include "Deferred.h"

NLOHMANN_JSON_SERIALIZE_ENUM(OrderIndependentTransparency::Method,
	{ 
		{ OrderIndependentTransparency::Method::Disabled, "Disabled" },
		{ OrderIndependentTransparency::Method::AdaptiveTransparency, "AT" }
	}
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OrderIndependentTransparency::Settings,
	Method,
	BufferSize,
	MaxLayers)

bool OrderIndependentTransparency::HasShaderDefine(RE::BSShader::Type shaderType)
{
	return shaderType != shaderType;
	// return shaderType == RE::BSShader::Type::Lighting
		//|| shaderType == RE::BSShader::Type::Water
		//|| shaderType == RE::BSShader::Type::Grass
		;
}

struct Main_RenderWorld_RenderTransparency
{
	// static void* __fastcall thunk(RE::NiCamera*, RE::BSShaderAccumulator*, std::int64_t);
	static void thunk(RE::BSShaderAccumulator* accumulator, uint32_t renderFlags);
	static inline REL::Relocation<decltype(thunk)> func;
};

struct Renderer_Flush_OMSetRenderTargets
{
	static void thunk(ID3D11DeviceContext* a_self, UINT a_numViews, ID3D11RenderTargetView** a_ppRenderTargetViews, ID3D11DepthStencilView* a_depthStencilView)
	{
		auto& oit = globals::features::orderIndependentTransparency;
		if (!oit.inAlphaPass)
			a_self->OMSetRenderTargets(a_numViews, a_ppRenderTargetViews, a_depthStencilView);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct Renderer_Flush
{
	static void thunk(RE::BSGraphics::Renderer* renderer, uint8_t flags)
	{
		// Prevent render target change during the alpha pass
		// Effect shader forced RT change in sub_1414EA1B0 + 0x43
		// And called renderer flush in sub_1414EA1B0 + 0x5E
		// So we have to hook in this level
		auto& oit = globals::features::orderIndependentTransparency;
		if (oit.inAlphaPass)
		{
			globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
			globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

			// Force depth to test only (not write)
			auto shadowState = globals::game::shadowState;
			GET_INSTANCE_MEMBER(depthStencil, shadowState);
			GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState);
			depthStencil = 0;
			depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTest;
		}
		func(renderer, flags);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

void OrderIndependentTransparency::PostPostLoad()
{
	logger::info("[OIT] Hooking OrderIndependentTransparency::Main_RenderWorld_RenderTransparency");

	// std::uintptr_t address = REL::RelocationID(100424, 107142).address() + REL::Relocate(0x3E4, 0x3FE);
	// REL::RelocationID(99938, 106583)
	// stl::write_thunk_call<Main_RenderWorld_RenderTransparency>(address);
	stl::detour_thunk<Main_RenderWorld_RenderTransparency>(REL::RelocationID(99940, 106585));

	logger::info("[OIT] Hooking Renderer_Flush");
	// Need RE for SSE/VR
	if (REL::Module::IsAE())
	{
		//REL::Relocation _target{ REL::ID(77247), 0x1A8 };
		//_target.write_call<6>(OMSetRenderTargets_Hook);
		//stl::write_thunk_call<Renderer_Flush_OMSetRenderTargets, 6>(REL::ID(77247).address() + 0x1A8);
		stl::detour_thunk<Renderer_Flush>(REL::RelocationID(77247, 77247));
		REL::IDDatabase::Offset2ID offset2id;
		auto id = offset2id(0x14EA1B0);
		logger::info("[OIT] Effect shader something id {}", id);
	}
}

void OrderIndependentTransparency::DataLoaded()
{
}

void OrderIndependentTransparency::DrawSettings()
{
	if (ImGui::TreeNodeEx("Method", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::RadioButton("Disable", (int*)&settings.Method, Method::Disabled);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Turn off OIT without unload it.");
		}
		ImGui::RadioButton("Visualization (For performance and tuning)", (int*)&settings.Method, Method::DebugVisualization);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Visualize the number of transparent layers to blend per pixel\n"
						"Useful to tune performance settings"
						"The color coding is blue, cyan, green, yellow, orange, red, purple, white.");
		}
		ImGui::RadioButton("Balanced (Enable)", (int*)&settings.Method, Method::AdaptiveTransparency);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("\"Adaptive Transparency\" technique.\n"
						"It provide accurate result before Max Layers is reach.\n"
						"After the limit, a minimal error estimation is used.\n"
						"This method is fast, but may have flicker or artifacts beyond max layer");
		}
		ImGui::RadioButton("Fast Approximation (*)", (int*)&settings.Method, Method::WeightedBlendedOIT);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("\"Weighted, Blended OIT\" technique.\n"
						"Fast but INACCURATE approximation of the OIT effect.\n"
						"Have MAJOR problem with additive blend layers\n"
						"like RAIN or SNOW weather");
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();
	if (ImGui::TreeNodeEx("Compatibility", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Checkbox("Multiplicative Blend Support (*)", (bool*)&settings.CaptureMultiplicativeLayer))
		{
			featureCB.Flags = settings.CaptureMultiplicativeLayer ? 1 : 0;
		}
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Whether to support multiplicative blend layer as __grayscale__ in OIT.\n"
						"(*) Colored multiplicative layer is __not__ supported.\n");
		}
		ImGui::Checkbox("Enforce Render Target (*)", (bool*)&settings.OverrideRenderTargets);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Force to override the transparent render target on every draw call.\n"
						"(*) Have a CPU cost, only enable this option if your alpha mesh is disappearing.\n"
						"(*) This option can only affect AE for now, needs more RE for SSE/VR version.");
		}
		ImGui::Checkbox("Use Pixel Shader (EXPERIMENTAL) (*)", (bool*)&settings.UsePixelShader);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Use pixel shader instead of compute shader for OIT resolve.\n"
						"(*) Mostly for performance compare, we will settle with PS or CS eventually.");
		}
		ImGui::TreePop();
	}
	ImGui::Spacing();
	if (ImGui::TreeNodeEx("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("Pass Time      = %6.2f ms", passTime);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("CPU Time spend in the engine's transparent pass");
		}
		ImGui::Text("Composite Time = %6.2f ms", compositeTime);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("CPU Time spend issuing the OIT composition draw");
		}
		bool bufferChanged = ImGui::SliderInt("Pixel Buffer Size", (int*)&settings.BufferSize, 2, 16);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Multiplier of the back buffer size for holding transparent layers.\n"
						"You may experience flickering when this buffer is overflowed.\n"
						"Adjust based on your system's performance and memory capacity.");
		}
		bool layerChanged = ImGui::SliderInt("Max Layers", (int*)&settings.MaxLayers, 4, 32);
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Maximum layers to blend in OIT composition\n"
						"Layers exceeding this limit may introduce rendering artifacts\n"
						"Increasing this value can have performance impact.");
		}
		bool thresholdChanged = ImGui::SliderFloat("Alpha Cutoff", &settings.AlphaThreshold, 0.0f, 0.1f, "%.3f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Alpha cutoff below which pixels are discarded.\n"
						"Increase this value to improve performance slightly but could have noticeable artifact.\n"
						"For example, increasing this value beyond 0.01 may cause rain particles to fade.");
		}
		thresholdChanged |= ImGui::SliderFloat("Depth Cutoff", &settings.DepthThreshold, 0.9f, 1.0f, "%.4f");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Camera space depth threshold to prevent z-fighting on 32 bits float precision limit.\n"
						"Decreasing this value if you notice z-fighting on overlapping transparent surfaces.\n"
						"However, decreasing this value too much may cause some surfaces to disappear.");
		}
		ImGui::SliderFloat("Distance Threshold", &settings.DistanceThreshold, 0.f, 500'000.f, "%.0f units");
		if (auto _tt = Util::HoverTooltipWrapper()) {
			ImGui::Text("Distance to camera before enabling OIT, to exclude large & complex distant volumetric fogs.\n"
						"Try decrease this value if you notice performance drop or flickering at distant foggy landscape."
						"Use `Visualize` mode to help you adjust this value to exclude the distant fogs."
						"1 `unit` = 1.428cm or 0.5625\"");
		}
		ImGui::TreePop();
		if (thresholdChanged) {
			featureCB.AlphaThreshold = settings.AlphaThreshold;
			featureCB.DepthThreshold = settings.DepthThreshold;
		}
		if (bufferChanged) {
			auto* renderer = globals::game::renderer;
			auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
			D3D11_TEXTURE2D_DESC mainDesc;
			mainTex.texture->GetDesc(&mainDesc);
			AllocateFragmentListNodes(settings.BufferSize * mainDesc.Width * mainDesc.Height);
		}
		if (layerChanged) 
		{
			ClearShaderCache();
		}
	}
}

void OrderIndependentTransparency::LoadSettings(json& o_json)
{
	settings = o_json;
	featureCB.AlphaThreshold = settings.AlphaThreshold;
	featureCB.DepthThreshold = settings.DepthThreshold;
	featureCB.Flags = settings.CaptureMultiplicativeLayer ? 1 : 0;
}

void OrderIndependentTransparency::SaveSettings(json& o_json)
{
	o_json = settings;
}

void OrderIndependentTransparency::RestoreDefaultSettings()
{
	settings = {};
	featureCB.AlphaThreshold = 0.f;
	featureCB.Flags = 1;
}

void SetupRenderTarget(RE::RENDER_TARGET target, D3D11_TEXTURE2D_DESC texDesc, D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc, D3D11_RENDER_TARGET_VIEW_DESC rtvDesc, D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc, DXGI_FORMAT format, uint bindFlags);

void OrderIndependentTransparency::SetupResources()
{
	auto* renderer = globals::game::renderer;
	auto* device = globals::d3d::device;
	// When you want to align with the main texture format
	auto& mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	D3D11_TEXTURE2D_DESC mainDesc;
	mainTex.texture->GetDesc(&mainDesc);
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

		mainTex.SRV->GetDesc(&srvDesc);
		mainTex.RTV->GetDesc(&rtvDesc);
		mainTex.UAV->GetDesc(&uavDesc);

		SetupRenderTarget(RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA, mainDesc, srvDesc, rtvDesc, uavDesc, mainDesc.Format, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
	}

	logger::info("Setting up Order Independent Transparency resources...");
	{
		auto texDesc = mainDesc;
		// CD3D11_TEXTURE2D_DESC texDesc;
		texDesc.Format = DXGI_FORMAT_R32_UINT;
		//texDesc.Width = mainDesc.Width;
		//texDesc.Height = mainDesc.Height;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		try {
			fragmentListHead.emplace(texDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create fragment list head texture @{}x{}: {}", mainDesc.Width, mainDesc.Height, e.what());
			return;
		}

		CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(
			D3D11_SRV_DIMENSION_TEXTURE2D,
			texDesc.Format
		);
		CD3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc(
			D3D11_UAV_DIMENSION_TEXTURE2D,
			texDesc.Format);
		try {
			fragmentListHead->CreateSRV(srvDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create fragment list head SRV: {}", e.what());
			return;
		}
		try {
			fragmentListHead->CreateUAV(uavDesc);
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create fragment list head UVA: {}", e.what());
			return;
		}
	}

	{
		UINT numElem = settings.BufferSize * mainDesc.Width * mainDesc.Height;
		AllocateFragmentListNodes(numElem);
	}

	{
		D3D11_DEPTH_STENCIL_DESC depthStencilDesc{};
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		depthStencilDesc.StencilEnable = false;
		try {
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, depthStencilState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create depth stencil state {}", e.what());
			return;
		}
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
		try {
			DX::ThrowIfFailed(device->CreateDepthStencilState(&depthStencilDesc, resolveDepthStencilState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create depth stencil state {}", e.what());
			return;
		}
		D3D11_BLEND_DESC blendDesc{};
		blendDesc.AlphaToCoverageEnable = false;
		blendDesc.IndependentBlendEnable = false;
		blendDesc.RenderTarget[0].BlendEnable = true;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		try {
			DX::ThrowIfFailed(device->CreateBlendState(&blendDesc, resolveBlendState.put()));
		} catch (const DX::com_exception& e) {
			logger::error("Failed to create blend state {}", e.what());
			return;
		}
	}

	CompileShaders();
	logger::info("Order Independent Transparency resources setup complete.");
}

void OrderIndependentTransparency::ClearShaderCache()
{
	resolveCS.detach();
	debugCS.detach();
	blendCS.detach();
	resolvePS.detach();
	debugPS.detach();
	blendPS.detach();
	CompileShaders();
}

void OrderIndependentTransparency::CompileShaders()
{
	// OIT_NODE_COUNT = MaxLayers
	uint nodes = std::clamp<uint>(settings.MaxLayers, 1, 32);
	nodes = nodes % 4 == 0 ? nodes : nodes - nodes % 4 + 4;
	char nodesStr[4] = { 0 };
	std::to_chars(nodesStr, nodesStr + 4, nodes);

	logger::info("Compiling Order Independent Transparency shaders, OIT_NODE_COUNT={}...", nodesStr);

	if (!resolveCS)
	{
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.cs.hlsl", { { "OIT_NODE_COUNT", nodesStr } }, "cs_5_0"))) {
			resolveCS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency resolve compute shader.");
			return;
		}
	}
	if (!debugCS) {
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.cs.hlsl", { { "OIT_DEBUG", "1" } }, "cs_5_0"))) {
			debugCS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency visualize compute shader.");
			return;
		}
	}
	if (!blendCS) {
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.cs.hlsl", { { "OIT_BLENDED", "1" } }, "cs_5_0"))) {
			blendCS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency weighted blend compute shader.");
			return;
		}
	}
	if (!debugPS) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.ps.hlsl", { { "OIT_DEBUG", "1" } }, "ps_5_0"))) {
			debugPS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency debug pixel shader.");
			return;
		}
	}
	if (!resolvePS) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.ps.hlsl", { { "OIT_NODE_COUNT", nodesStr } }, "ps_5_0"))) {
			resolvePS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency resolve pixel shader.");
			return;
		}
	}
	if (!blendPS) {
		if (auto rawPtr = reinterpret_cast<ID3D11PixelShader*>(Util::CompileShader(L"Data\\Shaders\\OIT\\DXResolve.ps.hlsl", { { "OIT_BLENDED", "1" } }, "ps_5_0"))) {
			blendPS.attach(rawPtr);
		} else {
			logger::error("Failed to compile Order Independent Transparency blend pixel shader.");
			return;
		}
	}
}

void OrderIndependentTransparency::AllocateFragmentListNodes(uint numElem)
{
	static constexpr UINT structSize = sizeof(FragmentListNode);
	featureCB.MaxListNodes = numElem;

	CD3D11_BUFFER_DESC bufferDesc(
		structSize * numElem,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
		D3D11_USAGE_DEFAULT,
		0,
		D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
		structSize);
	try {
		fragmentListNode.emplace(bufferDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create fragment list nodes buffer: {}", e.what());
		return;
	}

	CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(
		D3D11_SRV_DIMENSION_BUFFER,
		DXGI_FORMAT_UNKNOWN,
		0, numElem);
	CD3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc(
		D3D11_UAV_DIMENSION_BUFFER,
		DXGI_FORMAT_UNKNOWN,
		0, numElem, UINT(-1), D3D11_BUFFER_UAV_FLAG_COUNTER);
	try {
		fragmentListNode->CreateSRV(srvDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create fragment list nodes SRV: {}", e.what());
		return;
	}
	try {
		fragmentListNode->CreateUAV(uavDesc);
	} catch (const DX::com_exception& e) {
		logger::error("Failed to create fragment list nodes UAV: {}", e.what());
		return;
	}

}

enum AlphaBlendMode : uint32_t
{
	kAlpha = 1,
	kAdditive = 2,
	kMultiplicative = 4,
};

void OrderIndependentTransparency::PreSetStateDirty(bool)
{
	if (!inAlphaPass) {
		return;
	}
	globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(false, RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);

	// Force depth to test only (not write)
	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(depthStencil, shadowState);
	GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState);
	depthStencil = 0;
	depthStencilDepthMode = RE::BSGraphics::DepthStencilDepthMode::kTest;
}

void OrderIndependentTransparency::PreDrawHack()
{
	static constexpr uint OITAddtiveDescriptor = std::to_underlying(State::ExtraFeatureDescriptors::OITAdditive);
	static constexpr uint OITMultiplicativeDescriptor = std::to_underlying(State::ExtraFeatureDescriptors::OITMultiplicative);
	static constexpr uint OITDepthWriteDescriptor = std::to_underlying(State::ExtraFeatureDescriptors::OITDepthWrite);

	auto& descriptor = globals::state->permutationData.ExtraFeatureDescriptor;
	descriptor &= ~(OITAddtiveDescriptor | OITMultiplicativeDescriptor | OITDepthWriteDescriptor);

	if (!inAlphaPass) {
		return;
	}

	auto shadowState = globals::game::shadowState;
	GET_INSTANCE_MEMBER(alphaBlendMode, shadowState);
	GET_INSTANCE_MEMBER(depthStencilDepthMode, shadowState);

	if (alphaBlendMode == 3 || alphaBlendMode > 4) [[unlikely]] {
		winrt::com_ptr<ID3D11BlendState> blend = nullptr;
		globals::d3d::context->OMGetBlendState(blend.put(), nullptr, nullptr);
		D3D11_BLEND_DESC desc;
		blend->GetDesc(&desc);

		auto dest = desc.RenderTarget[0].DestBlend;
		auto src = desc.RenderTarget[0].SrcBlend;
		
		logger::warn("unknown alpha blend mode {}, DestBlend {} , SrcBlend {}", alphaBlendMode, magic_enum::enum_name(dest), magic_enum::enum_name(src));
	}

	if (alphaBlendMode == AlphaBlendMode::kAdditive)
	{
		// Additive blend have effective 
		descriptor |= OITAddtiveDescriptor;
	}
	else if (alphaBlendMode == AlphaBlendMode::kMultiplicative)
	{
		descriptor |= OITMultiplicativeDescriptor;
	}
	if (depthStencilDepthMode == RE::BSGraphics::DepthStencilDepthMode::kWrite || depthStencilDepthMode == RE::BSGraphics::DepthStencilDepthMode::kTestWrite) {
		// If depth write is enabled, we need to disable it for OIT
		descriptor |= OITDepthWriteDescriptor;
	}

	if (settings.OverrideRenderTargets || !REL::Module::IsAE())
	{
		globals::d3d::context->OMSetRenderTargetsAndUnorderedAccessViews(3, rtvs.data(), dsv, 3, 2, uavs.data(), nullptr);
		lastVS = *globals::game::currentPixelShader;
	}
}

void OrderIndependentTransparency::SetupGeometry(RE::BSShader*, RE::BSRenderPass* pass, uint32_t)
{
	if (!inAlphaPass)
		return;
	if (closeEnough)
		return;

	const RE::NiBound& geometryBound = pass->geometry->worldBound;
	auto position = geometryBound.center;
	// float distance = position.GetDistance(cameraPos);
	auto viewPos = cameraWorldInverse * position;

	//wchar_t buffer[256];
	//*fmt::format_to_n(buffer, std::size(buffer) - 1, "SetupGeometry Distance<{}>", distance).out = '\0';
	//globals::state->SetPerfMarker(buffer);

	// logger::debug("distance = {}, view position = {}", distance, viewPos);
	//float rdistance = distance - geometryBound.radius;
	if (viewPos.y < settings.DistanceThreshold) {
		closeEnough = true;
		// logger::debug("Close enough for OIT");
	}
}

void OrderIndependentTransparency::BeginAlphaGroup()
{
	EndWater();

	cameraPos = Util::GetAverageEyePosition();
	auto cameraWorld = RE::PlayerCamera::GetSingleton()->cameraRoot->world;
	cameraWorldInverse = cameraWorld.Invert();
	inAlphaPass = true;
	calls = 0;
	lastVS = nullptr;
	closeEnough = false;

	logger::debug("Beginning OIT alpha group camera pos = {}, camera world position = {}", cameraPos, cameraWorld.translate);

	static constexpr bool resetUAVCounter = true;

	ID3D11DeviceContext* context = globals::d3d::context;
	ID3D11UnorderedAccessView* fragmentListHeadUAV = fragmentListHead->uav.get();
	// context->OMGetRenderTargets(0, NULL, sceneDSV.put());

	// Initialize the first node offset RW UAV with a NULL offset (end of the list)
	static constexpr UINT clearValuesHead[4] = {
		0x0UL,
		0x0UL,
		0x0UL,
		0x0UL
	};

	context->ClearUnorderedAccessViewUint(fragmentListHeadUAV, clearValuesHead);
	// FillFragmentListConstants(pD3DImmediateContext, mLisTexNodeCount * 2);

	auto* renderer = globals::game::renderer;
	uavs = { fragmentListHead->uav.get(), fragmentListNode->uav.get() };
	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& TAAMask = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kTEMPORAL_AA_MASK];
	auto& alphaOnly = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	// Need to capture pre-water depth
	auto& preWaterDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];
	rtvs = { main.RTV, TAAMask.RTV, alphaOnly.RTV };
	dsv = preWaterDepth.readOnlyViews[0];
	static constexpr UINT uavcounters[2] = { 1, 1 };

	context->OMSetRenderTargetsAndUnorderedAccessViews(3, rtvs.data(), dsv, 3, 2, uavs.data(), uavcounters);
	context->OMSetDepthStencilState(depthStencilState.get(), 0xFF);
}

template <typename ShaderType, typename ViewType, size_t N>
struct ScopedShaderResource
{
	template <typename... TArgs>
	ScopedShaderResource(ShaderType* , ViewType* (&_views)[N], TArgs&&... _args)
	{
		if constexpr (std::is_same_v<ShaderType, ID3D11PixelShader>)
		{
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>)
			{
				globals::d3d::context->PSSetShaderResources(_args..., N, _views);
			} 
			else if constexpr (std::is_same_v<ViewType, ID3D11RenderTargetView>) 
			{
				globals::d3d::context->OMSetRenderTargets(N, _views, _args...);
			}
		}
		else if constexpr (std::is_same_v<ShaderType, ID3D11ComputeShader>)
		{
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>)
			{
				globals::d3d::context->CSSetShaderResources(_args..., N, _views);
			}
			else if constexpr (std::is_same_v<ViewType, ID3D11UnorderedAccessView>)
			{
				globals::d3d::context->CSSetUnorderedAccessViews(_args..., N, _views, nullptr);
			}
		}
	}

	~ScopedShaderResource()
	{
		ViewType* views[N] = { 0 };
		if constexpr (std::is_same_v<ShaderType, ID3D11PixelShader>) {
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>) {
				globals::d3d::context->PSSetShaderResources(0, N, views);
			} else if constexpr (std::is_same_v<ViewType, ID3D11RenderTargetView>) {
				globals::d3d::context->OMSetRenderTargets(N, views, nullptr);
			}
		} else if constexpr (std::is_same_v<ShaderType, ID3D11ComputeShader>) {
			if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>) {
				globals::d3d::context->CSSetShaderResources(0, N, views);
			} else if constexpr (std::is_same_v<ViewType, ID3D11UnorderedAccessView>) {
				globals::d3d::context->CSSetUnorderedAccessViews(0, N, views, nullptr);
			}
		}
	}
};

template <typename ShaderType, typename ViewType, size_t N, typename... TArgs>
ScopedShaderResource(ShaderType*, ViewType* (&)[N], TArgs&&...) -> ScopedShaderResource<ShaderType, ViewType, N>;

void OrderIndependentTransparency::EndAlphaGroup()
{
	inAlphaPass = false;
	closeEnough = false;

	logger::debug("End OIT alpha group.");

	TracyD3D11Zone(globals::state->tracyCtx, "OIT Composite");
	ID3D11DeviceContext* context = globals::d3d::context;
	auto* renderer = globals::game::renderer;

	// First unbind fragment lists from UAV
	{
		ID3D11RenderTargetView* _rtvs[3] = { nullptr, nullptr, nullptr };
		ID3D11UnorderedAccessView* _uavs[2] = { nullptr, nullptr };
		context->OMSetRenderTargetsAndUnorderedAccessViews(3, _rtvs, nullptr, 3, 2, _uavs, nullptr);
	}

	auto& main = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	auto& alpha = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	auto& mainDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN];

	// Water depth is rendered at kMain after water pass
	// But mainDepth.depthSRV was REDIRECTED by terrain blending to its own copy (for UAV access)
	// At this point, we need to access the actual main depth as SRV
	using globals::features::terrainBlending;
	ID3D11ShaderResourceView* waterDepthSrv = terrainBlending.loaded ? terrainBlending.depthSRVBackup : nullptr;
	waterDepthSrv = waterDepthSrv ? waterDepthSrv : mainDepth.depthSRV;
	ID3D11ShaderResourceView* srvs[] = { fragmentListHead->srv.get(), fragmentListNode->srv.get(), waterDepthSrv };

	if (settings.UsePixelShader)
	{
		ID3D11PixelShader* shader = nullptr;
		switch (settings.Method) {
		case Method::AdaptiveTransparency:
			shader = resolvePS.get();
			break;
		case Method::WeightedBlendedOIT:
			shader = blendPS.get();
			break;
		case Method::DebugVisualization:
			shader = debugPS.get();
			break;
		}

		// Set up viewport for fullscreen rendering
		auto screenSize = globals::state->screenSize;

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = screenSize.x;
		viewport.Height = screenSize.y;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set up Input Assembler for fullscreen triangle
		context->IASetInputLayout(nullptr);
		context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// Set up vertex shader
		context->VSSetShader(globals::features::upscaling.GetUpscaleVS(), nullptr, 0);

		// Set up rasterizer and blend states
		context->RSSetState(globals::features::upscaling.upscaleRasterizerState.get());
		context->OMSetBlendState(resolveBlendState.get(), nullptr, 0xffffffff);
		context->OMSetDepthStencilState(resolveDepthStencilState.get(), 1);

		// Set up pixel shader resources
		ID3D11RenderTargetView* _rtvs[2] = { main.RTV, alpha.RTV };
		auto& preWaterDepth = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY];

		ScopedShaderResource srvGuard ( shader, srvs, 0 );
		ScopedShaderResource rtvGuard ( shader, _rtvs, preWaterDepth.views[0] );

		context->PSSetShader(shader, nullptr, 0);

		context->Draw(3, 0);

		context->PSSetShader(nullptr, nullptr, 0);
		context->VSSetShader(nullptr, nullptr, 0);
	}
	else
	{
		ID3D11ComputeShader* shader = nullptr;
		switch (settings.Method) {
			case Method::AdaptiveTransparency:
				shader = resolveCS.get();
				break;
			case Method::WeightedBlendedOIT:
				shader = blendCS.get();
				break;
			case Method::DebugVisualization:
				shader = debugCS.get();
				break;
		}
		context->CSSetShader(shader, NULL, 0);

		ID3D11UnorderedAccessView* _uavs[2] = { main.UAV, alpha.UAV };
		ScopedShaderResource srvGuard(shader, srvs, 0);
		ScopedShaderResource uavGuard(shader, _uavs, 0);
		auto dispatchCount = Util::GetScreenDispatchCount();
		context->Dispatch(dispatchCount.x, dispatchCount.y, 1);

		context->CSSetShader(nullptr, nullptr, 0);
	}	

	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_RENDERTARGET);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
	globals::game::stateUpdateFlags->set(RE::BSGraphics::ShaderFlags::DIRTY_ALPHA_BLEND);
}

void OrderIndependentTransparency::BeginWater()
{
	if (settings.Method == Method::Disabled)
		return;
	auto* renderer = globals::game::renderer;
	ID3D11DeviceContext* context = globals::d3d::context;
	auto& alpha = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_ONLY_ALPHA];
	ID3D11ShaderResourceView* srv[] = { alpha.SRV };
	context->PSSetShaderResources(66, ARRAYSIZE(srv), srv);
	// context->PSSetSamplers(66, 1, &globals::deferred->linearSampler);
}

void OrderIndependentTransparency::EndWater()
{
	ID3D11DeviceContext* context = globals::d3d::context;
	ID3D11ShaderResourceView* srv[] = { nullptr };
	context->PSSetShaderResources(66, ARRAYSIZE(srv), srv);
}

void Main_RenderWorld_RenderTransparency::thunk(RE::BSShaderAccumulator* accumulator, uint32_t flags)
{
	using namespace std::chrono;
	using clock = high_resolution_clock;
	auto& oit = globals::features::orderIndependentTransparency;
	bool began = false;
	auto passBegin = clock::now();
	if (globals::shaderCache->IsEnabled() && globals::state->inWorld && oit.loaded && oit.settings.Method != OrderIndependentTransparency::Method::Disabled)
	{
		began = true;
		// Override the pixel shaders & OM state for OIT
		oit.BeginAlphaGroup();
	}

	// Render the alpha blending geometries
	{
		TracyD3D11Zone(globals::state->tracyCtx, "Transparency");
		(*func)(accumulator, flags);
	}
	auto passEnd = clock::now();
	oit.passTime = duration_cast<duration<float, std::milli>>(passEnd - passBegin).count();

	if (began)
	{
		// OIT resolve and blend in image space
		oit.EndAlphaGroup();
		oit.compositeTime = duration_cast<duration<float, std::milli>>(clock::now() - passEnd).count();
	} else {
		oit.compositeTime = 0;
	}
}
