#pragma once
#include "Core/Buffer.h"
#include "RendererID_t.h"
namespace Pistachio {
	class PISTACHIO_API VertexBuffer
	{
	public:
		VertexBuffer();
		void Bind() const;
		void UnBind();
		static VertexBuffer* Create(unsigned int size, unsigned int stride);
		void CreateStack(unsigned int size, unsigned int stride);
		void SetData(const void* data, unsigned int size);
		static VertexBuffer* Create(const void* vertices, unsigned int size, unsigned int stride);
		void CreateStack(const void* vertices, unsigned int size, unsigned int stride);
		
	private:
		//we want fast direct acess in the renderer
		friend class Renderer;
		unsigned int stride = 0;
		RHI::Buffer* ID;
	};
	class PISTACHIO_API IndexBuffer
	{
	public:
		IndexBuffer();
		void Bind() const;
		void UnBind();
		static IndexBuffer* Create(const void* indices, unsigned int size, unsigned int stride);
		void CreateStack(const void* indices, unsigned int size, unsigned int stride);
		inline unsigned int GetCount() const{ return count; }
	private:
		friend class Renderer;
		unsigned int count;
		RHI::Buffer* ID;
	};
	struct PISTACHIO_API StructuredBuffer
	{
		void Bind(std::uint32_t slot) const;
		void Update(const void* data, std::uint32_t size, std::uint32_t offset);
		static StructuredBuffer* Create(const void* data, std::uint32_t size);
		void CreateStack(const void* data, std::uint32_t size);
	private:
		friend class Renderer;
		RHI::Buffer* ID;
	};
	class PISTACHIO_API ConstantBuffer {
	public:
		void Bind(std::uint32_t slot) const;
		void Update(void* data, std::uint32_t size, std::uint32_t offset);
		void CreateStack(void* data, std::uint32_t size);
		static ConstantBuffer* Create(void* data, std::uint32_t size);
		RHI::Buffer* GetID() { return ID; }
	private:
		friend class Renderer;
		RHI::Buffer* ID;
	};
}
