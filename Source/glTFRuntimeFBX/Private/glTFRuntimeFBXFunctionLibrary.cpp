// Copyright 2023-2024 - Roberto De Ioris

#include "glTFRuntimeFBXFunctionLibrary.h"
THIRD_PARTY_INCLUDES_START
#include "ufbx.h"
THIRD_PARTY_INCLUDES_END

struct FglTFRuntimeFBXCacheData : FglTFRuntimePluginCacheData
{
	ufbx_scene* Scene = nullptr;

	TMap<uint32, ufbx_node*> NodesMap;

	~FglTFRuntimeFBXCacheData()
	{
		if (Scene)
		{
			ufbx_free_scene(Scene);
		}
	}
};

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

	void FillSkeleton(UglTFRuntimeAsset* Asset, ufbx_node* Node, const int32 ParentIndex, TArray<FglTFRuntimeBone>& Skeleton, TMap<FString, int32>& BonesMap, TArray<ufbx_node*>& DiscoveredBones)
	{
		const int32 NewIndex = Skeleton.AddDefaulted();
		Skeleton[NewIndex].BoneName = UTF8_TO_TCHAR(Node->name.data);
		Skeleton[NewIndex].ParentIndex = ParentIndex;
		Skeleton[NewIndex].Transform = glTFRuntimeFBX::GetTransform(Asset, Node->local_transform);

		BonesMap.Add(Skeleton[NewIndex].BoneName, NewIndex);

		DiscoveredBones.Add(Node);

		for (ufbx_node* Child : Node->children)
		{
			FillSkeleton(Asset, Child, NewIndex, Skeleton, BonesMap, DiscoveredBones);
		}
	}

	void FillNode(UglTFRuntimeAsset* Asset, ufbx_node* Node, FglTFRuntimeFBXNode& FBXNode)
	{
		FBXNode.Id = Node->element_id;
		FBXNode.Name = UTF8_TO_TCHAR(Node->name.data);
		FBXNode.Transform = GetTransform(Asset, Node->local_transform);
		FBXNode.bHasMesh = Node->mesh != nullptr;
	}

	TSharedPtr<FglTFRuntimeFBXCacheData> GetCacheData(UglTFRuntimeAsset* Asset)
	{
		if (Asset->GetParser()->PluginsCacheData.Contains("FBX"))
		{
			if (Asset->GetParser()->PluginsCacheData["FBX"]->bValid)
			{
				return StaticCastSharedPtr<FglTFRuntimeFBXCacheData>(Asset->GetParser()->PluginsCacheData["FBX"]);
			}
			else
			{
				return nullptr;
			}
		}

		TSharedRef<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = MakeShared<FglTFRuntimeFBXCacheData>();

		Asset->GetParser()->PluginsCacheData.Add("FBX", RuntimeFBXCacheData);

		TArray64<uint8>& Blob = Asset->GetParser()->GetBlob();

		ufbx_load_opts Options = {};
		Options.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
		Options.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
		Options.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
		Options.target_unit_meters = 1;
		Options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

		ufbx_error Error;
		RuntimeFBXCacheData->Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);

		if (!RuntimeFBXCacheData->Scene)
		{
			return nullptr;
		}

		for (int32 NodeIndex = 0; NodeIndex < RuntimeFBXCacheData->Scene->nodes.count; NodeIndex++)
		{
			ufbx_node* Node = RuntimeFBXCacheData->Scene->nodes.data[NodeIndex];

			RuntimeFBXCacheData->NodesMap.Add(Node->element_id, Node);
		}

		RuntimeFBXCacheData->bValid = true;

		return RuntimeFBXCacheData;
	}
}

TArray<FglTFRuntimeFBXNode> UglTFRuntimeFBXFunctionLibrary::GetFBXNodes(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXNode> Nodes;

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return Nodes;
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < RuntimeFBXCacheData->Scene->nodes.count; NodeIndex++)
	{
		FglTFRuntimeFBXNode FBXNode;
		ufbx_node* Node = RuntimeFBXCacheData->Scene->nodes.data[NodeIndex];

		glTFRuntimeFBX::FillNode(Asset, Node, FBXNode);

		Nodes.Add(MoveTemp(FBXNode));
	}

	return Nodes;
}

FglTFRuntimeFBXNode UglTFRuntimeFBXFunctionLibrary::GetFBXRootNode(UglTFRuntimeAsset* Asset)
{
	FglTFRuntimeFBXNode FBXNode;

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return FBXNode;
		}
	}

	glTFRuntimeFBX::FillNode(Asset, RuntimeFBXCacheData->Scene->root_node, FBXNode);

	return FBXNode;
}

TArray<FglTFRuntimeFBXNode> UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode)
{
	TArray<FglTFRuntimeFBXNode> Nodes;

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return Nodes;
		}
	}

	if (RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		for (uint32 ChildNodeIndex = 0; ChildNodeIndex < RuntimeFBXCacheData->NodesMap[FBXNode.Id]->children.count; ChildNodeIndex++)
		{
			FglTFRuntimeFBXNode ChildFBXNode;
			ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id]->children.data[ChildNodeIndex];

			glTFRuntimeFBX::FillNode(Asset, Node, ChildFBXNode);

			Nodes.Add(MoveTemp(ChildFBXNode));
		}
	}

	return Nodes;
}

TArray<FglTFRuntimeFBXAnim> UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXAnim> Anims;

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return Anims;
		}
	}

	for (int32 AnimStackIndex = 0; AnimStackIndex < RuntimeFBXCacheData->Scene->anim_stacks.count; AnimStackIndex++)
	{
		ufbx_anim_stack* AnimStack = RuntimeFBXCacheData->Scene->anim_stacks.data[AnimStackIndex];

		FglTFRuntimeFBXAnim FBXAnim;
		FBXAnim.Id = AnimStack->element_id;
		FBXAnim.Name = UTF8_TO_TCHAR(AnimStack->name.data);
		FBXAnim.Duration = AnimStack->time_end - AnimStack->time_begin;

		UE_LOG(LogTemp, Error, TEXT("%s %f"), *FBXAnim.Name, AnimStack->time_begin);

		Anims.Add(MoveTemp(FBXAnim));
	}

	return Anims;
}

UAnimSequence* UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	if (!Asset || !SkeletalMesh)
	{
		return nullptr;
	}

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return nullptr;
		}
	}

	ufbx_node* FoundNode = nullptr;

	for (int32 NodeIndex = 0; NodeIndex < RuntimeFBXCacheData->Scene->nodes.count; NodeIndex++)
	{
		ufbx_node* Node = RuntimeFBXCacheData->Scene->nodes.data[NodeIndex];

		if (Node->element_id == FBXNode.Id)
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode || !FoundNode->mesh || FoundNode->mesh->skin_deformers.count < 1)
	{
		return nullptr;
	}

	ufbx_anim_stack* FoundAnim = nullptr;

	for (int32 AnimStackIndex = 0; AnimStackIndex < RuntimeFBXCacheData->Scene->anim_stacks.count; AnimStackIndex++)
	{
		ufbx_anim_stack* AnimStack = RuntimeFBXCacheData->Scene->anim_stacks.data[AnimStackIndex];

		if (AnimStack->element_id == FBXAnim.Id)
		{
			FoundAnim = AnimStack;
			break;
		}
	}

	if (!FoundAnim)
	{
		return nullptr;
	}

	const float Duration = FoundAnim->time_end - FoundAnim->time_begin;
	const int32 NumFrames = SkeletalAnimationConfig.FramesPerSecond * Duration;
	const float Delta = Duration / NumFrames;

	ufbx_skin_deformer* Skin = FoundNode->mesh->skin_deformers.data[0];

	FglTFRuntimePoseTracksMap PosesMap;
	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;

	for (int32 ClusterIndex = 0; ClusterIndex < Skin->clusters.count; ClusterIndex++)
	{
		ufbx_skin_cluster* Cluster = Skin->clusters.data[ClusterIndex];
		//UE_LOG(LogTemp, Error, TEXT("%s"), UTF8_TO_TCHAR(Cluster->bone_node->name.data));

		const FString BoneName = UTF8_TO_TCHAR(Cluster->bone_node->name.data);

		float Time = FoundAnim->time_begin;
		if (SkeletalMesh->GetRefSkeleton().FindBoneIndex(*BoneName) > INDEX_NONE)
		{
			FRawAnimSequenceTrack Track;
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
				FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(FoundAnim->anim, Cluster->bone_node, Time));
				Track.PosKeys.Add(FVector3f(Transform.GetLocation()));
				Track.RotKeys.Add(FQuat4f(Transform.GetRotation()));
				Track.ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
				Time += Delta;
			}

			PosesMap.Add(BoneName, MoveTemp(Track));
		}
	}

	/*for (ufbx_node* FbxNode : AnimationNodes)
	{
		FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(AnimStack->anim, FbxNode, 0.1));
		UE_LOG(LogTemp, Error, TEXT("%s = %s"), UTF8_TO_TCHAR(FbxNode->name.data), *Transform.ToString());
	}*/

	return Asset->GetParser()->LoadSkeletalAnimationFromTracksAndMorphTargets(SkeletalMesh, PosesMap, MorphTargetCurves, Duration, SkeletalAnimationConfig);
}

bool UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (!Asset)
	{
		return false;
	}

	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return false;
		}
	}

	const uint32 NodeId = FBXNode.Id;

	RuntimeLOD.Empty();

	bool bSuccess = false;

	for (int32 NodeIndex = 0; NodeIndex < RuntimeFBXCacheData->Scene->nodes.count; NodeIndex++)
	{
		ufbx_node* Node = RuntimeFBXCacheData->Scene->nodes.data[NodeIndex];

		if (Node->element_id == NodeId)
		{
			ufbx_mesh* Mesh = Node->mesh;
			if (!Mesh)
			{
				break;
			}

			UE_LOG(LogTemp, Error, TEXT("Transform of %s: %s"), UTF8_TO_TCHAR(Node->name.data), *glTFRuntimeFBX::GetTransform(Asset, Node->local_transform).ToString());

			TMap<uint32, TMap<int32, float>> JointsWeightsMap;

			TArray<ufbx_node*> AnimationNodes;

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

						glTFRuntimeFBX::FillSkeleton(Asset, RootNode, INDEX_NONE, Skeleton, BonesMap, AnimationNodes);
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

			for (uint32 PrimitiveIndex = 0; PrimitiveIndex < Node->materials.count; PrimitiveIndex++)
			{
				ufbx_material* MeshMaterial = Mesh->materials.data[PrimitiveIndex];

				FglTFRuntimePrimitive Primitive;
				FglTFRuntimeMaterial Material;
				Material.BaseColorFactor = FLinearColor(MeshMaterial->pbr.base_factor.value_vec4.x,
					MeshMaterial->pbr.base_factor.value_vec4.y,
					MeshMaterial->pbr.base_factor.value_vec4.z,
					MeshMaterial->pbr.base_factor.value_vec4.w);

				if (MeshMaterial->pbr.base_color.texture)
				{
					TArray64<uint8> ImageData;
					ImageData.Append(reinterpret_cast<const uint8*>(MeshMaterial->pbr.base_color.texture->content.data), MeshMaterial->pbr.base_color.texture->content.size);
					Asset->GetParser()->LoadBlobToMips(ImageData, Material.BaseColorTextureMips, true, MaterialsConfig);
				}

				if (MeshMaterial->pbr.normal_map.texture)
				{
					TArray64<uint8> ImageData;
					ImageData.Append(reinterpret_cast<const uint8*>(MeshMaterial->pbr.normal_map.texture->content.data), MeshMaterial->pbr.normal_map.texture->content.size);
					Asset->GetParser()->LoadBlobToMips(ImageData, Material.NormalTextureMips, false, MaterialsConfig);
				}

				Primitive.Material = Primitive.Material = Asset->GetParser()->BuildMaterial(-1, UTF8_TO_TCHAR(MeshMaterial->name.data), Material, MaterialsConfig, false);

				Primitive.Joints.AddDefaulted();
				Primitive.Weights.AddDefaulted();
				Primitive.UVs.AddDefaulted();

				for (uint32 FaceIndex = 0; FaceIndex < Mesh->num_faces; FaceIndex++)
				{
					if (Mesh->face_material.data[FaceIndex] == PrimitiveIndex)
					{
						ufbx_face Face = Mesh->faces.data[FaceIndex];
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
				}

				RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
			}

			bSuccess = true;

			break;
		}
	}

	return bSuccess;
}