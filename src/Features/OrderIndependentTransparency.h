#pragma once
#include "Feature.h"

struct OrderIndependentTransparency : Feature
{
	virtual inline std::string GetName() override { return "Order Independent Transparency"; }
	virtual inline std::string GetShortName() override { return "OrderIndependentTransparency"; }
	virtual inline std::string_view GetShaderDefineName() override { return "OIT"; }
	virtual std::string_view GetCategory() const override { return "Materials"; }

	virtual std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Order Independent Transparency (OIT) allow translucent objects like water, hair, glass, foliage and smoke to be blended correctly when overlapping with each other.\n"
			,
			{ "Fixing Water Reflection & Refraction" }
		};
	}

	bool HasShaderDefine(RE::BSShader::Type shaderType) override;

	// Implementation method
	// The difficulty of implementing more method here is the shader descriptor flags / defines
	// We will take way too much flags for that
	enum Method : int
	{
		Disabled,
		DebugVisualization,
		AdaptiveTransparency,
		WeightedBlendedOIT,
		MultiLayerAlphaBlend,	// To be implemented
	};

	struct Settings
	{
		Method	Method = AdaptiveTransparency;  // Method to use for OIT
		uint	BufferSize = 8; // Multiplier of the back buffer size for holding Fragment List Node
		uint	MaxLayers = 8;  // Maximum layers in OIT resolution, layers exceeding this limit may introduce rendering artifacts
		float	AlphaThreshold = 0.f; // Alpha cutoff below which pixels are discarded
		float	DepthThreshold = 1.f; // Camera space depth threshold to prevent z-fighting on 32 bits float precision limit
		float	DistanceThreshold = 60'000.f; // Distance to camera before enabling OIT, to exclude large & complex distant volumetric fogs
		bool	CaptureMultiplicativeLayer = true; // Whether to support multiplicative blend mode
		bool	OverrideRenderTargets = false; // Force override render target in alpha pass, only use you having issue
		bool	UsePixelShader = false; // Whether to use pixel shader or compute shader for OIT resolve
	};

	struct alignas(16) FeatureCB
	{
		uint	MaxListNodes = 0;
		uint	Flags = 0;
		float	AlphaThreshold = 0.f;
		float	DepthThreshold = 1.f;
	};

	FeatureCB featureCB;

	Settings settings;

	float passTime; // API Time spend in the transparency (material) pass
	float compositeTime; // API Time spend in the composite pass

	const auto& GetCommonBufferData() const { return featureCB; }

	virtual void PostPostLoad() override;

	virtual void DataLoaded() override;

	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	void CompileShaders();
	void AllocateFragmentListNodes(uint numElem);
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;

	virtual bool SupportsVR() override { return true; };
	virtual bool IsCore() const override { return false; };

	void PreSetStateDirty(bool isCompute);
	void PreDrawHack();
	void SetupGeometry(RE::BSShader* shader, RE::BSRenderPass* pass, uint32_t renderFlags);
	void BeginAlphaGroup();
	void EndAlphaGroup();
	void BeginWater();
	void EndWater();
	[[nodiscard]] bool ShouldCapture() const { return inAlphaPass && closeEnough; }

	struct FragmentListNode
	{
		uint next;
		float depth;
		uint color;
	};

	// GPU Resources
	std::optional<Texture2D>				fragmentListHead;
	std::optional<Buffer>					fragmentListNode;

	winrt::com_ptr<ID3D11ComputeShader>		debugCS;   // For visualizing the fragment count
	winrt::com_ptr<ID3D11ComputeShader>		resolveCS; // For adaptive transparency
	winrt::com_ptr<ID3D11ComputeShader>		blendCS;   // For weighted blended OIT

	winrt::com_ptr<ID3D11PixelShader>			resolvePS;   // For adaptive transparency
	winrt::com_ptr<ID3D11PixelShader>			debugPS;   // For adaptive transparency
	winrt::com_ptr<ID3D11PixelShader>			blendPS;   // For adaptive transparency
	winrt::com_ptr<ID3D11DepthStencilState>		depthStencilState; // depth testing but not writing
	winrt::com_ptr<ID3D11BlendState>			resolveBlendState; // depth testing but not writing
	winrt::com_ptr<ID3D11DepthStencilState>		resolveDepthStencilState;  // depth testing but not writing

	// Temporarily for setting render target in alpha pass, not reference counted
	std::array<ID3D11RenderTargetView*,3>		rtvs; 
	std::array<ID3D11UnorderedAccessView*, 2>	uavs;
	ID3D11DepthStencilView*						dsv;
	int											calls = 0;
	void*										lastVS = nullptr;
	RE::NiPoint3								cameraPos;
	RE::NiTransform								cameraWorldInverse;
	bool										closeEnough = false;

	// States
	bool inAlphaPass = false;  // Whether we are in alpha pass of the render
};
