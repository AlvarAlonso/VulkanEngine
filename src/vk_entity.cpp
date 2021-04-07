#include "vk_entity.h"
#include "vk_engine.h"
#include <extra/imgui/imgui.h>
#include <extra/imgui/imgui_impl_sdl.h>
#include <extra/imgui/imgui_impl_vulkan.h>
#include <extra/imgui/ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>

Entity::Entity()
{
	_model = glm::mat4(1);
}

RenderObject::RenderObject() : Entity()
{
	_prefab = nullptr;
}

RenderObject::RenderObject(const std::string& name)
{
	_name = name;
}

void RenderObject::renderInMenu()
{
	ImGui::Text("Name: %s", _name.c_str());
	
	if (ImGui::Button("Selected"))
		VulkanEngine::cinstance->gizmoEntity = this;

	if(ImGui::TreeNode((void*)this, "Model"))
	{
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(_model), matrixTranslation, matrixRotation, matrixScale);
		ImGui::DragFloat3("Position", matrixTranslation, 0.1f);
		ImGui::DragFloat3("Rotation", matrixRotation, 0.1f);
		ImGui::DragFloat3("Scale", matrixScale, 0.1f);
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, glm::value_ptr(_model));
		
		ImGui::TreePop();
	}
}

Light::Light() : Entity()
{
	_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	_maxDist = 10.0f;
	_intensity = 30.0f;
	_radius = 1.0f;
	_type = DIRECTIONAL;
}

Light::Light(const std::string& name)
{
	_name = name;
}

void Light::renderInMenu()
{
	ImGui::Text("Name: %s", _name.c_str());

	if (ImGui::Button("Selected"))
		VulkanEngine::cinstance->gizmoEntity = this;

	// TODO: Add debug options specific to light

	if (ImGui::TreeNode((void*)this, "Model"))
	{
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(_model), matrixTranslation, matrixRotation, matrixScale);
		ImGui::DragFloat3("Position", matrixTranslation, 0.1f);
		ImGui::DragFloat3("Rotation", matrixRotation, 0.1f);
		ImGui::DragFloat3("Scale", matrixScale, 0.1f);
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, glm::value_ptr(_model));

		ImGui::TreePop();
	}
}
