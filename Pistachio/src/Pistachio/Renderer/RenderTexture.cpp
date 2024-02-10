#include "ptpch.h"
#include "RendererBase.h"
#include "RenderTexture.h"

namespace Pistachio
{
    RenderTexture* RenderTexture::Create(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
    {
        RenderTexture* returnVal = new RenderTexture;
        returnVal->CreateStack(width,height,mipLevels,format);
        return returnVal;
    }
    void RenderTexture::CreateStack(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
	{
        m_width = width;
        m_height = height;
        m_mipLevels = mipLevels;
        m_format = format;
        RHI::TextureDesc desc{};
        desc.depthOrArraySize = 1;
        desc.height = height;
        desc.width = width;
        desc.mipLevels = mipLevels;
        desc.mode = RHI::TextureTilingMode::Optimal;
        desc.optimizedClearValue = nullptr;
        desc.format = format;
        desc.sampleCount = 1;
        desc.type = RHI::TextureType::Texture2D;
        desc.usage = RHI::TextureUsage::ColorAttachment | RHI::TextureUsage::SampledImage;
        RHI::AutomaticAllocationInfo allocInfo;
        allocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::None;
        RendererBase::device->CreateTexture(&desc, &m_ID, nullptr, nullptr, &allocInfo, 0, RHI::ResourceType::Automatic);
        RHI::SubResourceRange range;
        range.FirstArraySlice = 0;
        range.imageAspect = RHI::Aspect::COLOR_BIT;
        range.IndexOrFirstMipLevel = 0;
        range.NumArraySlices = 1;
        range.NumMipLevels = mipLevels;

        RHI::TextureViewDesc viewDesc;
        viewDesc.format = format;
        viewDesc.range = range;
        viewDesc.texture = m_ID;
        viewDesc.type = RHI::TextureViewType::Texture2D;
        RendererBase::device->CreateTextureView(&viewDesc, &m_view);
        
        RHI::RenderTargetViewDesc rtDesc;
        rtDesc.arraySlice = 0;
        rtDesc.format = format;
        rtDesc.TextureArray = 0;
        rtDesc.textureMipSlice = 0;
        RTView = RendererBase::CreateRenderTargetView(m_ID, &rtDesc);
	}
    RHI::Format RenderTexture::GetFormat() const{return m_format;}
    uint32_t RenderTexture::GetWidth()  const{ return m_width; }
    uint32_t RenderTexture::GetHeight() const{ return m_height; }
    RenderCubeMap* RenderCubeMap::Create(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
    {
        RenderCubeMap* returnVal = new RenderCubeMap;
        returnVal->CreateStack(width, height, mipLevels, format);
        return returnVal;
    }
    void RenderCubeMap::CreateStack(uint32_t width, uint32_t height, uint32_t mipLevels, RHI::Format format)
    {
        m_width = width;
        m_height = height;
        m_mipLevels = mipLevels;
        m_format = format;
        RHI::TextureDesc desc{};
        desc.depthOrArraySize = 6;
        desc.height = height;
        desc.width = width;
        desc.mipLevels = mipLevels;
        desc.mode = RHI::TextureTilingMode::Optimal;
        desc.optimizedClearValue = nullptr;
        desc.format = format;
        desc.sampleCount = 1;
        desc.type = RHI::TextureType::Texture2D;
        desc.usage = RHI::TextureUsage::ColorAttachment | RHI::TextureUsage::SampledImage | RHI::TextureUsage::CubeMap;
        RHI::AutomaticAllocationInfo allocInfo;
        allocInfo.access_mode = RHI::AutomaticAllocationCPUAccessMode::None;
        RendererBase::device->CreateTexture(&desc, &m_ID, nullptr, nullptr, &allocInfo, 0, RHI::ResourceType::Automatic);
        RHI::SubResourceRange range;
        range.FirstArraySlice = 0;
        range.imageAspect = RHI::Aspect::COLOR_BIT;
        range.IndexOrFirstMipLevel = 0;
        range.NumArraySlices = 6;
        range.NumMipLevels = mipLevels;

        RHI::TextureViewDesc viewDesc;
        viewDesc.format = format;
        viewDesc.range = range;
        viewDesc.texture = m_ID;
        viewDesc.type = RHI::TextureViewType::TextureCube;
        RendererBase::device->CreateTextureView(&viewDesc, &m_view);

        for (uint32_t i = 0; i < 6; i++)
        {
            RHI::RenderTargetViewDesc rtDesc;
            rtDesc.arraySlice = i;
            rtDesc.format = format;
            rtDesc.TextureArray = true;
            rtDesc.textureMipSlice = 0;
            RTViews[i] = RendererBase::CreateRenderTargetView(m_ID, &rtDesc);
        }
    }
}
