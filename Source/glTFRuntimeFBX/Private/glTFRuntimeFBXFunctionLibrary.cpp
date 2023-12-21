// Copyright 2023 - Roberto De Ioris

#include "glTFRuntimeFBXFunctionLibrary.h"
THIRD_PARTY_INCLUDES_START
#include "ufbx.h"
THIRD_PARTY_INCLUDES_END

namespace glTFRuntimeFBX
{
	FTransform GetTransform(UglTFRuntimeAsset* Asset, ufbx_transform FbxTransform)
	{
		FTransform Transform;

		Transform.SetLocation(FVector(FbxTransform.translation.x, FbxTransform.translation.y, FbxTransform.translation.z));
		Transform.SetRotation(FQuat(FbxTransform.rotation.x, FbxTransform.rotation.y, FbxTransform.rotation.z, FbxTransform.rotation.w));
		Transform.SetScale3D(FVector(FbxTransform.scale.x, FbxTransform.scale.y, FbxTransform.scale.z));


		return Asset->GetParser()->TransformTransform(Transform);
	}

	void FillSkeleton(UglTFRuntimeAsset* Asset, ufbx_node* Node, const int32 ParentIndex, TArray<FglTFRuntimeBone>& Skeleton, TMap<FString, int32>& BonesMap)
	{
		const int32 NewIndex = Skeleton.AddDefaulted();
		Skeleton[NewIndex].BoneName = UTF8_TO_TCHAR(Node->name.data);
		Skeleton[NewIndex].ParentIndex = ParentIndex;
		Skeleton[NewIndex].Transform = glTFRuntimeFBX::GetTransform(Asset, Node->local_transform);

		BonesMap.Add(Skeleton[NewIndex].BoneName, NewIndex);

		for (ufbx_node* Child : Node->children)
		{
			FillSkeleton(Asset, Child, NewIndex, Skeleton, BonesMap);
		}
	}
}

TArray<FString> UglTFRuntimeFBXFunctionLibrary::GetFBXNodeNames(UglTFRuntimeAsset* Asset)
{
	TArray<FString> Names;

	TArray64<uint8> Blob = Asset->GetParser()->GetBlob();

	ufbx_load_opts Options = {};
	ufbx_error Error;
	ufbx_scene* Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);

	for (int32 NodeIndex = 0; NodeIndex < Scene->nodes.count; NodeIndex++)
	{
		ufbx_node* Node = Scene->nodes.data[NodeIndex];

		Names.Add(UTF8_TO_TCHAR(Node->name.data));
	}

	ufbx_free_scene(Scene);

	return Names;
}

TArray<int64> UglTFRuntimeFBXFunctionLibrary::GetFBXNodeIDs(UglTFRuntimeAsset* Asset)
{
	TArray<int64> IDs;

	TArray64<uint8> Blob = Asset->GetParser()->GetBlob();

	ufbx_load_opts Options = {};
	ufbx_error Error;
	ufbx_scene* Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);

	for (int32 NodeIndex = 0; NodeIndex < Scene->nodes.count; NodeIndex++)
	{
		ufbx_node* Node = Scene->nodes.data[NodeIndex];

		IDs.Add(Node->element_id);
	}

	ufbx_free_scene(Scene);

	return IDs;
}

TArray<FglTFRuntimeFBXNode> UglTFRuntimeFBXFunctionLibrary::GetFBXNodes(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXNode> Nodes;

	TArray64<uint8> Blob = Asset->GetParser()->GetBlob();

	ufbx_load_opts Options = {};
	ufbx_error Error;
	ufbx_scene* Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);

	for (int32 NodeIndex = 0; NodeIndex < Scene->nodes.count; NodeIndex++)
	{
		FglTFRuntimeFBXNode FBXNode;
		ufbx_node* Node = Scene->nodes.data[NodeIndex];

		FBXNode.ID = Node->element_id;
		FBXNode.bHasMesh = Node->mesh != nullptr;

		Nodes.Add(MoveTemp(FBXNode));
	}

	ufbx_free_scene(Scene);

	return Nodes;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNodeId(UglTFRuntimeAsset* Asset, const int64 NodeId, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (!Asset)
	{
		return false;
	}

	RuntimeLOD.Empty();

	TArray64<uint8> Blob = Asset->GetParser()->GetBlob();

	ufbx_load_opts Options = {};
	Options.generate_missing_normals = true;
	Options.allow_null_material = true;
	Options.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
	Options.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
	Options.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
	Options.target_unit_meters = 1;
	Options.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;

	ufbx_error Error;
	ufbx_scene* Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);

	bool bSuccess = false;

	for (int32 NodeIndex = 0; NodeIndex < Scene->nodes.count; NodeIndex++)
	{
		ufbx_node* Node = Scene->nodes.data[NodeIndex];

		if (Node->element_id == NodeId)
		{
			ufbx_mesh* Mesh = Node->mesh;
			if (!Mesh)
			{
				break;
			}

			UE_LOG(LogTemp, Error, TEXT("Transform of %s: %s"), UTF8_TO_TCHAR(Node->name.data), *glTFRuntimeFBX::GetTransform(Asset, Node->local_transform).ToString());

			TMap<uint32, TMap<int32, float>> JointsWeightsMap;

			if (Mesh->skin_deformers.count > 0)
			{
				UE_LOG(LogTemp, Error, TEXT("Has skeleton!"));

				ufbx_skin_deformer* Skin = Mesh->skin_deformers.data[0];

				TArray<FglTFRuntimeBone>& Skeleton = RuntimeLOD.Skeleton;

				TSet<FString> Bones;

				ufbx_node* RootNode = nullptr;

				TMap<FString, int32> BonesMap;

				FString RootBoneName;

				for (int32 ClusterIndex = 0; ClusterIndex < Skin->clusters.count; ClusterIndex++)
				{
					ufbx_skin_cluster* Cluster = Skin->clusters.data[ClusterIndex];

					if (!RootNode)
					{
						RootNode = Cluster->bone_node;

						ufbx_node* Parent = RootNode->parent;
						while (Parent && !Parent->is_root)
						{
							RootNode = Parent;

							Parent = Parent->parent;
						}

						glTFRuntimeFBX::FillSkeleton(Asset, RootNode, INDEX_NONE, Skeleton, BonesMap);
					}

					const FString BoneName = UTF8_TO_TCHAR(Cluster->bone_node->name.data);

					if (BonesMap.Contains(BoneName))
					{
						const int32 BoneIndex = BonesMap[BoneName];

						for (uint32 VertexIndex = 0; VertexIndex < Cluster->vertices.count; VertexIndex++)
						{
							const uint32 VertexIndexValue = Cluster->vertices.data[VertexIndex];
							const float VertexWeight = Cluster->weights.data[VertexIndex];
							if (!JointsWeightsMap.Contains(VertexIndexValue))
							{
								JointsWeightsMap.Add(VertexIndexValue);
							}

							JointsWeightsMap[VertexIndexValue].Add(BoneIndex, VertexWeight);
						}
					}
				}

			}


			uint32 NumTriangleIndices = Mesh->max_face_triangles * 3;
			TArray<uint32> TriangleIndices;
			TriangleIndices.AddUninitialized(NumTriangleIndices);

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < Mesh->materials.count; PrimitiveIndex++)
			{
				ufbx_mesh_material* MeshMaterial = &Mesh->materials.data[PrimitiveIndex];
				if (MeshMaterial->num_triangles == 0)
				{
					continue;
				}

				FglTFRuntimePrimitive Primitive;
				FglTFRuntimeMaterial Material;
				Material.BaseColorFactor = FLinearColor(MeshMaterial->material->pbr.base_factor.value_vec4.x,
					MeshMaterial->material->pbr.base_factor.value_vec4.y,
					MeshMaterial->material->pbr.base_factor.value_vec4.z,
					MeshMaterial->material->pbr.base_factor.value_vec4.w);

				if (MeshMaterial->material->pbr.base_color.texture)
				{
					TArray64<uint8> ImageData;
					ImageData.Append(reinterpret_cast<const uint8*>(MeshMaterial->material->pbr.base_color.texture->content.data), MeshMaterial->material->pbr.base_color.texture->content.size);
					Asset->GetParser()->LoadBlobToMips(ImageData, Material.BaseColorTextureMips, true, MaterialsConfig);
				}

				if (MeshMaterial->material->pbr.normal_map.texture)
				{
					TArray64<uint8> ImageData;
					ImageData.Append(reinterpret_cast<const uint8*>(MeshMaterial->material->pbr.normal_map.texture->content.data), MeshMaterial->material->pbr.normal_map.texture->content.size);
					Asset->GetParser()->LoadBlobToMips(ImageData, Material.NormalTextureMips, false, MaterialsConfig);
				}

				Primitive.Material = Primitive.Material = Asset->GetParser()->BuildMaterial(-1, UTF8_TO_TCHAR(MeshMaterial->material->name.data), Material, MaterialsConfig, false);

				Primitive.Joints.AddDefaulted();
				Primitive.Weights.AddDefaulted();
				Primitive.UVs.AddDefaulted();

				for (uint32 FaceIndex = 0; FaceIndex < MeshMaterial->num_faces; FaceIndex++)
				{
					ufbx_face Face = Mesh->faces.data[MeshMaterial->face_indices.data[FaceIndex]];
					uint32 NumTriangles = ufbx_triangulate_face(TriangleIndices.GetData(), NumTriangleIndices, Mesh, Face);

					for (uint32 VertexIndex = 0; VertexIndex < NumTriangles * 3; VertexIndex++)
					{
						const uint32 Index = TriangleIndices[VertexIndex];
						if (!JointsWeightsMap.Contains(Mesh->vertex_indices.data[Index]))
						{
							UE_LOG(LogTemp, Error, TEXT("Index %u not found!"), Index);

						}
						else
						{
							const TMap<int32, float>& JointsWeights = JointsWeightsMap[Mesh->vertex_indices.data[Index]];
							int32 BoneIndex = 0;
							FglTFRuntimeUInt16Vector4 Joints;
							FVector4 Weights(0, 0, 0, 0);
							for (const TPair<int32, float>& Pair : JointsWeights)
							{
								//UE_LOG(LogTemp, Error, TEXT("Index: %u/%u Bone: %u Weight: %f"), Index, BoneIndex, Pair.Key, Pair.Value);
								Joints[BoneIndex] = Pair.Key;
								Weights[BoneIndex] = Pair.Value;
								BoneIndex++;
								if (BoneIndex > 3)
								{
									break;
								}
							}
							Primitive.Joints[0].Add(Joints);
							Primitive.Weights[0].Add(Weights);
						}
						ufbx_vec3 Position = ufbx_get_vertex_vec3(&Mesh->vertex_position, Index);
						ufbx_vec3 Normal = ufbx_get_vertex_vec3(&Mesh->vertex_normal, Index);
						ufbx_vec2 UV = Mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&Mesh->vertex_uv, Index) : ufbx_vec2{ 0 };

						Primitive.Positions.Add(Asset->GetParser()->TransformPosition(FVector(Position.x, Position.y, Position.z)));
						Primitive.Normals.Add(Asset->GetParser()->TransformVector(FVector(Normal.x, Normal.y, Normal.z)));
						Primitive.UVs[0].Add(FVector2D(UV.x, 1 - UV.y));

						Primitive.Indices.Add(Primitive.Positions.Num() - 1);
					}
				}

				RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
			}

			for (int32 AnimStackIndex = 0; AnimStackIndex < Scene->anim_stacks.count; AnimStackIndex++)
			{
				ufbx_anim_stack* AnimStack = Scene->anim_stacks.data[AnimStackIndex];

				UE_LOG(LogTemp, Error, TEXT("Anim %s %f %f"), UTF8_TO_TCHAR(AnimStack->name.data), AnimStack->time_begin, AnimStack->time_end);

				FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(&AnimStack->anim, Node, 0.1));
			}

			break;
		}
	}

	ufbx_free_scene(Scene);
	return bSuccess;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& Node, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	return LoadFBXAsRuntimeLODByNodeId(Asset, Node.ID, RuntimeLOD, MaterialsConfig);
}