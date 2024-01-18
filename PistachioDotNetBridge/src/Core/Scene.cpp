#include "pch.h"
#include "Scene.h"
const DirectX::SimpleMath::Matrix DirectX::SimpleMath::Matrix::Identity = DirectX::SimpleMath::Matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
namespace PistachioCS
{
	Scene::Scene(float width, float height)
	{
		Pistachio::SceneDesc dsc;
		dsc.Resolution = { width, height };
		m_ptr = new Pistachio::Scene(dsc);
		
	}
	Entity^ Scene::CreateEntity(System::String^ name)
	{
		Pistachio::Entity e = m_ptr->CreateEntity((char*)System::Runtime::InteropServices::Marshal::StringToHGlobalAnsi(name).ToPointer());
		auto entity = gcnew Entity(e, m_ptr);
		SceneGraphChangedArgs^ args = gcnew SceneGraphChangedArgs();
		args->affectedEntity = entity;
		args->entityParentHierarchy = gcnew System::Collections::ObjectModel::Collection<Entity^>();
		args->op = SceneGraphOp::EntityCreated;
		args->entityParentHierarchy->Add(GetRootEntity());
		SceneGraphChanged->Invoke(this, args);
		//SceneGraphChanged->BeginInvoke(this, nullptr, nullptr, nullptr);
		return entity;
	}

	void Scene::ReParentEntity(Entity^ child, Entity^ parent)
	{
		if ((*child->m_ptr) == (*parent->m_ptr)) throw gcnew System::ArgumentException("Cannot Reparent entity to itself");
		SceneGraphChangedArgs^ args = gcnew SceneGraphChangedArgs();
		//fill the old hierarchy
		args->OldEntityParentHierarchy = gcnew System::Collections::ObjectModel::Collection<Entity^>();
		int PID = child->m_ptr->GetComponent<Pistachio::ParentComponent>().parentID;
		while (PID != -1)
		{
			args->OldEntityParentHierarchy->Add(gcnew Entity((entt::entity)PID, m_ptr));
			PID = Pistachio::Entity((entt::entity)PID, m_ptr).GetComponent<Pistachio::ParentComponent>().parentID;
		}
		//fill the new hierarchy
		child->m_ptr->GetComponent<Pistachio::ParentComponent>().parentID = parent->m_ptr->operator uint32_t();
		args->affectedEntity = child;
		args->entityParentHierarchy = gcnew System::Collections::ObjectModel::Collection<Entity^>();
		PID = child->m_ptr->GetComponent<Pistachio::ParentComponent>().parentID;
		while (PID != -1)
		{
			args->entityParentHierarchy->Add(gcnew Entity((entt::entity)PID, m_ptr));
			PID = Pistachio::Entity((entt::entity)PID, m_ptr).GetComponent<Pistachio::ParentComponent>().parentID;
		}
		args->op = SceneGraphOp::EntityReparented;
		SceneGraphChanged->Invoke(this, args);
	}
	
	void Scene::DeleteEntity(Entity^ entity)
	{
		//add error checking and exception throwing todo
		if (entity->m_ptr->GetComponent<Pistachio::ParentComponent>().parentID == -1)
		{
			throw gcnew System::ArgumentException("Cannot Delete Root Entity of a scene");
		}
		SceneGraphChangedArgs^ args = gcnew SceneGraphChangedArgs();
		args->affectedEntity = entity;
		args->entityParentHierarchy = gcnew System::Collections::ObjectModel::Collection<Entity^>();
		int PID = entity->m_ptr->GetComponent<Pistachio::ParentComponent>().parentID;
		while (PID != -1)
		{
			args->entityParentHierarchy->Add(gcnew Entity((entt::entity)PID, m_ptr));
			PID = Pistachio::Entity((entt::entity)PID, m_ptr).GetComponent<Pistachio::ParentComponent>().parentID;
		}
		args->op = SceneGraphOp::EntityRemoved;
		m_ptr->DestroyEntity(*entity->m_ptr);
		SceneGraphChanged->Invoke(this, args);
	}
	void Scene::OnUpdateEditor(float delta, EditorCamera^ camera)
	{
		return m_ptr->OnUpdateEditor(delta, *camera->m_ptr);
	}
	System::IntPtr Scene::GetImage()
	{
		unsigned char* a = new unsigned char[1280 * 720 * 4];
		m_ptr->GetRenderedScene().GetRenderTexture().CopyToCPUBuffer(a);
		return System::IntPtr(a);
	}
	void Scene::FreeImage(System::IntPtr^ ptr)
	{
		delete ptr->ToPointer();
	}
	Entity^ Scene::GetRootEntity()
	{
		return gcnew Entity(m_ptr->GetRootEntity(), m_ptr);
	}
	System::Collections::ObjectModel::ObservableCollection<Entity^>^ PistachioCS::Scene::GetEntityChildern(Entity ^ entity)
	{
		System::Collections::ObjectModel::ObservableCollection<Entity^>^ children = gcnew System::Collections::ObjectModel::ObservableCollection<Entity^>();
		auto id = entity->m_ptr->operator uint32_t();
		auto view = m_ptr->GetAllComponents<Pistachio::ParentComponent>();
		bool HasChildren = false;
		for (auto pc : view)
		{
			if (view.get<Pistachio::ParentComponent>(pc).parentID == id)
			{
				children->Add(gcnew Entity(pc, m_ptr));
				HasChildren = true;
			}
		}
		if (!HasChildren) return nullptr;
		return children;
	}
}