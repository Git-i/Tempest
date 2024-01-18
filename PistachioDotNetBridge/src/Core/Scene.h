#pragma once
#define PT_CUSTOM_ENTRY
#include "Pistachio.h"
#include "ManagedBase.h"
#include "Entity.h"
#include "EditorCamera.h"

namespace PistachioCS
{
	public enum class SceneGraphOp
	{
		EntityCreated, EntityRemoved, EntityReparented
	};
	public ref struct SceneGraphChangedArgs : public System::EventArgs
	{
		SceneGraphOp op;
		Entity^ affectedEntity;
		System::Collections::ObjectModel::Collection<Entity^>^ entityParentHierarchy;
		//only valid for reparents
		System::Collections::ObjectModel::Collection<Entity^>^ OldEntityParentHierarchy;
	};
	public ref class Scene : ManagedBase<Pistachio::Scene>
	{
	public:
		Scene(float width, float height);
		Entity^ CreateEntity(System::String^ name);
		void ReParentEntity(Entity^ child,Entity^ parent);
		void DeleteEntity(Entity^ entity);
		void OnUpdateEditor(float delta, EditorCamera^ camera);
		System::IntPtr GetImage();
		void FreeImage(System::IntPtr^ ptr);
		Entity^ GetRootEntity();
		System::Collections::ObjectModel::Collection<Entity^>^ GetEntityChildern(Entity^ entity);
		System::EventHandler<SceneGraphChangedArgs^>^ SceneGraphChanged;
	};
}
