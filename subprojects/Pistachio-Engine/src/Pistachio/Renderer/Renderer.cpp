#include "Barrier.h"
#include "FormatsAndTypes.h"
#include "PipelineStateObject.h"
#include "Pistachio/Renderer/RenderGraph.h"
#include "Pistachio/Renderer/RendererBase.h"
#include "Pistachio/Renderer/Shader.h"
#include "Texture.h"
#include "ptpch.h"
#include "Renderer.h"
#include "Material.h"
#include "../Scene/Scene.h"
#include "Pistachio/Core/Window.h"
#include "Pistachio/Core/Math.h"
DirectX::XMMATRIX Pistachio::Renderer::viewproj = DirectX::XMMatrixIdentity();
DirectX::XMVECTOR Pistachio::Renderer::m_campos = DirectX::XMVectorZero();
Pistachio::Texture2D Pistachio::Renderer::BrdfTex;
Pistachio::RenderCubeMap Pistachio::Renderer::skybox = Pistachio::RenderCubeMap();
Pistachio::RenderCubeMap Pistachio::Renderer::irradianceSkybox = Pistachio::RenderCubeMap();
Pistachio::RenderCubeMap Pistachio::Renderer::prefilterSkybox = { Pistachio::RenderCubeMap() };
Pistachio::StructuredBuffer Pistachio::Renderer::computeShaderMiscBuffer;
std::vector<Pistachio::RegularLight>   Pistachio::Renderer::RegularLightData = {};
std::vector<Pistachio::ShadowCastingLight>   Pistachio::Renderer::ShadowLightData = {};
std::vector<std::uint8_t> Pistachio::Renderer::LightSBCPU;
std::unordered_map<std::string, Pistachio::ComputeShader*> Pistachio::Renderer::computeShaders;
std::unordered_map<std::string, Pistachio::Shader*> Pistachio::Renderer::shaders;
Pistachio::Renderer::CamerData Pistachio::Renderer::CameraData = {};
Pistachio::Texture2D Pistachio::Renderer::whiteTexture = Pistachio::Texture2D();
Pistachio::Material* Pistachio::Renderer::currentMat = nullptr;
Pistachio::Shader* Pistachio::Renderer::currentShader = nullptr;
Pistachio::SetInfo Pistachio::Renderer::eqShaderVS[6];
Pistachio::SetInfo Pistachio::Renderer::eqShaderPS;
Pistachio::SetInfo Pistachio::Renderer::irradianceShaderPS;
Pistachio::SetInfo Pistachio::Renderer::prefilterShaderVS[5];
Pistachio::DepthTexture Pistachio::Renderer::shadowMapAtlas;
Pistachio::Shader* Pistachio::Renderer::eqShader;
Pistachio::Shader* Pistachio::Renderer::irradianceShader;
Pistachio::Shader* Pistachio::Renderer::brdfShader;
Pistachio::Shader* Pistachio::Renderer::prefilterShader;
Pistachio::SamplerHandle Pistachio::Renderer::defaultSampler;
Pistachio::SamplerHandle Pistachio::Renderer::brdfSampler;
Pistachio::SamplerHandle Pistachio::Renderer::shadowSampler;
RHI::Ptr<RHI::Buffer>             Pistachio::Renderer::meshVertices; // all meshes in the scene?
RHI::Ptr<RHI::Buffer>             Pistachio::Renderer::meshIndices;
uint32_t                 Pistachio::Renderer::vbFreeFastSpace;//free space for an immerdiate allocation
uint32_t                 Pistachio::Renderer::vbFreeSpace;   //total free space to consider reordering
uint32_t                 Pistachio::Renderer::vbCapacity;
Pistachio::FreeList      Pistachio::Renderer::vbFreeList;
uint32_t                 Pistachio::Renderer::ibFreeFastSpace;
uint32_t                 Pistachio::Renderer::ibFreeSpace;
uint32_t                 Pistachio::Renderer::ibCapacity;
uint32_t                 Pistachio::Renderer::cbCapacity;
uint32_t                 Pistachio::Renderer::cbFreeSpace;
uint32_t                 Pistachio::Renderer::cbFreeFastSpace;
Pistachio::FreeList      Pistachio::Renderer::cbFreeList;
Pistachio::FreeList      Pistachio::Renderer::ibFreeList;
Pistachio::FrameResource Pistachio::Renderer::resources[3];
std::vector<uint32_t>    Pistachio::Renderer::ibHandleOffsets;
std::vector<uint32_t>    Pistachio::Renderer::ibUnusedHandles;
std::vector<uint32_t>    Pistachio::Renderer::vbHandleOffsets;
std::vector<uint32_t>    Pistachio::Renderer::vbUnusedHandles;
std::vector<uint32_t>    Pistachio::Renderer::cbHandleOffsets;
std::vector<uint32_t>    Pistachio::Renderer::cbUnusedHandles; 
Pistachio::SetInfo 		 Pistachio::Renderer::backgroundInfo;
Pistachio::Mesh    		 Pistachio::Renderer::cube;

void (*Pistachio::Renderer::CBInvalidated)() = nullptr;
static const uint32_t VB_INITIAL_SIZE = 1024;
static const uint32_t IB_INITIAL_SIZE = 1024;
static const uint32_t INITIAL_NUM_LIGHTS = 20;
static const uint32_t INITIAL_NUM_OBJECTS = 20;

static const uint32_t NUM_SKYBOX_MIPS = 5;

namespace Pistachio {
	void Renderer::Init(const char* skyboxFile)
	{
		PT_PROFILE_FUNCTION();
		{
			//Create constant and structured buffers needed for each frame in flight
			PT_CORE_INFO("Initializing Renderer");

			//vertex buffer
			PT_CORE_INFO("Creating Vertex Buffer");
			RHI::BufferDesc bufferDesc;
			bufferDesc.size = VB_INITIAL_SIZE;
			bufferDesc.usage = RHI::BufferUsage::VertexBuffer | RHI::BufferUsage::CopyDst | RHI::BufferUsage::CopySrc;
			RHI::AutomaticAllocationInfo bufferAllocInfo;
			bufferAllocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::None;
			meshVertices = RendererBase::device->CreateBuffer(&bufferDesc, 0, 0, &bufferAllocInfo, 0, RHI::ResourceType::Automatic).value();
			//index buffer
			PT_CORE_INFO("Creating Index Buffer");
			bufferDesc.size = IB_INITIAL_SIZE;
			bufferDesc.usage = RHI::BufferUsage::IndexBuffer | RHI::BufferUsage::CopyDst | RHI::BufferUsage::CopySrc;
			meshIndices = RendererBase::device->CreateBuffer(&bufferDesc, 0, 0, &bufferAllocInfo, 0, RHI::ResourceType::Automatic).value();
			//set constants for vb and ib
			vbCapacity = VB_INITIAL_SIZE;
			vbFreeFastSpace = VB_INITIAL_SIZE;
			vbFreeSpace = VB_INITIAL_SIZE;
			//---------------------------
			ibCapacity = IB_INITIAL_SIZE;
			ibFreeFastSpace = IB_INITIAL_SIZE;
			ibFreeSpace = IB_INITIAL_SIZE;
			//Create Free lists
			vbFreeList = FreeList(VB_INITIAL_SIZE);
			ibFreeList = FreeList(IB_INITIAL_SIZE);
			cbFreeList = FreeList(RendererUtils::ConstantBufferElementSize(sizeof(TransformData)) * INITIAL_NUM_OBJECTS);

			cbCapacity = RendererUtils::ConstantBufferElementSize(sizeof(TransformData)) * INITIAL_NUM_OBJECTS;
			cbFreeFastSpace = cbCapacity;
			cbFreeSpace = cbCapacity;
			for (uint32_t i = 0; i < 3; i++)
			{
				PT_CORE_INFO("Initializing Frame Resources {0} of 3", i + 1);
				PT_CORE_INFO("Creating Constant Buffer(s)");
				resources[i].transformBuffer.CreateStack(nullptr, cbCapacity);//better utilise the free 256 bytes
				PT_CORE_INFO("Creating Structured Buffer(s)");
				resources[i].transformBufferDesc = RendererBase::device->CreateDynamicDescriptor(
					RendererBase::heap.Get(),
					RHI::DescriptorType::ConstantBufferDynamic,
					RHI::ShaderStage::Vertex,
					resources[i].transformBuffer.ID.Get(),
					0,
					256).value();
				resources[i].transformBufferDescPS = RendererBase::device->CreateDynamicDescriptor(
					RendererBase::heap.Get(),
					RHI::DescriptorType::ConstantBufferDynamic,
					RHI::ShaderStage::Pixel,
					resources[i].transformBuffer.ID.Get(),
					0,
					256).value();
			}
		}
		
		
		{
			RHI::SamplerDesc sampler;
			sampler.AddressU = RHI::AddressMode::Clamp;
			sampler.AddressV = RHI::AddressMode::Clamp;
			sampler.AddressW = RHI::AddressMode::Clamp;
			sampler.anisotropyEnable = false;
			sampler.compareEnable = false;
			sampler.magFilter = RHI::Filter::Linear;
			sampler.minFilter = RHI::Filter::Linear;
			sampler.mipFilter = RHI::Filter::Linear;
			sampler.maxLOD = std::numeric_limits<float>::max();
			sampler.minLOD = 0;
			sampler.mipLODBias = 0;
			defaultSampler = RendererBase::CreateSampler(&sampler);
			brdfSampler = RendererBase::CreateSampler(&sampler);
			sampler.compareEnable = true;
			sampler.compareFunc = RHI::ComparisonFunc::LessEqual;
			shadowSampler = RendererBase::CreateSampler(&sampler);
		}

		computeShaderMiscBuffer.CreateStack(nullptr, sizeof(uint32_t) * 2, SBCreateFlags::None);

		RHI::BlendMode blendMode{};
		blendMode.BlendAlphaToCoverage = false;
		blendMode.IndependentBlend = true;
		blendMode.blendDescs[0].blendEnable = false;
		RHI::DepthStencilMode dsMode{};
		Pistachio::Helpers::FillDepthStencilMode(&dsMode);
		RHI::RasterizerMode rsMode{};
		rsMode.cullMode = RHI::CullMode::None;
		rsMode.fillMode = RHI::FillMode::Solid;
		rsMode.topology = RHI::PrimitiveTopology::TriangleList;
		ShaderCreateDesc ShaderDesc{};
		Pistachio::Helpers::ZeroAndFillShaderDesc(&ShaderDesc, 
			"resources/shaders/vertex/Compiled/equirectangular_to_cubemap_vs", 
			"resources/shaders/pixel/Compiled/equirectangular_to_cubemap_fs");
		ShaderDesc.BlendModes = &blendMode;
		ShaderDesc.DepthStencilModes = &dsMode;
		ShaderDesc.RasterizerModes = &rsMode;
		ShaderDesc.numBlendModes = 1;
		ShaderDesc.numDepthStencilModes = 1;
		ShaderDesc.numRasterizerModes = 1;
		ShaderDesc.InputDescription = Pistachio::Mesh::GetLayout();
		ShaderDesc.numInputs = Pistachio::Mesh::GetLayoutSize();
		ShaderDesc.RTVFormats[0] = RHI::Format::R16G16B16A16_FLOAT;
		eqShader = Shader::Create(&ShaderDesc);
		eqShader->GetPSShaderBinding(eqShaderPS, 1);
		for(uint32_t i = 0; i < 6; i++)
			eqShader->GetVSShaderBinding(eqShaderVS[i], 0);
		skybox.CreateStack(512, 512, NUM_SKYBOX_MIPS,  RHI::Format::R16G16B16A16_FLOAT PT_DEBUG_REGION(,"Renderer -> Skybox Texture"), RHI::TextureUsage::CopySrc | RHI::TextureUsage::CopyDst);
		ShaderDesc.PS = { (char*)"resources/shaders/pixel/Compiled/irradiance_fs",0 };
		irradianceShader = Shader::Create(&ShaderDesc);
		irradianceShader->GetPSShaderBinding(irradianceShaderPS,1);
		irradianceSkybox.CreateStack(32, 32, 1, RHI::Format::R16G16B16A16_FLOAT, "Renderer -> Irradiance Texture");

		ShaderDesc.VS = {(char*)"resources/shaders/vertex/Compiled/prefilter_vs",0};
		ShaderDesc.PS = {(char*)"resources/shaders/pixel/Compiled/prefilter_fs" ,0};
		prefilterShader = Shader::Create(&ShaderDesc);
		for(uint32_t i = 0; i < 5; i++)
			prefilterShader->GetVSShaderBinding(prefilterShaderVS[i], 2);
		prefilterSkybox.CreateStack(128, 128, 5, RHI::Format::R16G16B16A16_FLOAT PT_DEBUG_REGION(,"Renderer -> Prefilter Texture"),RHI::TextureUsage::CopyDst);

		
		uint8_t data[4] = { 255,255,255,255 };
		whiteTexture.CreateStack(1, 1, RHI::Format::R8G8B8A8_UNORM,data PT_DEBUG_REGION(,"Renderer -> White Texture"));

		shadowMapAtlas.CreateStack(1024, 1024, 1, RHI::Format::D32_FLOAT, "Renderer -> Shadow Texture");

		computeShaders["Build Clusters"] = ComputeShader::Create({ (char*)"resources/shaders/compute/Compiled/CFBuildClusters_cs",0 }, RHI::ShaderMode::File);
		computeShaders["Filter Clusters"] = ComputeShader::Create({ (char*)"resources/shaders/compute/Compiled/CFActiveClusters_cs",0 }, RHI::ShaderMode::File);
		computeShaders["Tighten Clusters"] = ComputeShader::Create({ (char*)"resources/shaders/compute/Compiled/CFTightenList_cs",0 }, RHI::ShaderMode::File);
		computeShaders["Cull Lights"] = ComputeShader::Create({ (char*)"resources/shaders/compute/Compiled/CFCullLights_cs",0 }, RHI::ShaderMode::File);
		
		RHI::RootParameterDesc rpDesc[5];
		RHI::DescriptorRange FrameCBRange;
		Pistachio::Helpers::FillDescriptorRange(&FrameCBRange, 1, 0, RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel, RHI::DescriptorType::ConstantBuffer);
		Pistachio::Helpers::FillDynamicDescriptorRootParam(rpDesc + 0, 0, RHI::DescriptorType::ConstantBufferDynamic, RHI::ShaderStage::Vertex);
		Pistachio::Helpers::FillDescriptorSetRootParam(rpDesc + 1, 1, 1, &FrameCBRange);


		RHI::DescriptorRange RendererPsRanges[10];
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 0, 1, 0, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//brdf
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 1, 1, 1, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//prefilter
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 2, 1, 2, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//irradiance
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 3, 1, 3, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//shadow map
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 4, 1, 4, RHI::ShaderStage::Pixel, RHI::DescriptorType::StructuredBuffer);//light grid
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 5, 1, 5, RHI::ShaderStage::Pixel, RHI::DescriptorType::StructuredBuffer);//lights
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 6, 1, 6, RHI::ShaderStage::Pixel, RHI::DescriptorType::StructuredBuffer);//light index list
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 7, 1, 7, RHI::ShaderStage::Pixel, RHI::DescriptorType::Sampler);//brdf sampler
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 8, 1, 8, RHI::ShaderStage::Pixel, RHI::DescriptorType::Sampler);//shadow sampler
		Pistachio::Helpers::FillDescriptorRange(RendererPsRanges + 9, 1, 9, RHI::ShaderStage::Pixel, RHI::DescriptorType::Sampler);//texture sampler
		Pistachio::Helpers::FillDescriptorSetRootParam(rpDesc + 2, 10, 2, RendererPsRanges);

		RHI::DescriptorRange materialRanges[4];
		Pistachio::Helpers::FillDescriptorRange(materialRanges + 0, 1, 0, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//diffuse
		Pistachio::Helpers::FillDescriptorRange(materialRanges + 1, 1, 1, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//metallic
		Pistachio::Helpers::FillDescriptorRange(materialRanges + 2, 1, 2, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//roughness
		Pistachio::Helpers::FillDescriptorRange(materialRanges + 3, 1, 3, RHI::ShaderStage::Pixel, RHI::DescriptorType::SampledTexture);//normal
		Pistachio::Helpers::FillDescriptorSetRootParam(rpDesc + 3, 4, 3, materialRanges);

		Pistachio::Helpers::FillDynamicDescriptorRootParam(rpDesc + 4, 4, RHI::DescriptorType::ConstantBufferDynamic, RHI::ShaderStage::Pixel);

		RHI::RootSignatureDesc rsDesc;
		rsDesc.numRootParameters = 5;
		rsDesc.rootParameters = rpDesc;

		RHI::Ptr<RHI::RootSignature> rs;
		RHI::Ptr<RHI::DescriptorSetLayout> layouts[5]{};
		rs = RendererBase::device->CreateRootSignature(&rsDesc, layouts).value();

		Pistachio::Helpers::FillDepthStencilMode(&dsMode, true, RHI::DepthWriteMask::None);
		Pistachio::Helpers::BlendModeDisabledBlend(&blendMode);
		Pistachio::Helpers::FillRaseterizerMode(&rsMode);
		Pistachio::Helpers::ZeroAndFillShaderDesc(&ShaderDesc, 
			"resources/shaders/vertex/Compiled/VertexShader", 
			"resources/shaders/pixel/Compiled/CFPBRShader_ps", 1, 1, &dsMode,1, &blendMode,1,&rsMode);
		ShaderDesc.RTVFormats[0] = RHI::Format::R16G16B16A16_FLOAT;
		ShaderDesc.DSVFormat = RHI::Format::D32_FLOAT;
		ShaderDesc.InputDescription = Pistachio::Mesh::GetLayout();
		ShaderDesc.numInputs = Pistachio::Mesh::GetLayoutSize();

		ShaderAsset* fwdShader = new ShaderAsset();
		fwdShader->shader.CreateStackRs(&ShaderDesc, rs.Get(), layouts, 5);
		fwdShader->paramBufferSize = 12;
		fwdShader->parametersMap["Diffuse"] = ParamInfo{ 0,ParamType::Float };
		fwdShader->parametersMap["Metallic"] = ParamInfo{ 4,ParamType::Float };
		fwdShader->parametersMap["Roughness"] = ParamInfo{ 8,ParamType::Float };
		fwdShader->bindingsMap["Diffuse Texture"] = 0;
		fwdShader->bindingsMap["Metallic Texture"] = 1;
		fwdShader->bindingsMap["Roughness Texture"] = 2;
		fwdShader->bindingsMap["Normal Texture"] = 3;
		fwdShader->hold();//keep alive
		GetAssetManager()->FromResource(fwdShader, "Default Shader", Pistachio::ResourceType::Shader);

		Pistachio::Helpers::FillDescriptorRange(&FrameCBRange, 1, 0, RHI::ShaderStage::Vertex, RHI::DescriptorType::ConstantBuffer);
		Pistachio::Helpers::FillDynamicDescriptorRootParam(rpDesc + 0, 0, RHI::DescriptorType::ConstantBufferDynamic, RHI::ShaderStage::Vertex);
		Pistachio::Helpers::FillDescriptorSetRootParam(rpDesc + 1, 1, 1, &FrameCBRange);
		rsDesc.numRootParameters = 2;

		rs = RendererBase::device->CreateRootSignature(&rsDesc, layouts).value();

		//Z-Prepass
		Pistachio::Helpers::FillDepthStencilMode(&dsMode);
		Pistachio::Helpers::BlendModeDisabledBlend(&blendMode);
		Pistachio::Helpers::FillRaseterizerMode(&rsMode);
		Pistachio::Helpers::ZeroAndFillShaderDesc(&ShaderDesc,
			"resources/shaders/vertex/Compiled/StandaloneVertexShader",
			nullptr, 0, 1, &dsMode, 1, &blendMode, 1, &rsMode);
		ShaderDesc.DSVFormat = RHI::Format::D32_FLOAT;
		ShaderDesc.InputDescription = Pistachio::Mesh::GetLayout();
		ShaderDesc.numInputs = Pistachio::Mesh::GetLayoutSize();
		shaders["Z-Prepass"] = Shader::CreateWithRs(&ShaderDesc, rs.Get(), layouts, 2);

		RHI::DescriptorRange shadowRanges[1];
		Pistachio::Helpers::FillDescriptorRange(shadowRanges + 0, 1, 0, RHI::ShaderStage::Vertex, RHI::DescriptorType::StructuredBuffer);
		Pistachio::Helpers::FillDescriptorSetRootParam(rpDesc + 1, 1, 1, shadowRanges);
		rpDesc[2].type = RHI::RootParameterType::PushConstant;
		rpDesc[2].pushConstant.bindingIndex = 1;
		rpDesc[2].pushConstant.numConstants = 2;
		rpDesc[2].pushConstant.offset = 0;
		rpDesc[2].pushConstant.stage = RHI::ShaderStage::Vertex;
		rsDesc.numRootParameters = 3;
		rs = RendererBase::device->CreateRootSignature(&rsDesc, layouts).value();
		//shadow shaders
		ShaderDesc.VS = RHI::ShaderCode{ (char*)"resources/shaders/vertex/Compiled/Shadow_vs",0 };
		ShaderDesc.RasterizerModes->cullMode = RHI::CullMode::Front;
		shaders["Shadow Shader"] = Shader::CreateWithRs(&ShaderDesc, rs.Get(), layouts, 2);


		BrdfTex.CreateStack(512, 512, RHI::Format::R16G16_FLOAT, nullptr PT_DEBUG_REGION(,"Renderer -> White Texture"),TextureFlags::Compute);
		ComputeShader* brdfShader = ComputeShader::Create({ (char*)"resources/shaders/compute/Compiled/BRDF_LUT_cs",0 },RHI::ShaderMode::File);

		ShaderDesc.DepthStencilModes->DepthWriteMask = RHI::DepthWriteMask::None;
		ShaderDesc.RasterizerModes->cullMode = RHI::CullMode::None;
		ShaderDesc.VS = {(char*)"resources/shaders/vertex/Compiled/background_vs", 0};
		ShaderDesc.PS = {(char*)"resources/shaders/pixel/Compiled/background_ps", 0};
		ShaderDesc.NumRenderTargets = 1;
		ShaderDesc.RTVFormats[0] = RHI::Format::R16G16B16A16_FLOAT;
		auto background_shader = Shader::Create(&ShaderDesc);
		shaders["Background Shader"] = background_shader;
		
		background_shader->GetPSShaderBinding(backgroundInfo, 1);
		backgroundInfo.UpdateTextureBinding(skybox.GetView(), 0);
		backgroundInfo.UpdateSamplerBinding(defaultSampler, 1);

		SetInfo brdfTexInfo;
		brdfShader->GetShaderBinding(brdfTexInfo, 0);
		brdfTexInfo.UpdateTextureBinding(BrdfTex.GetView(), 0, RHI::DescriptorType::CSTexture);
		RHI::TextureMemoryBarrier barr;
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::SHADER_WRITE;barr.AccessFlagsBefore = RHI::ResourceAcessFlags::NONE;
		barr.newLayout = RHI::ResourceLayout::GENERAL;
		barr.oldLayout = RHI::ResourceLayout::UNDEFINED;
		barr.previousQueue = barr.nextQueue = RHI::QueueFamily::Ignored;
		barr.texture = BrdfTex.m_ID.Get();
		RHI::SubResourceRange range;
		range.FirstArraySlice = 0;
		range.NumArraySlices = 1;
		range.IndexOrFirstMipLevel = 0;
		range.NumMipLevels = 1;
		range.imageAspect = RHI::Aspect::COLOR_BIT;
		barr.subresourceRange = range;
		RendererBase::mainCommandList->PipelineBarrier(RHI::PipelineStage::TOP_OF_PIPE_BIT, RHI::PipelineStage::COMPUTE_SHADER_BIT, 0, 0, 1, &barr);
		brdfShader->Bind(RendererBase::mainCommandList.Get());
		brdfShader->ApplyShaderBinding(RendererBase::mainCommandList.Get(), brdfTexInfo);
		RendererBase::mainCommandList->Dispatch(512,512,1);
		barr.oldLayout = RHI::ResourceLayout::GENERAL;
		barr.newLayout = RHI::ResourceLayout::SHADER_READ_ONLY_OPTIMAL;
		barr.AccessFlagsBefore = RHI::ResourceAcessFlags::SHADER_WRITE;
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::SHADER_READ;
		RendererBase::mainCommandList->PipelineBarrier(RHI::PipelineStage::COMPUTE_SHADER_BIT, RHI::PipelineStage::FRAGMENT_SHADER_BIT, 0, 0, 1, &barr);
		//Replace with mesh generation engine
		cube.CreateStack("cube.obj");
		ChangeSkybox(skyboxFile);
		EndScene();
	}
	void Renderer::ChangeRGTexture(RGTextureHandle& texture, RHI::ResourceLayout newLayout, RHI::ResourceAcessFlags newAccess,RHI::QueueFamily newFamily)
	{
		texture.originVector->at(texture.offset).current_layout = newLayout;
		texture.originVector->at(texture.offset).currentAccess = newAccess;
		texture.originVector->at(texture.offset).currentFamily = newFamily;
	}
	void Renderer::ChangeSkybox(const char* filename)
	{
		//the skybox texture
		RenderGraph skyboxRG;
		static Texture2D tex;
		tex.CreateStack(filename, RHI::Format::R32G32B32A32_FLOAT PT_DEBUG_REGION(,"Renderer -> Skybox Texture"));

		RendererBase::FlushStagingBuffer();
		


		//probably remove this
		struct CameraStruct {
			DirectX::XMMATRIX viewproj;
			DirectX::XMMATRIX transform;
			float roughness;
		} camerabufferData;
		static ConstantBuffer CameraCB;
		static ConstantBuffer PrefilterCB;
		PrefilterCB.CreateStack(nullptr, 256 * 5);
		CameraCB.CreateStack(nullptr, 256*6);//add padding
		
		DirectX::XMMATRIX captureProjection = DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(90.0f), 1.0f, 0.1f, 10.0f);
		DirectX::XMMATRIX captureViews[] =
		{
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(1.0f,  0.0f,  0.0f, 1.0f), DirectX::XMVectorSet(0.0f, -1.0f,  0.0f, 1.0f)),
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(-1.0f,  0.0f,  0.0f, 1.0f), DirectX::XMVectorSet(0.0f, -1.0f,  0.0f, 1.0f)),
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(0.0f, -1.0f,  0.0f, 1.0f), DirectX::XMVectorSet(0.0f,  0.0f,  1.0f, 1.0f)),
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(0.0f,  1.0f,  0.0f, 1.0f), DirectX::XMVectorSet(0.0f,  0.0f, -1.0f, 1.0f)),
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(0.0f,  0.0f, -1.0f, 1.0f), DirectX::XMVectorSet(0.0f, -1.0f,  0.0f, 1.0f)),
			DirectX::XMMatrixLookAtLH(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), DirectX::XMVectorSet(0.0f,  0.0f,  1.0f, 1.0f), DirectX::XMVectorSet(0.0f, -1.0f,  0.0f, 1.0f))
		};
		for (uint32_t i = 0; i < 6; i++)
		{
			camerabufferData = { DirectX::XMMatrixMultiplyTranspose(captureViews[i], captureProjection), DirectX::XMMatrixIdentity(), 0 };
			CameraCB.Update(&camerabufferData, sizeof(camerabufferData), 256 * i);
			BufferBindingUpdateDesc bufferDesc;
			bufferDesc.buffer = CameraCB.GetID();
			bufferDesc.offset = 256 * i;
			bufferDesc.size = sizeof(Matrix4) * 2;
			bufferDesc.type = RHI::DescriptorType::ConstantBuffer;
			eqShaderVS[i].UpdateBufferBinding(&bufferDesc, 0);
		}
		for (uint32_t i = 0; i < 5; i++)
		{
			float roughness = (float)i / 4.f;
			PrefilterCB.Update(&roughness,sizeof(float),256*i);
			BufferBindingUpdateDesc bufferDesc;
			bufferDesc.buffer = PrefilterCB.GetID();
			bufferDesc.offset = 256 * i;
			bufferDesc.size = sizeof(float);
			bufferDesc.type = RHI::DescriptorType::ConstantBuffer;
			prefilterShaderVS[i].UpdateBufferBinding(&bufferDesc,0);
		}
		RendererBase::ChangeViewport(512, 512);
		//eqShader->Bind();
		eqShaderPS.UpdateTextureBinding(tex.GetView(), 0);
		eqShaderPS.UpdateSamplerBinding(defaultSampler, 1);
		irradianceShaderPS.UpdateTextureBinding(skybox.GetView(), 0);
		irradianceShaderPS.UpdateSamplerBinding(defaultSampler, 1);

		Pistachio::RGTextureHandle skyboxFaces_pre[6];
		Pistachio::RGTextureInstance skyboxFaces[6];
		RGTextureHandle skybox_lv0[6];
		Pistachio::RGTextureHandle irradianceSkyboxFaces[6];
		for (int i = 0; i < 6; i++)
		{
			skybox_lv0[i] = skyboxRG.CreateTexture(skybox.m_ID.Get(), 0, true, i);
			skyboxFaces_pre[i] = skyboxRG.CreateTexture(&skybox, i);
			irradianceSkyboxFaces[i] = skyboxRG.CreateTexture(&irradianceSkybox, i);
			AttachmentInfo info;
			info.loadOp = RHI::LoadOp::Load;
			info.format = RHI::Format::R16G16B16A16_FLOAT;
			info.texture = skyboxFaces_pre[i];
			auto& eqpass = skyboxRG.AddPass(RHI::PipelineStage::ALL_GRAPHICS_BIT, "Equirectangular to cubemap");
			eqpass.AddColorOutput(&info);
			info.texture = skybox_lv0[i];
			info.usage = AttachmentUsage::Unspec;
			eqpass.AddColorOutput(&info);
			eqpass.SetPassArea({{0,0},{512,512}});
			eqpass.SetShader(eqShader);
			eqpass.pass_fn = [&,i](RHI::GraphicsCommandList* list)
				{
					RHI::Viewport vp;
					vp.height = vp.width = 512;
					vp.minDepth = 0;
					vp.maxDepth = 1;
					vp.x = vp.y = 0;
					list->SetViewports(1, &vp);
					RHI::Area2D rect = { {0,0},{512,512} };
					list->SetScissorRects(1, &rect);
					list->BindVertexBuffers(0, 1, &meshVertices->ID);
					list->BindIndexBuffer(meshIndices.Get(), 0);
					eqShader->ApplyBinding(list, eqShaderVS[i]);
					eqShader->ApplyBinding(list, eqShaderPS);
					Submit(list, cube.GetVBHandle(), cube.GetIBHandle(), sizeof(Vertex));
					auto layout = skyboxFaces_pre[i].originVector->at(skyboxFaces_pre[i].offset).current_layout;
					auto access = skyboxFaces_pre[i].originVector->at(skyboxFaces_pre[i].offset).currentAccess;
					auto family = skyboxFaces_pre[i].originVector->at(skyboxFaces_pre[i].offset).currentFamily;
					Renderer::ChangeRGTexture(skybox_lv0[i], layout, access, family);
				};
			
		}
		for(uint32_t i = 0; i < 6; i++)
		{
			
			skyboxFaces[i] = skyboxRG.MakeUniqueInstance(skyboxFaces_pre[i]);
			for(uint32_t j = 1; j < NUM_SKYBOX_MIPS; j++)
			{
				auto& pass = skyboxRG.AddPass(RHI::PipelineStage::TRANSFER_BIT, "Gen MipMaps");
				RGTextureHandle dst = skyboxRG.CreateTexture(skybox.m_ID.Get(), j, true, i);
				AttachmentInfo info;
				info.format = RHI::Format::R16G16B16A16_FLOAT;
				info.loadOp = RHI::LoadOp::Load;

				info.texture = skybox_lv0[i];
				info.usage = AttachmentUsage::Blit;
				pass.AddColorInput(&info);

				info.texture = dst;
				pass.AddColorOutput(&info);

				info.usage = AttachmentUsage::PassThrough;
				info.texture = skyboxFaces[i];
				pass.AddColorOutput(&info);
				pass.pass_fn = [&,j,i](RHI::GraphicsCommandList* list)
				{
					uint32_t mipSize = std::pow(0.5,j) * 512;
					RHI::SubResourceRange src;
					src.FirstArraySlice = i;
					src.NumArraySlices = 1;
					src.imageAspect = RHI::Aspect::COLOR_BIT;
					src.IndexOrFirstMipLevel = 0;
					src.NumMipLevels = 1;
					RHI::SubResourceRange dst = src;
					dst.IndexOrFirstMipLevel = j;
					list->BlitTexture(skybox.m_ID.Get(), skybox.m_ID.Get(), {512,512,1}, {0,0,0}, {mipSize, mipSize,1}, {0,0,0},src,dst);
					RHI::TextureMemoryBarrier barr[2];
					barr[0].texture = skybox.m_ID.Get();
					barr[0].AccessFlagsBefore = RHI::ResourceAcessFlags::TRANSFER_READ;
					barr[0].AccessFlagsAfter = RHI::ResourceAcessFlags::SHADER_READ;
					barr[0].subresourceRange = src;
					barr[0].newLayout = RHI::ResourceLayout::SHADER_READ_ONLY_OPTIMAL;
					barr[0].oldLayout = RHI::ResourceLayout::GENERAL;
					barr[0].nextQueue = barr[0].previousQueue = RHI::QueueFamily::Ignored;
					barr[1] = barr[0];
					barr[1].subresourceRange = dst;
					barr[1].AccessFlagsBefore = RHI::ResourceAcessFlags::TRANSFER_WRITE;
					barr[1].oldLayout = RHI::ResourceLayout::TRANSFER_DST_OPTIMAL;
					list->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT, RHI::PipelineStage::ALL_GRAPHICS_BIT, 0, 0, 2, barr);
					Renderer::ChangeRGTexture(skyboxFaces_pre[i], RHI::ResourceLayout::SHADER_READ_ONLY_OPTIMAL, RHI::ResourceAcessFlags::SHADER_READ, RHI::QueueFamily::Graphics);
				};
			}
		}
		for (uint32_t i = 0; i < 6; i++)
		{
			auto& irradiancePass = skyboxRG.AddPass(RHI::PipelineStage::ALL_GRAPHICS_BIT, "Irradiance Filter");
			AttachmentInfo info;
			info.loadOp = RHI::LoadOp::Load;
			info.format = RHI::Format::R16G16B16A16_FLOAT;
			for (uint32_t j = 0; j < 6; j++)
			{
				info.texture = skyboxFaces[j];
				irradiancePass.AddColorInput(&info);//inject pass dependency
			}
			irradiancePass.SetPassArea({ {0,0},{32,32} });
			irradiancePass.SetShader(irradianceShader);
			AttachmentInfo irrInfo;
			irrInfo.loadOp = RHI::LoadOp::Load;
			irrInfo.format = RHI::Format::R16G16B16A16_FLOAT;
			irrInfo.texture = irradianceSkyboxFaces[i];
			irradiancePass.AddColorOutput(&irrInfo);
			irradiancePass.pass_fn = [&, i](RHI::GraphicsCommandList* list)
				{
					RHI::Viewport vp;
					vp.height = vp.width = 32;
					vp.minDepth = 0;
					vp.maxDepth = 1;
					vp.x = vp.y = 0;
					list->SetViewports(1, &vp);
					RHI::Area2D rect = { {0,0},{32,32} };
					list->SetScissorRects(1, &rect);
					list->BindVertexBuffers(0, 1, &meshVertices->ID);
					list->BindIndexBuffer(meshIndices.Get(), 0);
					irradianceShader->ApplyBinding(list, eqShaderVS[i]);
					irradianceShader->ApplyBinding(list, irradianceShaderPS);
					Submit(list, cube.GetVBHandle(), cube.GetIBHandle(), sizeof(Vertex));
				};
		}
		//todo
		static RenderCubeMap prefilterMipLevels[5];
		RGTextureHandle prefilterRGTextures[5*6];
		RGTextureHandle prefilterDstTexture;
		prefilterDstTexture = skyboxRG.CreateTexture((Texture*)&prefilterSkybox,0, true, 0,6,5);
		const uint32_t mipLevels = 5;
		uint32_t mipFaceIndex =0;
		for (uint32_t mip = 0; mip < mipLevels; mip++)
		{
			uint32_t mipWidth  = (int)(128 * std::pow(0.5, mip));
			uint32_t mipHeight = (int)(128 * std::pow(0.5, mip));
			//create the cubemap
			prefilterMipLevels[mip].CreateStack(mipWidth, mipHeight, 1, RHI::Format::R16G16B16A16_FLOAT PT_DEBUG_REGION(,"Renderer -> Prefilter Mip") ,RHI::TextureUsage::CopySrc);
			//for each face
			for (uint32_t i = 0; i < 6; i++)
			{
				prefilterRGTextures [mipFaceIndex] = skyboxRG.CreateTexture(&prefilterMipLevels[mip], i);
				auto& prefilterPass = skyboxRG.AddPass(RHI::PipelineStage::ALL_GRAPHICS_BIT, "Render To Texture");
				for (uint32_t j = 0; j < 6; j++)
				{
					AttachmentInfo info{};
					info.format = RHI::Format::R16G16B16A16_FLOAT;
					info.texture = skyboxFaces[j];
					prefilterPass.AddColorInput(&info);
				}
				AttachmentInfo info{};
				info.format = RHI::Format::R16G16B16A16_FLOAT;
				info.texture = prefilterRGTextures[mipFaceIndex];
				prefilterPass.AddColorOutput(&info);
				prefilterPass.SetPassArea({ {0,0},{mipWidth,mipHeight} });
				prefilterPass.SetShader(prefilterShader);
				mipFaceIndex++;
				prefilterPass.pass_fn = [&, i, mip,mipWidth,mipHeight](RHI::GraphicsCommandList* list)
					{
						RHI::Viewport vp;
						vp.height = vp.width = (float)mipWidth;
						vp.minDepth = 0;
						vp.maxDepth = 1;
						vp.x = vp.y = 0;
						list->SetViewports(1, &vp);
						RHI::Area2D rect = { {0,0},{mipWidth,mipHeight} };
						list->SetScissorRects(1, &rect);
						list->BindVertexBuffers(0, 1, &meshVertices->ID);
						list->BindIndexBuffer(meshIndices.Get(), 0);
						prefilterShader->ApplyBinding(list, eqShaderVS[i]);
						prefilterShader->ApplyBinding(list, prefilterShaderVS[mip]);
						prefilterShader->ApplyBinding(list, irradianceShaderPS);
						Submit(list, cube.GetVBHandle(), cube.GetIBHandle(), sizeof(Vertex));
					};
			}
		}
		//after rendering, we copy
		for (uint32_t mip = 0; mip < 5; mip++)
		{
			//we can copy the entire cupe map?
			auto& copyPass = skyboxRG.AddPass(RHI::PipelineStage::TRANSFER_BIT, "Prefilter Copy");
			for (uint32_t j = 0; j < 5 * 6; j++)
			{
				AttachmentInfo input;
				input.format = RHI::Format::R16G16B16A16_FLOAT;
				input.loadOp = RHI::LoadOp::Load;
				input.texture = prefilterRGTextures[j];
				input.usage = AttachmentUsage::Copy;
				copyPass.AddColorInput(&input);
			}
			AttachmentInfo output;
			output.format = RHI::Format::R16G16B16A16_FLOAT;
			output.loadOp = RHI::LoadOp::Load;
			output.texture = prefilterDstTexture;
			output.usage = AttachmentUsage::Copy;
			copyPass.AddColorOutput(&output);
			copyPass.pass_fn = [&,mip](RHI::GraphicsCommandList* list)
				{
					uint32_t mipSize = (int)(128 * std::pow(0.5, mip));
					RHI::SubResourceRange srcRange;
					srcRange.FirstArraySlice = 0;
					srcRange.imageAspect = RHI::Aspect::COLOR_BIT;
					srcRange.IndexOrFirstMipLevel = 0;
					srcRange.NumArraySlices = 6;
					srcRange.NumMipLevels = 1;
					RHI::SubResourceRange dstRange;
					dstRange.FirstArraySlice = 0;
					dstRange.imageAspect = RHI::Aspect::COLOR_BIT;
					dstRange.IndexOrFirstMipLevel = mip;
					dstRange.NumArraySlices = 6;
					dstRange.NumMipLevels = 1;
					
					list->CopyTextureRegion(srcRange, dstRange, { 0,0,0 }, { 0,0,0 }, { mipSize,mipSize,1 }, prefilterMipLevels[mip].m_ID.Get(), prefilterSkybox.m_ID.Get());
				};
		}
		RHI::SubResourceRange Range;
		Range.FirstArraySlice = 0;
		Range.imageAspect = RHI::Aspect::COLOR_BIT;
		Range.IndexOrFirstMipLevel = 0;
		Range.NumArraySlices = 6;
		Range.NumMipLevels = 1;
		RHI::TextureMemoryBarrier barr[2];
		barr[0].AccessFlagsBefore = RHI::ResourceAcessFlags::SHADER_WRITE;
		barr[0].AccessFlagsAfter = RHI::ResourceAcessFlags::SHADER_READ;
		barr[0].newLayout = RHI::ResourceLayout::SHADER_READ_ONLY_OPTIMAL;
		barr[0].oldLayout = RHI::ResourceLayout::UNDEFINED;
		barr[0].texture = irradianceSkybox.m_ID.Get();
		barr[0].subresourceRange = barr[1].subresourceRange = Range;
		barr[1].AccessFlagsBefore = RHI::ResourceAcessFlags::TRANSFER_WRITE;
		barr[1].AccessFlagsAfter = RHI::ResourceAcessFlags::SHADER_READ;
		barr[1].newLayout = RHI::ResourceLayout::SHADER_READ_ONLY_OPTIMAL;
		barr[1].oldLayout = RHI::ResourceLayout::TRANSFER_DST_OPTIMAL;
		barr[1].texture = prefilterSkybox.m_ID.Get();
		barr[1].subresourceRange.NumMipLevels = 5;
		

		skyboxRG.Execute();
		skyboxRG.cmdLists[0].list->PipelineBarrier(RHI::PipelineStage::ALL_GRAPHICS_BIT, RHI::PipelineStage::FRAGMENT_SHADER_BIT, 0, 0, 1, barr);
		skyboxRG.cmdLists[0].list->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT,     RHI::PipelineStage::FRAGMENT_SHADER_BIT,0,0,1,barr+1);
		skyboxRG.SubmitToQueue();
	}
	void Renderer::EndScene()
	{
		PT_PROFILE_FUNCTION()
		RendererBase::EndFrame();
	}
	void Renderer::Shutdown() {
		
		RendererBase::Shutdown();
	}
	const RendererVBHandle Renderer::AllocateVertexBuffer(uint32_t size,const void* initialData)
	{
		return AllocateBuffer(vbFreeList, vbFreeSpace, vbFreeFastSpace, vbCapacity, &Renderer::GrowMeshVertexBuffer,&Renderer::DefragmentMeshVertexBuffer, vbHandleOffsets,vbUnusedHandles,&meshVertices, size, initialData);
	}
	const RendererIBHandle Renderer::AllocateIndexBuffer(uint32_t size, const void* initialData)
	{
		auto [a,b] = AllocateBuffer(ibFreeList, ibFreeSpace, ibFreeFastSpace, ibCapacity, &Renderer::GrowMeshIndexBuffer, &Renderer::DefragmentMeshIndexBuffer,ibHandleOffsets,ibUnusedHandles,&meshIndices, size, initialData);
		return { a,b };
	}
	ComputeShader* Renderer::GetBuiltinComputeShader(const std::string& name)
	{
		if (auto it = computeShaders.find(name); it != computeShaders.end())
		{
			return it->second;
		}
		return nullptr;
	}
	Shader* Renderer::GetBuiltinShader(const std::string& name)
	{
		if (auto it = shaders.find(name); it != shaders.end())
		{
			return it->second;
		}
		return nullptr;
	}
	const RendererCBHandle Renderer::AllocateConstantBuffer(uint32_t size)
	{
		auto [a,b] = AllocateBuffer(cbFreeList, cbFreeSpace, cbFreeFastSpace, cbCapacity, &Renderer::GrowConstantBuffer, &Renderer::DefragmentConstantBuffer, cbHandleOffsets, cbUnusedHandles, nullptr, RendererUtils::ConstantBufferElementSize(size), nullptr);
		return { a,b };
	}
	RenderCubeMap& Pistachio::Renderer::GetSkybox()
	{
		return skybox;
	}
	inline RendererVBHandle Renderer::AllocateBuffer(
		FreeList& flist, uint32_t& free_space, 
		uint32_t& fast_space, 
		uint32_t& capacity,
		decltype(&Renderer::GrowMeshVertexBuffer) grow_fn,
		decltype(&Renderer::DefragmentMeshVertexBuffer) defrag_fn,
		std::vector<uint32_t>& offsetsVector,
		std::vector<uint32_t>& freeHandlesVector,
		RHI::Buffer** buffer, 
		uint32_t size,
		const void* initialData)
	{
		RendererVBHandle handle;
		//check if we have immediate space available
		if (size < fast_space)
		{
			//allocate to buffer end
			//fast space is always at the end
			PT_CORE_ASSERT(flist.Allocate(capacity - fast_space, size) == 0);
			if (initialData)
			{
				RendererBase::PushBufferUpdate(*buffer, capacity - fast_space, initialData, size);
				RendererBase::FlushStagingBuffer();
			}
			handle.handle = AssignHandle(offsetsVector,freeHandlesVector,capacity - fast_space);
			handle.size = size;
			fast_space -= size;
			free_space -= size;
			return handle;
		}
		//if not, check if we have space at all
		else if (size < free_space)
		{
			//if we have space, check the free list to see if space is continuos
			if (auto space = flist.Find(size); space != UINT32_MAX)
			{
				//allocate
				PT_CORE_ASSERT(flist.Allocate(space, size) == 0);
				if (initialData)
				{
					RendererBase::PushBufferUpdate(*buffer, space, initialData, size);
					RendererBase::FlushStagingBuffer();
				}
				handle.handle = AssignHandle(offsetsVector, freeHandlesVector, space);
				handle.size = size;
				free_space -= size;
				return handle;
			}
			PT_CORE_WARN("Defragmenting Buffer");
			defrag_fn();
			if (initialData)
			{
				RendererBase::PushBufferUpdate(*buffer, capacity - fast_space, initialData, size);
				RendererBase::FlushStagingBuffer();
			}
			handle.handle = AssignHandle(offsetsVector,freeHandlesVector,capacity - fast_space);
			handle.size = size;
			fast_space -= size;
			free_space -= size;
			return handle;
			//allocate to buffer end
		}
		else
		{
			PT_CORE_WARN("Growing Buffer");
			grow_fn(size);
			//growth doesnt guarantee that the free space is "Fast" it just guarantees we'll have enough space for the op
			return AllocateBuffer(flist, free_space, fast_space, capacity,grow_fn,defrag_fn,offsetsVector,freeHandlesVector,buffer,size, initialData);
			//allocate to buffer end
		}
		return handle;
	}
	uint32_t Renderer::AssignHandle(std::vector<uint32_t>& offsetsVector, std::vector<uint32_t>& freeHandlesVector, std::uint32_t offset)
	{
		if (freeHandlesVector.empty())
		{
			offsetsVector.push_back(offset);
			return(uint32_t)( offsetsVector.size() - 1);
		}
		uint32_t handle = freeHandlesVector[freeHandlesVector.size()-1];
		offsetsVector[handle] = offset;
		freeHandlesVector.pop_back();
		return (uint32_t)handle;
	}
	void Renderer::GrowMeshVertexBuffer(uint32_t minSize)
	{
		const uint32_t GROW_FACTOR = 20; //probably use a better, more size dependent method to determing this
		uint32_t new_size = vbCapacity + minSize + GROW_FACTOR;
		RHI::BufferDesc desc;
		desc.size = new_size;
		desc.usage = RHI::BufferUsage::VertexBuffer | RHI::BufferUsage::CopyDst | RHI::BufferUsage::CopySrc;
		RHI::AutomaticAllocationInfo allocInfo;
		allocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::None;
		RHI::Ptr<RHI::Buffer> newVB = RendererBase::device->CreateBuffer(&desc, 0, 0, &allocInfo, 0, RHI::ResourceType::Automatic).value();
		RHI::BufferMemoryBarrier barr;
		barr.AccessFlagsBefore = RHI::ResourceAcessFlags::TRANSFER_WRITE;
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::TRANSFER_READ;
		barr.buffer = meshVertices;
		barr.nextQueue = barr.previousQueue = RHI::QueueFamily::Ignored;
		barr.size = vbCapacity;
		barr.offset = 0;
		RendererBase::stagingCommandList->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT, RHI::PipelineStage::TRANSFER_BIT, 1,&barr,0,0);
		//Queue it with staging stuff
		RendererBase::stagingCommandList->CopyBufferRegion(0, 0, vbCapacity, meshVertices, newVB);
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::TRANSFER_WRITE;
		barr.buffer = newVB;
		RendererBase::stagingCommandList->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT, RHI::PipelineStage::TRANSFER_BIT, 1,&barr,0,0);
		//wait until copy is finished?
		RendererBase::FlushStagingBuffer();
		//before destroying old buffer, wait for old frames to render
		RendererBase::mainFence->Wait(RendererBase::currentFenceVal);
		meshVertices->Release();
		meshVertices = newVB;
		vbFreeSpace += minSize + GROW_FACTOR;
		vbFreeFastSpace += minSize + GROW_FACTOR;
		vbCapacity = new_size;
		vbFreeList.Grow(new_size);
	}
	void Renderer::GrowMeshIndexBuffer(uint32_t minSize)
	{
		const uint32_t GROW_FACTOR = 20; //probably use a better, more size dependent method to determing this
		uint32_t new_size = ibCapacity + minSize + GROW_FACTOR;
		RHI::BufferDesc desc;
		desc.size = new_size;
		desc.usage = RHI::BufferUsage::IndexBuffer | RHI::BufferUsage::CopyDst | RHI::BufferUsage::CopySrc;
		
		RHI::AutomaticAllocationInfo allocInfo;
		allocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::None;
		RHI::Ptr<RHI::Buffer> newIB = RendererBase::device->CreateBuffer(&desc, 0, 0, &allocInfo, 0, RHI::ResourceType::Automatic).value();
		//Queue it with staging stuff
		RHI::BufferMemoryBarrier barr;
		barr.AccessFlagsBefore = RHI::ResourceAcessFlags::TRANSFER_WRITE;
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::TRANSFER_READ;
		barr.buffer = meshIndices;
		barr.nextQueue = barr.previousQueue = RHI::QueueFamily::Ignored;
		barr.size = ibCapacity;
		barr.offset = 0;
		RendererBase::stagingCommandList->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT, RHI::PipelineStage::TRANSFER_BIT, 1,&barr,0,0);
		RendererBase::stagingCommandList->CopyBufferRegion(0, 0, ibCapacity, meshIndices, newIB);
		barr.AccessFlagsAfter = RHI::ResourceAcessFlags::TRANSFER_WRITE;
		barr.buffer = newIB;
		RendererBase::stagingCommandList->PipelineBarrier(RHI::PipelineStage::TRANSFER_BIT, RHI::PipelineStage::TRANSFER_BIT, 1,&barr,0,0);
		//wait until copy is finished?
		RendererBase::FlushStagingBuffer();
		//before destroying old buffer, wait for old frames to render
		RendererBase::mainFence->Wait(RendererBase::currentFenceVal);
		meshIndices->Release();
		meshIndices = newIB;
		ibFreeSpace += minSize + GROW_FACTOR;
		ibFreeFastSpace += minSize + GROW_FACTOR;
		ibCapacity = new_size;
		ibFreeList.Grow(new_size);
	}
	void Pistachio::Renderer::GrowConstantBuffer(uint32_t minExtraSize)
	{
		const uint32_t GROW_FACTOR = 20; //probably use a better, more size dependent method to determing this
		uint32_t new_size = cbCapacity + minExtraSize + GROW_FACTOR;
		RHI::BufferDesc desc;
		desc.size = new_size;
		desc.usage = RHI::BufferUsage::ConstantBuffer;
		RendererBase::mainFence->Wait(RendererBase::currentFenceVal);
		for (uint32_t i = 0; i < 3; i++)
		{
			
			RHI::AutomaticAllocationInfo allocInfo;
			allocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::Sequential;
			RHI::Ptr<RHI::Buffer> newCB = RendererBase::device->CreateBuffer(&desc, 0, 0, &allocInfo, 0, RHI::ResourceType::Automatic).value();
			void* writePtr;
			void* readPtr;
			newCB->Map(&writePtr);
			resources[i].transformBuffer.ID->Map(&readPtr);
			memcpy(writePtr, readPtr, cbCapacity);
			resources[i].transformBuffer.ID->UnMap();
			newCB->UnMap();
			resources[i].transformBuffer.ID = newCB;
		}
	}
	void Pistachio::Renderer::FreeVertexBuffer(const RendererVBHandle handle)
	{
		vbFreeSpace += handle.size;
		vbFreeList.DeAllocate(vbHandleOffsets[handle.handle], handle.size);
		vbUnusedHandles.push_back(handle.handle);
	}
	void Renderer::FreeIndexBuffer(const RendererIBHandle handle)
	{
		ibFreeSpace += handle.size;
		ibFreeList.DeAllocate(ibHandleOffsets[handle.handle], handle.size);
		ibUnusedHandles.push_back(handle.handle);
	}
	RHI::Buffer* Renderer::GetVertexBuffer()
	{
		return meshVertices;
	}
	RHI::Buffer* Renderer::GetIndexBuffer()
	{
		return meshIndices;
	}
	void Renderer::DefragmentMeshVertexBuffer()
	{
		auto block = vbFreeList.GetBlockPtr();
		uint32_t nextFreeOffset = 0;
		while (block)
		{
			if (block->offset > nextFreeOffset)
			{
				RendererBase::stagingCommandList->CopyBufferRegion(block->offset, nextFreeOffset, block->size, meshVertices, meshVertices);
				nextFreeOffset += block->size;
			}
			block = block->next;
		}
		vbFreeList.Reset();
		//fill up the beginning of the free list with the memory size we just copied
		vbFreeList.Allocate(0, nextFreeOffset);
		vbFreeFastSpace = vbFreeSpace;//after defrag all free space is fast space
		/*
		 *Is that a safe assumptions;
		 *we dont flush for every copy, because we asssume copies are done in order, so memory won't get overritten
		 */
		RendererBase::FlushStagingBuffer();
		
	}
	void Renderer::DefragmentMeshIndexBuffer()
	{
		auto block = ibFreeList.GetBlockPtr();
		uint32_t nextFreeOffset = 0;
		while (block)
		{
			if (block->offset > nextFreeOffset)
			{
				RendererBase::stagingCommandList->CopyBufferRegion(block->offset, nextFreeOffset, block->size, meshIndices, meshIndices);
				nextFreeOffset += block->size;
			}
			block = block->next;
		}
		ibFreeList.Reset();
		//fill up the beginning of the free list with the memory size we just copied
		ibFreeList.Allocate(0, nextFreeOffset);
		ibFreeFastSpace = vbFreeSpace;//after defrag all free space is fast space
		/*
		 *Is that a safe assumptions;
		 *we dont flush for every copy, because we asssume copies are done in order, so memory won't get overritten
		 */
		RendererBase::FlushStagingBuffer();

	}
	void Renderer::DefragmentConstantBuffer()
	{
		RendererBase::mainFence->Wait(RendererBase::currentFenceVal);
		auto block = cbFreeList.GetBlockPtr();
		uint32_t nextFreeOffset = 0;
		void *ptr1, *ptr2, *ptr3;
		resources[0].transformBuffer.ID->Map(&ptr1);
		resources[1].transformBuffer.ID->Map(&ptr2);
		resources[2].transformBuffer.ID->Map(&ptr3);
		while (block)
		{
			//if block has gap from last block
			if (block->offset > nextFreeOffset)
			{
				//we use memmove to handle overlap possibility
				memmove((uint8_t*)ptr1 + nextFreeOffset, (uint8_t*)ptr1 + block->offset, block->size);
				memmove((uint8_t*)ptr2 + nextFreeOffset, (uint8_t*)ptr2 + block->offset, block->size);
				memmove((uint8_t*)ptr3 + nextFreeOffset, (uint8_t*)ptr3 + block->offset, block->size);
				nextFreeOffset += block->size;
			}
			block = block->next;
		}
		resources[0].transformBuffer.ID->UnMap();
		resources[1].transformBuffer.ID->UnMap();
		resources[2].transformBuffer.ID->UnMap();
		if(CBInvalidated)
			CBInvalidated(); // the scene would bind a function here to update all descriptor sets
	}
	const uint32_t Pistachio::Renderer::GetIBOffset(const RendererIBHandle handle)
	{
		return ibHandleOffsets[handle.handle];
	}
	const uint32_t Pistachio::Renderer::GetVBOffset(const RendererVBHandle handle)
	{
		return vbHandleOffsets[handle.handle];
	}
	void Pistachio::Renderer::PartialCBUpdate(RendererCBHandle handle, void* data, uint32_t offset, uint32_t size)
	{
		PT_CORE_ASSERT(offset+size <= handle.size);
		resources[RendererBase::currentFrameIndex].transformBuffer.Update(data, size, cbHandleOffsets[handle.handle] + offset);
	}
	void Pistachio::Renderer::FullCBUpdate(RendererCBHandle handle, void* data)
	{
		resources[RendererBase::currentFrameIndex].transformBuffer.Update(data, handle.size, cbHandleOffsets[handle.handle]);
	}
	RHI::Buffer* Pistachio::Renderer::GetConstantBuffer()
	{
		return resources[RendererBase::currentFrameIndex].transformBuffer.ID.Get();
	}
	const RHI::DynamicDescriptor* Pistachio::Renderer::GetCBDesc()
	{
		return resources[RendererBase::currentFrameIndex].transformBufferDesc;
	}
	const RHI::DynamicDescriptor* Renderer::GetCBDescPS()
	{
		return resources[RendererBase::currentFrameIndex].transformBufferDescPS;
	}
	const uint32_t Pistachio::Renderer::GetCBOffset(const RendererCBHandle handle)
	{
		return cbHandleOffsets[handle.handle];
	}
	void Pistachio::Renderer::Submit(RHI::GraphicsCommandList* list,const RendererVBHandle vb, const RendererIBHandle ib, uint32_t vertexStride)
	{
		list->DrawIndexed(ib.size / sizeof(uint32_t),
			1,
			ibHandleOffsets[ib.handle] / sizeof(uint32_t),
			vbHandleOffsets[vb.handle] / vertexStride, 0);
	}
	const Texture2D& Pistachio::Renderer::GetWhiteTexture()
	{
		return whiteTexture;
	}
	SamplerHandle Pistachio::Renderer::GetDefaultSampler()
	{
		return defaultSampler;
	}
}