// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeFBXFunctionLibrary.h"
THIRD_PARTY_INCLUDES_START
#include "ufbx.h"
THIRD_PARTY_INCLUDES_END

namespace glTFRuntimeFBX
{
	FTransform GetTransform(ufbx_transform FbxTransform)
	{
		FTransform Transform;

		return Transform;
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

bool UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNodeId(UglTFRuntimeAsset* Asset, const int64 NodeId, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (!Asset)
	{
		return false;
	}

	TArray64<uint8> Blob = Asset->GetParser()->GetBlob();

	ufbx_load_opts Options = {};
	Options.generate_missing_normals = true;
	Options.allow_null_material = true;
	Options.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
	Options.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
	Options.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
	Options.target_unit_meters = 1.0;
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

			UE_LOG(LogTemp, Error, TEXT("(%f %f %f) (%f %f %f %f) (%f %f %f)"),
				Node->local_transform.translation.x,
				Node->local_transform.translation.y,
				Node->local_transform.translation.z,
				Node->local_transform.rotation.x,
				Node->local_transform.rotation.y,
				Node->local_transform.rotation.z,
				Node->local_transform.rotation.w,
				Node->local_transform.scale.x,
				Node->local_transform.scale.y,
				Node->local_transform.scale.z);

			if (Mesh->skin_deformers.count > 0)
			{
				UE_LOG(LogTemp, Error, TEXT("Has skeleton!"));

				ufbx_skin_deformer* Skin = Mesh->skin_deformers.data[0];

				TArray<FglTFRuntimeBone> Skeleton;
				Skeleton.AddDefaulted(Skin->clusters.count);

				for (int32 ClusterIndex = 0; ClusterIndex < Skin->clusters.count; ClusterIndex++)
				{
					ufbx_skin_cluster* Cluster = Skin->clusters.data[ClusterIndex];
					UE_LOG(LogTemp, Error, TEXT("%d %d %s"), Cluster->bone_node->element_id, Cluster->bone_node->typed_id, UTF8_TO_TCHAR(Cluster->bone_node->name.data));

					Skeleton[Cluster->bone_node->typed_id - 1].BoneName = UTF8_TO_TCHAR(Cluster->bone_node->name.data);
					Skeleton[Cluster->bone_node->typed_id - 1].ParentIndex = Cluster->bone_node->typed_id == 1 ? INDEX_NONE : Cluster->bone_node->parent->typed_id - 1;
					Skeleton[Cluster->bone_node->typed_id - 1].Transform = glTFRuntimeFBX::GetTransform(ufbx_matrix_to_transform(&(Cluster->geometry_to_bone)));
				}

			}

			uint32 NumTriangleIndices = Mesh->max_face_triangles * 3;
			TArray<uint32_t> TriangleIndices;
			TriangleIndices.AddUninitialized(NumTriangleIndices);

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < Mesh->materials.count; PrimitiveIndex++)
			{

				ufbx_mesh_material* MeshMaterial = &Mesh->materials.data[PrimitiveIndex];
				if (MeshMaterial->num_triangles == 0)
				{
					continue;
				}

				FglTFRuntimePrimitive Primitive;
				Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);

				for (uint32 FaceIndex = 0; FaceIndex < MeshMaterial->num_faces; FaceIndex++)
				{
					ufbx_face Face = Mesh->faces.data[MeshMaterial->face_indices.data[FaceIndex]];
					uint32 NumTriangles = ufbx_triangulate_face(TriangleIndices.GetData(), NumTriangleIndices, Mesh, Face);

					for (uint32 VertexIndex = 0; VertexIndex < NumTriangles * 3; VertexIndex++)
					{
						uint32 Index = TriangleIndices[VertexIndex];

						ufbx_vec3 Position = ufbx_get_vertex_vec3(&Mesh->vertex_position, Index);
						ufbx_vec3 Normal = ufbx_get_vertex_vec3(&Mesh->vertex_normal, Index);
						ufbx_vec2 UV = Mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&Mesh->vertex_uv, Index) : ufbx_vec2{ 0 };

						Primitive.Positions.Add(Asset->GetParser()->TransformPosition(FVector(Position.x, Position.y, Position.z)));
						Primitive.Normals.Add(Asset->GetParser()->TransformVector(FVector(Position.x, Position.y, Position.z)));

						Primitive.Indices.Add(Primitive.Positions.Num() - 1);
					}
				}

				RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
			}



			break;
		}
	}

	ufbx_free_scene(Scene);
	return bSuccess;
}