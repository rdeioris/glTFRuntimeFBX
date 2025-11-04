// Copyright 2023-2025 - Roberto De Ioris

#include "glTFRuntimeFBXFunctionLibrary.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#include "MaterialDomain.h"
#else
#include "MaterialShared.h"
#endif
THIRD_PARTY_INCLUDES_START
#include "ufbx.h"
THIRD_PARTY_INCLUDES_END

struct FglTFRuntimeFBXCacheData : FglTFRuntimePluginCacheData
{
	ufbx_scene* Scene = nullptr;

	TMap<uint32, ufbx_node*> NodesMap;
	TMap<FString, ufbx_node*> NodesNamesMap;

	~FglTFRuntimeFBXCacheData()
	{
		if (Scene)
		{
			ufbx_free_scene(Scene);
		}
	}

	TMap<ufbx_texture*, TStrongObjectPtr<UTexture2D>> TexturesCache;
	FCriticalSection TexturesLock;
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

	void FillSkeleton(UglTFRuntimeAsset* Asset, ufbx_skin_deformer* Skin, ufbx_node* Node, const int32 ParentIndex, TArray<FglTFRuntimeBone>& Skeleton, TMap<FString, int32>& BonesMap)
	{

		FglTFRuntimeBone Bone;

		bool bHasBone = false;

		for (int32 ClusterIndex = 0; ClusterIndex < Skin->clusters.count; ClusterIndex++)
		{
			if (Skin->clusters.data[ClusterIndex]->bone_node == Node)
			{
				ufbx_matrix BoneMatrix = Skin->clusters.data[ClusterIndex]->bind_to_world;

				bool bFound = false;
				// now find the parent
				for (int32 ParentClusterIndex = 0; ParentClusterIndex < Skin->clusters.count; ParentClusterIndex++)
				{
					if (Skin->clusters.data[ParentClusterIndex]->bone_node == Skin->clusters.data[ClusterIndex]->bone_node->parent)
					{
						ufbx_matrix ParentBoneInverseMatrix = ufbx_matrix_invert(&Skin->clusters.data[ParentClusterIndex]->bind_to_world);
						BoneMatrix = ufbx_matrix_mul(&ParentBoneInverseMatrix, &BoneMatrix);
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					ufbx_matrix ParentBoneInverseMatrix = ufbx_matrix_invert(&Skin->clusters.data[ClusterIndex]->bone_node->parent->node_to_world);
					BoneMatrix = ufbx_matrix_mul(&ParentBoneInverseMatrix, &BoneMatrix);
				}

				Bone.Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_matrix_to_transform(&BoneMatrix));
				bHasBone = true;
				break;
			}
		}

		if (!bHasBone)
		{
			Bone.Transform = glTFRuntimeFBX::GetTransform(Asset, Node->local_transform);
		}

		Bone.BoneName = UTF8_TO_TCHAR(Node->name.data);
		Bone.ParentIndex = ParentIndex;

		const int32 NewIndex = Skeleton.Add(Bone);

		BonesMap.Add(Skeleton[NewIndex].BoneName, NewIndex);

		for (ufbx_node* Child : Node->children)
		{
			FillSkeleton(Asset, Skin, Child, NewIndex, Skeleton, BonesMap);
		}
	}

	void FillNode(UglTFRuntimeAsset* Asset, ufbx_node* Node, FglTFRuntimeFBXNode& FBXNode)
	{
		FBXNode.Id = Node->element_id;
		FBXNode.Name = UTF8_TO_TCHAR(Node->name.data);
		FBXNode.Transform = GetTransform(Asset, Node->local_transform);
		FBXNode.bHasMesh = Node->mesh != nullptr;
		FBXNode.bIsLight = Node->light != nullptr;

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

		ufbx_load_opts Options = {};
		Options.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
		Options.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
		Options.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
		Options.target_unit_meters = 1;
		Options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

		ufbx_error Error;

		if (Asset->IsArchive())
		{
			TArray64<uint8> ArchiveBlob;
			for (const FString& Name : Asset->GetArchiveItems())
			{
				if (Name.EndsWith(".fbx"))
				{
					if (!Asset->GetParser()->GetBlobByName(Name, ArchiveBlob))
					{
						return nullptr;
					}

					break;
				}
			}

			if (ArchiveBlob.Num() > 0)
			{
				RuntimeFBXCacheData->Scene = ufbx_load_memory(ArchiveBlob.GetData(), ArchiveBlob.Num(), &Options, &Error);
			}
		}
		else
		{
			TArray64<uint8>& Blob = Asset->GetParser()->GetBlob();
			RuntimeFBXCacheData->Scene = ufbx_load_memory(Blob.GetData(), Blob.Num(), &Options, &Error);
		}

		if (!RuntimeFBXCacheData->Scene)
		{
			return nullptr;
		}

		for (int32 NodeIndex = 0; NodeIndex < RuntimeFBXCacheData->Scene->nodes.count; NodeIndex++)
		{
			ufbx_node* Node = RuntimeFBXCacheData->Scene->nodes.data[NodeIndex];

			RuntimeFBXCacheData->NodesMap.Add(Node->element_id, Node);
			if (Node->name.length > 0)
			{
				RuntimeFBXCacheData->NodesNamesMap.Add(UTF8_TO_TCHAR(Node->name.data), Node);
			}
		}

		RuntimeFBXCacheData->bValid = true;

		return RuntimeFBXCacheData;
	}

	bool LoadTexture(UglTFRuntimeAsset* Asset, TSharedRef<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData, ufbx_texture* Texture, UTexture2D*& TextureCache, const bool bSRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
	{
		if (!Texture)
		{
			return false;
		}

		FScopeLock TextureLock(&RuntimeFBXCacheData->TexturesLock);

		if (RuntimeFBXCacheData->TexturesCache.Contains(Texture))
		{
			TextureCache = RuntimeFBXCacheData->TexturesCache[Texture].Get();
			return true;
		}

		TArray64<uint8> ImageData;

		auto LoadTextureFromArchive = [Asset, &ImageData](ufbx_string* FilenameString) -> bool
			{
				const FString TextureFilename = FPaths::GetCleanFilename(UTF8_TO_TCHAR(FilenameString->data));
				for (const FString& Name : Asset->GetArchiveItems())
				{
					const FString CleanedName = FPaths::GetCleanFilename(Name);
					if (CleanedName == TextureFilename || CleanedName == TextureFilename.Replace(TEXT(" "), TEXT("_")))
					{
						return Asset->GetParser()->GetBlobByName(Name, ImageData);
					}
				}

				return true;
			};

		if (Texture->content.size > 0)
		{
			ImageData.Append(reinterpret_cast<const uint8*>(Texture->content.data), Texture->content.size);
		}
		else
		{
			if (Texture->filename.length > 0)
			{
				if (!LoadTextureFromArchive(&Texture->filename))
				{
					return false;
				}
			}

			// fallback to absolute
			if (ImageData.Num() == 0)
			{
				if (Texture->absolute_filename.length > 0)
				{
					if (!LoadTextureFromArchive(&Texture->absolute_filename))
					{
						return false;
					}
				}
			}

			// final fallback to local filesystem
			if (ImageData.Num() == 0)
			{
				if (Texture->filename.length > 0 && !(Asset->GetParser()->GetBaseDirectory().IsEmpty()))
				{
					const FString TextureFilename = Asset->GetParser()->GetBaseDirectory() / UTF8_TO_TCHAR(Texture->filename.data);
					if (FPaths::FileExists(TextureFilename))
					{
						FFileHelper::LoadFileToArray(ImageData, *TextureFilename);
					}
				}
			}
		}

		if (ImageData.Num() == 0)
		{
			return false;
		}

		TArray<FglTFRuntimeMipMap> Mips;
		if (!Asset->GetParser()->LoadBlobToMips(ImageData, Mips, bSRGB, MaterialsConfig))
		{
			return false;
		}

		FglTFRuntimeImagesConfig ImagesConfig = MaterialsConfig.ImagesConfig;
		ImagesConfig.bSRGB = bSRGB;
		TextureCache = Asset->GetParser()->BuildTexture(GetTransientPackage(), Mips, ImagesConfig, FglTFRuntimeTextureSampler());

		if (TextureCache)
		{
			RuntimeFBXCacheData->TexturesCache.Add(Texture, TStrongObjectPtr<UTexture2D>(TextureCache));
		}

		return TextureCache != nullptr;
	}

	UMaterialInterface* LoadMaterial(UglTFRuntimeAsset* Asset, TSharedRef<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData, ufbx_material* MeshMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, FString& MaterialName)
	{
		const bool bIsTwoSided = MeshMaterial->features.double_sided.enabled;
		bool bIsTranslucent = false;

		if (MeshMaterial->pbr.transmission_color.texture_enabled)
		{
			bIsTranslucent = true;
		}
		else if (MeshMaterial->pbr.transmission_color.has_value)
		{
			if (MeshMaterial->pbr.transmission_color.value_components == 1 && MeshMaterial->pbr.transmission_color.value_real < 1.0)
			{
				bIsTranslucent = true;
			}
			else if (MeshMaterial->pbr.transmission_color.value_components > 1 && MeshMaterial->pbr.transmission_color.value_vec4.w < 1.0)
			{
				bIsTranslucent = true;
			}
		}

		EglTFRuntimeMaterialType MaterialType = EglTFRuntimeMaterialType::Opaque;
		if (bIsTwoSided && bIsTranslucent)
		{
			MaterialType = EglTFRuntimeMaterialType::TwoSidedTranslucent;
		}
		else if (bIsTwoSided)
		{
			MaterialType = EglTFRuntimeMaterialType::TwoSided;
		}
		else if (bIsTranslucent)
		{
			MaterialType = EglTFRuntimeMaterialType::Translucent;
		}

		UMaterialInterface* BaseMaterial = nullptr;

		if (!MaterialsConfig.ForceMaterial)
		{
			MaterialName = UTF8_TO_TCHAR(MeshMaterial->name.data);

			if (MaterialsConfig.MaterialsOverrideByNameMap.Contains(MaterialName))
			{
				BaseMaterial = MaterialsConfig.MaterialsOverrideByNameMap[MaterialName];
			}
			else if (MaterialsConfig.UberMaterialsOverrideMap.Contains(MaterialType))
			{
				BaseMaterial = MaterialsConfig.UberMaterialsOverrideMap[MaterialType];
			}
			else
			{
				if (bIsTwoSided)
				{
					if (bIsTranslucent)
					{

						BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntimeFBX/MI_glTFRuntimeFBXTwoSidedTranslucent"));
					}
					else
					{
						BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntimeFBX/MI_glTFRuntimeFBXTwoSided"));
					}
				}
				else
				{
					if (bIsTranslucent)
					{
						BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntimeFBX/MI_glTFRuntimeFBXTranslucent"));
					}
					else
					{
						BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntimeFBX/M_glTFRuntimeFBXBase"));
					}
				}
			}
		}
		else
		{
			BaseMaterial = MaterialsConfig.ForceMaterial;
		}

		if (!BaseMaterial)
		{
			Asset->GetParser()->AddError("BuildMaterial()", "Unable to find glTFRuntimeFBX Material, ensure it has been packaged, falling back to default material");
			return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
		}

		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
		if (!Material)
		{
			Asset->GetParser()->AddError("BuildMaterial()", "Unable to create material instance, falling back to default material");
			return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
		}

		// make it public to allow exports
		Material->SetFlags(EObjectFlags::RF_Public);

		auto MaterialFBXSetVectorAndTextureWithFactor = [&](const ufbx_material_map& FBXMaterialMap, const ufbx_material_map& FBXMaterialFactorMap, const FString& ParamName, const bool bSRGB)
			{
				if (FBXMaterialMap.texture_enabled)
				{
					UTexture2D* FBXTexture = nullptr;
					if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
					{
						Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						Material->SetVectorParameterValue(*(ParamName + "Factor"), FLinearColor(1.0, 1.0, 1.0, 1.0));
					}

					if (FBXMaterialFactorMap.has_value)
					{
						if (FBXMaterialFactorMap.value_components == 1)
						{
							Material->SetVectorParameterValue(*(ParamName + "Factor"), FLinearColor(FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real));
						}
						else if (FBXMaterialFactorMap.value_components > 1)
						{
							Material->SetVectorParameterValue(*(ParamName + "Factor"), FLinearColor(FBXMaterialFactorMap.value_vec4.x, FBXMaterialFactorMap.value_vec4.y, FBXMaterialFactorMap.value_vec4.z, FBXMaterialFactorMap.value_vec4.w));
						}
					}
				}
				else if (FBXMaterialMap.has_value)
				{
					FLinearColor BaseValue = FLinearColor(FBXMaterialMap.value_vec4.x, FBXMaterialMap.value_vec4.y, FBXMaterialMap.value_vec4.z, FBXMaterialMap.value_vec4.w);
					if (FBXMaterialFactorMap.texture_enabled)
					{
						UTexture2D* FBXTexture = nullptr;
						if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialFactorMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
						{
							Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						}
					}
					else if (FBXMaterialFactorMap.has_value)
					{
						if (FBXMaterialFactorMap.value_components == 1)
						{
							BaseValue *= FLinearColor(FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real, FBXMaterialFactorMap.value_real);
						}
						else if (FBXMaterialFactorMap.value_components > 1)
						{
							BaseValue *= FLinearColor(FBXMaterialFactorMap.value_vec4.x, FBXMaterialFactorMap.value_vec4.y, FBXMaterialFactorMap.value_vec4.z, FBXMaterialFactorMap.value_vec4.w);
						}
					}

					Material->SetVectorParameterValue(*(ParamName + "Factor"), BaseValue);
				}
			};

		auto MaterialFBXSetVectorAndTexture = [&](const ufbx_material_map& FBXMaterialMap, const FString& ParamName, const bool bSRGB)
			{
				if (FBXMaterialMap.texture_enabled)
				{
					UTexture2D* FBXTexture = nullptr;
					if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
					{
						Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						Material->SetVectorParameterValue(*(ParamName + "Factor"), FLinearColor(1.0, 1.0, 1.0, 1.0));
					}
				}
				else if (FBXMaterialMap.has_value)
				{
					const FLinearColor BaseValue = FLinearColor(FBXMaterialMap.value_vec4.x, FBXMaterialMap.value_vec4.y, FBXMaterialMap.value_vec4.z, FBXMaterialMap.value_vec4.w);
					Material->SetVectorParameterValue(*(ParamName + "Factor"), BaseValue);
				}
			};

		auto MaterialFBXSetScalarAndTextureWithFactor = [&](const ufbx_material_map& FBXMaterialMap, const ufbx_material_map& FBXMaterialFactorMap, const FString& ParamName, const bool bSRGB)
			{
				if (FBXMaterialMap.texture_enabled)
				{
					UTexture2D* FBXTexture = nullptr;
					if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
					{
						Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						Material->SetScalarParameterValue(*(ParamName + "Factor"), 1.0);
					}

					if (FBXMaterialFactorMap.has_value)
					{
						Material->SetScalarParameterValue(*(ParamName + "Factor"), FBXMaterialFactorMap.value_real);
					}
				}
				else if (FBXMaterialMap.has_value)
				{
					double BaseValue = FBXMaterialMap.value_real;
					if (FBXMaterialFactorMap.texture_enabled)
					{
						UTexture2D* FBXTexture = nullptr;
						if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialFactorMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
						{
							Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						}
					}
					else if (FBXMaterialFactorMap.has_value)
					{
						BaseValue *= FBXMaterialFactorMap.value_real;
					}

					Material->SetScalarParameterValue(*(ParamName + "Factor"), BaseValue);
				}
			};

		auto MaterialFBXSetScalarAndTexture = [&](const ufbx_material_map& FBXMaterialMap, const FString& ParamName, const bool bSRGB)
			{
				if (FBXMaterialMap.texture_enabled)
				{
					UTexture2D* FBXTexture = nullptr;
					if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
					{
						Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						Material->SetScalarParameterValue(*(ParamName + "Factor"), 1.0);
					}
				}
				else if (FBXMaterialMap.has_value)
				{
					Material->SetScalarParameterValue(*(ParamName + "Factor"), FBXMaterialMap.value_real);
				}
			};

		auto MaterialFBXSetTexture = [&](const ufbx_material_map& FBXMaterialMap, const FString& ParamName, const bool bSRGB)
			{
				if (FBXMaterialMap.texture_enabled)
				{
					UTexture2D* FBXTexture = nullptr;
					if (LoadTexture(Asset, RuntimeFBXCacheData, FBXMaterialMap.texture, FBXTexture, bSRGB, MaterialsConfig) && FBXTexture)
					{
						Material->SetTextureParameterValue(*(ParamName + "Texture"), FBXTexture);
						Material->SetScalarParameterValue(*(ParamName + "Factor"), 1.0);
					}
				}
			};

		// base_color + base_factor
		MaterialFBXSetVectorAndTextureWithFactor(MeshMaterial->pbr.base_color, MeshMaterial->pbr.base_factor, "baseColor", true);

		if (bIsTranslucent)
		{
			// TODO: transmission_factor is currently disabled as I have no idea how it is supposed to work :P
#if 0
			// transmission_color + transmission_factor
			MaterialFBXSetScalarAndTextureWithFactor(MeshMaterial->pbr.transmission_color, MeshMaterial->pbr.transmission_factor, "transmission", false);
#endif
			// transmission_color
			MaterialFBXSetScalarAndTexture(MeshMaterial->pbr.transmission_color, "transmission", false);
		}

		// normal_map
		MaterialFBXSetTexture(MeshMaterial->pbr.normal_map, "normalMap", false);

		// roughness
		MaterialFBXSetScalarAndTexture(MeshMaterial->pbr.roughness, "roughness", false);

		// metallic
		MaterialFBXSetScalarAndTexture(MeshMaterial->pbr.metalness, "metalness", false);

		// specular
		MaterialFBXSetScalarAndTextureWithFactor(MeshMaterial->pbr.specular_color, MeshMaterial->pbr.specular_factor, "specular", false);

		// emissive_color + emissive_factor
		MaterialFBXSetVectorAndTextureWithFactor(MeshMaterial->pbr.emission_color, MeshMaterial->pbr.emission_factor, "emission", true);

		return Material;
	}
}

TArray<FglTFRuntimeFBXNode> UglTFRuntimeFBXFunctionLibrary::GetFBXNodes(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXNode> Nodes;

	if (!Asset)
	{
		return Nodes;
	}

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

TArray<FglTFRuntimeFBXNode> UglTFRuntimeFBXFunctionLibrary::GetFBXNodesMeshes(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXNode> Nodes;

	if (!Asset)
	{
		return Nodes;
	}

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
		if (!Node->mesh)
		{
			continue;
		}

		glTFRuntimeFBX::FillNode(Asset, Node, FBXNode);

		Nodes.Add(MoveTemp(FBXNode));
	}

	return Nodes;
}

FglTFRuntimeFBXNode UglTFRuntimeFBXFunctionLibrary::GetFBXRootNode(UglTFRuntimeAsset* Asset)
{
	FglTFRuntimeFBXNode FBXNode;

	if (!Asset)
	{
		return FBXNode;
	}

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

	if (!Asset)
	{
		return Nodes;
	}

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

bool UglTFRuntimeFBXFunctionLibrary::GetFBXNodeParent(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeFBXNode& FBXParentNode)
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

	if (RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		ufbx_node* ParentNode = RuntimeFBXCacheData->NodesMap[FBXNode.Id]->parent;
		if (!ParentNode)
		{
			return false;
		}

		glTFRuntimeFBX::FillNode(Asset, ParentNode, FBXParentNode);
		return true;
	}

	return false;
}

bool UglTFRuntimeFBXFunctionLibrary::IsFBXNodeBone(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode)
{
	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return false;
		}
	}

	if (RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id];
		for (uint32 ClusterIndex = 0; ClusterIndex < RuntimeFBXCacheData->Scene->skin_clusters.count; ClusterIndex++)
		{
			ufbx_skin_cluster* Cluster = RuntimeFBXCacheData->Scene->skin_clusters.data[ClusterIndex];
			if (Cluster->bone_node == Node)
			{
				return true;
			}
		}
	}

	return false;
}

bool UglTFRuntimeFBXFunctionLibrary::GetFBXDefaultAnimation(UglTFRuntimeAsset* Asset, FglTFRuntimeFBXAnim& FBXAnim)
{
	TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData = nullptr;
	{
		FScopeLock Lock(&(Asset->GetParser()->PluginsCacheDataLock));

		RuntimeFBXCacheData = glTFRuntimeFBX::GetCacheData(Asset);
		if (!RuntimeFBXCacheData)
		{
			return false;
		}
	}

	ufbx_anim* Anim = RuntimeFBXCacheData->Scene->anim;
	if (Anim)
	{

		for (int32 AnimStackIndex = 0; AnimStackIndex < RuntimeFBXCacheData->Scene->anim_stacks.count; AnimStackIndex++)
		{
			ufbx_anim_stack* AnimStack = RuntimeFBXCacheData->Scene->anim_stacks.data[AnimStackIndex];

			if (AnimStack->anim == Anim)
			{
				FBXAnim.Id = AnimStack->element_id;
				FBXAnim.Name = UTF8_TO_TCHAR(AnimStack->name.data);
				FBXAnim.Duration = AnimStack->time_end - AnimStack->time_begin;
				return true;
			}
		}
	}

	return false;
}

TArray<FglTFRuntimeFBXAnim> UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(UglTFRuntimeAsset* Asset)
{
	TArray<FglTFRuntimeFBXAnim> Anims;

	if (!Asset)
	{
		return Anims;
	}

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

	return LoadFBXAnimAsSkeletalAnimation(Asset, FBXAnim, FBXNode, SkeletalMesh->GetSkeleton(), SkeletalAnimationConfig);
}


UAnimSequence* UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	if (!Asset || !Skeleton)
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

	if (!RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		return nullptr;
	}

	ufbx_node* FoundNode = RuntimeFBXCacheData->NodesMap[FBXNode.Id];

	if (!FoundNode || !FoundNode->mesh || (FoundNode->mesh->skin_deformers.count < 1 && FoundNode->mesh->blend_deformers.count < 1))
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

	FglTFRuntimePoseTracksMap PosesMap;
	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;

	const int32 BonesNum = Skeleton->GetReferenceSkeleton().GetNum();

	for (int32 BoneIndex = 0; BoneIndex < BonesNum; BoneIndex++)
	{
		const FString BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneIndex).ToString();

		if (!RuntimeFBXCacheData->NodesNamesMap.Contains(BoneName))
		{
			continue;
		}

		ufbx_node* BoneNode = RuntimeFBXCacheData->NodesNamesMap[BoneName];

		float Time = FoundAnim->time_begin;

		FRawAnimSequenceTrack Track;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(FoundAnim->anim, BoneNode, Time));
#if ENGINE_MAJOR_VERSION >= 5
			Track.PosKeys.Add(FVector3f(Transform.GetLocation()));
			Track.RotKeys.Add(FQuat4f(Transform.GetRotation()));
			Track.ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
#else
			Track.PosKeys.Add(Transform.GetLocation());
			Track.RotKeys.Add(Transform.GetRotation());
			Track.ScaleKeys.Add(Transform.GetScale3D());
#endif
			Time += Delta;
		}

		PosesMap.Add(BoneName, MoveTemp(Track));
	}

	for (uint32 BlendDeformerIndex = 0; BlendDeformerIndex < FoundNode->mesh->blend_deformers.count; BlendDeformerIndex++)
	{
		for (uint32 BlendDeformerChannelIndex = 0; BlendDeformerChannelIndex < FoundNode->mesh->blend_deformers.data[BlendDeformerIndex]->channels.count; BlendDeformerChannelIndex++)
		{
			const FString MorphTargetName = UTF8_TO_TCHAR(FoundNode->mesh->blend_deformers.data[BlendDeformerIndex]->channels.data[BlendDeformerChannelIndex]->name.data);
			float Time = FoundAnim->time_begin;
			TArray<TPair<float, float>> MorphTargetValues;
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
				const float Weight = ufbx_evaluate_blend_weight(FoundAnim->anim, FoundNode->mesh->blend_deformers.data[BlendDeformerIndex]->channels.data[BlendDeformerChannelIndex], Time);
				MorphTargetValues.Add(TPair<float, float>(Time, Weight));
				Time += Delta;
			}

			MorphTargetCurves.Add(*MorphTargetName, MoveTemp(MorphTargetValues));
		}
	}

	return Asset->GetParser()->LoadSkeletalAnimationFromTracksAndMorphTargets(Skeleton, PosesMap, MorphTargetCurves, Duration, SkeletalAnimationConfig);
}

UAnimSequence* UglTFRuntimeFBXFunctionLibrary::LoadFBXExternalAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	if (!Asset || !SkeletalMesh)
	{
		return nullptr;
	}

	return LoadFBXExternalAnimAsSkeletalAnimation(Asset, FBXAnim, SkeletalMesh->GetSkeleton(), SkeletalAnimationConfig);
}

UAnimSequence* UglTFRuntimeFBXFunctionLibrary::LoadFBXExternalAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	if (!Asset || !Skeleton)
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

	FglTFRuntimePoseTracksMap PosesMap;
	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;

	const int32 BonesNum = Skeleton->GetReferenceSkeleton().GetNum();

	for (int32 BoneIndex = 0; BoneIndex < BonesNum; BoneIndex++)
	{
		const FString BoneName = Skeleton->GetReferenceSkeleton().GetBoneName(BoneIndex).ToString();

		if (!RuntimeFBXCacheData->NodesNamesMap.Contains(BoneName))
		{
			continue;
		}

		ufbx_node* BoneNode = RuntimeFBXCacheData->NodesNamesMap[BoneName];

		float Time = FoundAnim->time_begin;

		FRawAnimSequenceTrack Track;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(FoundAnim->anim, BoneNode, Time));
#if ENGINE_MAJOR_VERSION >= 5
			Track.PosKeys.Add(FVector3f(Transform.GetLocation()));
			Track.RotKeys.Add(FQuat4f(Transform.GetRotation()));
			Track.ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
#else
			Track.PosKeys.Add(Transform.GetLocation());
			Track.RotKeys.Add(Transform.GetRotation());
			Track.ScaleKeys.Add(Transform.GetScale3D());
#endif
			Time += Delta;
		}

		PosesMap.Add(BoneName, MoveTemp(Track));
	}

	return Asset->GetParser()->LoadSkeletalAnimationFromTracksAndMorphTargets(Skeleton, PosesMap, MorphTargetCurves, Duration, SkeletalAnimationConfig);
}

UAnimSequence* UglTFRuntimeFBXFunctionLibrary::LoadFBXRawAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	if (!Asset || !Skeleton)
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

	FglTFRuntimePoseTracksMap PosesMap;
	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;

	TMap<FString, FTransform> RestTransforms;

	for (const TPair<FString, ufbx_node*>& Pair : RuntimeFBXCacheData->NodesNamesMap)
	{
		const FString BoneName = Pair.Key;

		ufbx_node* BoneNode = Pair.Value;

		float Time = FoundAnim->time_begin;

		RestTransforms.Add(BoneName, glTFRuntimeFBX::GetTransform(Asset, BoneNode->local_transform));

		FRawAnimSequenceTrack Track;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
			FTransform Transform = glTFRuntimeFBX::GetTransform(Asset, ufbx_evaluate_transform(FoundAnim->anim, BoneNode, Time));
#if ENGINE_MAJOR_VERSION >= 5
			Track.PosKeys.Add(FVector3f(Transform.GetLocation()));
			Track.RotKeys.Add(FQuat4f(Transform.GetRotation()));
			Track.ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
#else
			Track.PosKeys.Add(Transform.GetLocation());
			Track.RotKeys.Add(Transform.GetRotation());
			Track.ScaleKeys.Add(Transform.GetScale3D());
#endif
			Time += Delta;
		}

		PosesMap.Add(BoneName, MoveTemp(Track));
	}

	FglTFRuntimePoseTracksMap Tracks = Asset->GetParser()->FixupAnimationTracks(PosesMap, RestTransforms, SkeletalAnimationConfig);

	return Asset->GetParser()->LoadSkeletalAnimationFromTracksAndMorphTargets(Skeleton, Tracks, MorphTargetCurves, Duration, SkeletalAnimationConfig);
}

bool UglTFRuntimeFBXFunctionLibrary::FillFBXSkinDeformer(UglTFRuntimeAsset* Asset, ufbx_skin_deformer* SkinDeformer, TArray<FglTFRuntimeBone>& Skeleton, TMap<uint32, TArray<TPair<int32, float>>>& JointsWeightsMap, int32& JointsWeightsGroups)
{
	TSet<FString> Bones;

	ufbx_node* RootNode = nullptr;

	TMap<FString, int32> BonesMap;

	for (int32 ClusterIndex = 0; ClusterIndex < SkinDeformer->clusters.count; ClusterIndex++)
	{
		ufbx_skin_cluster* Cluster = SkinDeformer->clusters.data[ClusterIndex];

		if (!RootNode)
		{
			RootNode = Cluster->bone_node;
			ufbx_node* Parent = RootNode->parent;
			while (Parent && !Parent->is_root)
			{
				RootNode = Parent;

				Parent = Parent->parent;
			}

			glTFRuntimeFBX::FillSkeleton(Asset, SkinDeformer, RootNode, INDEX_NONE, Skeleton, BonesMap);
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

				JointsWeightsMap[VertexIndexValue].Add(TPair<int32, float>(BoneIndex, VertexWeight));
			}
		}
	}

	int32 MaxBoneInfluences = 4;

	for (const TPair<uint32, TArray<TPair<int32, float>>>& Pair : JointsWeightsMap)
	{
		if (Pair.Value.Num() > MaxBoneInfluences)
		{
			MaxBoneInfluences = Pair.Value.Num();
		}
	}

	JointsWeightsGroups = MaxBoneInfluences / 4;
	if (MaxBoneInfluences % 4 != 0)
	{
		JointsWeightsGroups++;
	}

	return true;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeMeshLOD& RuntimeLOD, bool& bIsSkeletal, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig)
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

	if (!RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		return false;
	}

	RuntimeLOD.Empty();

	ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id];

	ufbx_mesh* Mesh = Node->mesh;
	if (!Mesh)
	{
		return false;
	}

	TMap<uint32, TArray<TPair<int32, float>>> JointsWeightsMap;
	int32 JointsWeightsGroups = 1;

	// skeletal mesh ?
	if (Mesh->skin_deformers.count > 0)
	{
		if (!FillFBXSkinDeformer(Asset, Mesh->skin_deformers.data[0], RuntimeLOD.Skeleton, JointsWeightsMap, JointsWeightsGroups))
		{
			return false;
		}
	}

	bIsSkeletal = JointsWeightsMap.Num() > 0;

	const FglTFRuntimeMaterialsConfig* MaterialsConfig = &StaticMeshMaterialsConfig;

	if (bIsSkeletal)
	{
		MaterialsConfig = &SkeletalMeshMaterialsConfig;
	}

	TArray<FglTFRuntimePrimitive> Primitives;

	if (!FillFBXPrimitives(Asset, RuntimeFBXCacheData, Node, 0, Primitives, JointsWeightsMap, JointsWeightsGroups, *MaterialsConfig))
	{
		return false;
	}

	// ensure only non-empty primitives are added
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.Indices.Num() > 0)
		{
			RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
		}
	}

	return true;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadAndMergeFBXAsRuntimeLODsGroupBySkinDeformer(UglTFRuntimeAsset* Asset, TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig)
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

	struct FglTFRuntimeFBXNodeToMerge
	{
		TArray<FglTFRuntimeBone> Skeleton;
		TMap<uint32, TArray<TPair<int32, float>>> JointsWeightsMap;
		int32 JointsWeightsGroups;
	};

	TArray< FglTFRuntimeFBXNodeToMerge> DiscoveredSkinDeformers;

	for (int32 SkinDeformerIndex = 0; SkinDeformerIndex < RuntimeFBXCacheData->Scene->skin_deformers.count; SkinDeformerIndex++)
	{
		FglTFRuntimeFBXNodeToMerge CurrentSkinDeformer;
		CurrentSkinDeformer.JointsWeightsGroups = 1;
		if (!FillFBXSkinDeformer(Asset, RuntimeFBXCacheData->Scene->skin_deformers.data[SkinDeformerIndex], CurrentSkinDeformer.Skeleton, CurrentSkinDeformer.JointsWeightsMap, CurrentSkinDeformer.JointsWeightsGroups))
		{
			return false;
		}

		DiscoveredSkinDeformers.Add(MoveTemp(CurrentSkinDeformer));
	}

	auto SkinDeformerIsCompatible = [](const TArray<FglTFRuntimeBone>& BaseSkeleton, const TArray<FglTFRuntimeBone>& CurrentSkeleton)
		{
			if (BaseSkeleton.Num() != CurrentSkeleton.Num())
			{
				return false;
			}

			for (int32 BoneIndex = 0; BoneIndex < BaseSkeleton.Num(); BoneIndex++)
			{
				if (BaseSkeleton[BoneIndex].ParentIndex != CurrentSkeleton[BoneIndex].ParentIndex)
				{
					return false;
				}

				if (BaseSkeleton[BoneIndex].BoneName != CurrentSkeleton[BoneIndex].BoneName)
				{
					return false;
				}
			}

			return true;
		};

	TSet<int32> ProcessedSkinDeformers;
	TArray<int32> SkinDeformerGroups;

	for (int32 SkinDeformerIndex = 0; SkinDeformerIndex < DiscoveredSkinDeformers.Num(); SkinDeformerIndex++)
	{
		if (ProcessedSkinDeformers.Contains(SkinDeformerIndex))
		{
			continue;
		}

		SkinDeformerGroups.Add(SkinDeformerIndex);
		ProcessedSkinDeformers.Add(SkinDeformerIndex);

		for (int32 CheckSkinDeformerIndex = 0; CheckSkinDeformerIndex < DiscoveredSkinDeformers.Num(); CheckSkinDeformerIndex++)
		{
			if (CheckSkinDeformerIndex == SkinDeformerIndex)
			{
				continue;
			}

			if (ProcessedSkinDeformers.Contains(CheckSkinDeformerIndex))
			{
				continue;
			}

			if (SkinDeformerIsCompatible(DiscoveredSkinDeformers[SkinDeformerIndex].Skeleton, DiscoveredSkinDeformers[CheckSkinDeformerIndex].Skeleton))
			{
				ProcessedSkinDeformers.Add(CheckSkinDeformerIndex);
			}
		}
	}

	for (const int32 SkinDeformerIndex : SkinDeformerGroups)
	{
		FglTFRuntimeMeshLOD LOD;
		if (LoadAndMergeFBXAsRuntimeLODBySkinDeformer(Asset, SkinDeformerIndex, LOD, SkeletalMeshMaterialsConfig))
		{
			RuntimeLODs.Add(MoveTemp(LOD));
		}
	}

	return true;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadAndMergeFBXAsRuntimeLODByBiggestSkinDeformer(UglTFRuntimeAsset* Asset, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig)
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

	struct FglTFRuntimeFBXNodeToMerge
	{
		FglTFRuntimeFBXNode FBXNode;
		TArray<FglTFRuntimeBone> Skeleton;
		TMap<uint32, TArray<TPair<int32, float>>> JointsWeightsMap;
		int32 JointsWeightsGroups;
	};

	int32 BiggestDeformerValue = -1;

	TArray<FglTFRuntimeBone> BiggestSkeleton;

	for (int32 SkinDeformerIndex = 0; SkinDeformerIndex < RuntimeFBXCacheData->Scene->skin_deformers.count; SkinDeformerIndex++)
	{
		FglTFRuntimeFBXNodeToMerge CurrentSkinDeformer;
		CurrentSkinDeformer.JointsWeightsGroups = 1;
		if (FillFBXSkinDeformer(Asset, RuntimeFBXCacheData->Scene->skin_deformers.data[SkinDeformerIndex], CurrentSkinDeformer.Skeleton, CurrentSkinDeformer.JointsWeightsMap, CurrentSkinDeformer.JointsWeightsGroups))
		{
			if (CurrentSkinDeformer.Skeleton.Num() > BiggestDeformerValue)
			{
				BiggestSkeleton = CurrentSkinDeformer.Skeleton;
				BiggestDeformerValue = CurrentSkinDeformer.Skeleton.Num();
			}
		}
	}

	if (BiggestDeformerValue < 0 || BiggestSkeleton.Num() < 1)
	{
		return false;
	}

	RuntimeLOD.Skeleton = MoveTemp(BiggestSkeleton);

	auto SkinDeformerIsCompatible = [](const TArray<FglTFRuntimeBone>& BaseSkeleton, const TArray<FglTFRuntimeBone>& CurrentSkeleton)
		{
			const int32 NumBones = FMath::Min(BaseSkeleton.Num(), CurrentSkeleton.Num());
			if (NumBones <= 0)
			{
				return false;
			}

			for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
			{
				if (BaseSkeleton[BoneIndex].ParentIndex != CurrentSkeleton[BoneIndex].ParentIndex)
				{
					return false;
				}

				if (BaseSkeleton[BoneIndex].BoneName != CurrentSkeleton[BoneIndex].BoneName)
				{
					return false;
				}
			}

			return true;
		};

	const TArray<FglTFRuntimeFBXNode> FBXNodes = GetFBXNodesMeshes(Asset);

	if (FBXNodes.Num() < 1)
	{
		return false;
	}

	TArray<FglTFRuntimeFBXNodeToMerge> NodesToMerge;

	for (const FglTFRuntimeFBXNode& FBXNode : FBXNodes)
	{
		if (!RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
		{
			continue;
		}

		ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id];

		ufbx_mesh* Mesh = Node->mesh;
		if (!Mesh)
		{
			continue;
		}

		// skeletal mesh ?
		if (Mesh->skin_deformers.count < 1)
		{
			continue;
		}

		FglTFRuntimeFBXNodeToMerge FBXNodeToMerge;
		FBXNodeToMerge.FBXNode = FBXNode;
		FBXNodeToMerge.JointsWeightsGroups = 1;

		if (!FillFBXSkinDeformer(Asset, Mesh->skin_deformers.data[0], FBXNodeToMerge.Skeleton, FBXNodeToMerge.JointsWeightsMap, FBXNodeToMerge.JointsWeightsGroups))
		{
			continue;
		}

		if (!SkinDeformerIsCompatible(RuntimeLOD.Skeleton, FBXNodeToMerge.Skeleton))
		{
			continue;
		}

		NodesToMerge.Add(MoveTemp(FBXNodeToMerge));
	}

	if (NodesToMerge.Num() < 1)
	{
		return false;
	}

	TArray<FglTFRuntimePrimitive> Primitives;

	for (const FglTFRuntimeFBXNodeToMerge& FBXNodeToMerge : NodesToMerge)
	{
		ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNodeToMerge.FBXNode.Id];

		if (!FillFBXPrimitives(Asset, RuntimeFBXCacheData, Node, Primitives.Num(), Primitives, FBXNodeToMerge.JointsWeightsMap, FBXNodeToMerge.JointsWeightsGroups, SkeletalMeshMaterialsConfig))
		{
			return false;
		}
	}

	// ensure only non-empty primitives are added
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.Indices.Num() > 0)
		{
			RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
		}
	}

	return true;
}

bool UglTFRuntimeFBXFunctionLibrary::LoadAndMergeFBXAsRuntimeLODBySkinDeformer(UglTFRuntimeAsset* Asset, const int32 SkinDeformerIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig)
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

	if (SkinDeformerIndex >= RuntimeFBXCacheData->Scene->skin_deformers.count)
	{
		return false;
	}

	TMap<uint32, TArray<TPair<int32, float>>> JointsWeightsMap;
	int32 JointsWeightsGroups = 1;

	if (!FillFBXSkinDeformer(Asset, RuntimeFBXCacheData->Scene->skin_deformers.data[SkinDeformerIndex], RuntimeLOD.Skeleton, JointsWeightsMap, JointsWeightsGroups))
	{
		return false;
	}

	struct FglTFRuntimeFBXNodeToMerge
	{
		FglTFRuntimeFBXNode FBXNode;
		TArray<FglTFRuntimeBone> Skeleton;
		TMap<uint32, TArray<TPair<int32, float>>> JointsWeightsMap;
		int32 JointsWeightsGroups;
	};

	auto SkinDeformerIsCompatible = [](const TArray<FglTFRuntimeBone>& BaseSkeleton, const TArray<FglTFRuntimeBone>& CurrentSkeleton)
		{
			if (BaseSkeleton.Num() != CurrentSkeleton.Num())
			{
				return false;
			}

			for (int32 BoneIndex = 0; BoneIndex < BaseSkeleton.Num(); BoneIndex++)
			{
				if (BaseSkeleton[BoneIndex].ParentIndex != CurrentSkeleton[BoneIndex].ParentIndex)
				{
					return false;
				}

				if (BaseSkeleton[BoneIndex].BoneName != CurrentSkeleton[BoneIndex].BoneName)
				{
					return false;
				}
			}

			return true;
		};

	const TArray<FglTFRuntimeFBXNode> FBXNodes = GetFBXNodesMeshes(Asset);

	if (FBXNodes.Num() < 1)
	{
		return false;
	}

	TArray<FglTFRuntimeFBXNodeToMerge> NodesToMerge;

	for (const FglTFRuntimeFBXNode& FBXNode : FBXNodes)
	{
		if (!RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
		{
			continue;
		}

		ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id];

		ufbx_mesh* Mesh = Node->mesh;
		if (!Mesh)
		{
			continue;
		}

		// skeletal mesh ?
		if (Mesh->skin_deformers.count < 1)
		{
			continue;
		}

		FglTFRuntimeFBXNodeToMerge FBXNodeToMerge;
		FBXNodeToMerge.FBXNode = FBXNode;
		FBXNodeToMerge.JointsWeightsGroups = 1;

		if (!FillFBXSkinDeformer(Asset, Mesh->skin_deformers.data[0], FBXNodeToMerge.Skeleton, FBXNodeToMerge.JointsWeightsMap, FBXNodeToMerge.JointsWeightsGroups))
		{
			continue;
		}

		if (!SkinDeformerIsCompatible(RuntimeLOD.Skeleton, FBXNodeToMerge.Skeleton))
		{
			continue;
		}

		NodesToMerge.Add(MoveTemp(FBXNodeToMerge));
	}

	if (NodesToMerge.Num() < 1)
	{
		return false;
	}

	TArray<FglTFRuntimePrimitive> Primitives;

	for (const FglTFRuntimeFBXNodeToMerge& FBXNodeToMerge : NodesToMerge)
	{
		ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNodeToMerge.FBXNode.Id];

		if (!FillFBXPrimitives(Asset, RuntimeFBXCacheData, Node, Primitives.Num(), Primitives, FBXNodeToMerge.JointsWeightsMap, FBXNodeToMerge.JointsWeightsGroups, SkeletalMeshMaterialsConfig))
		{
			return false;
		}
	}

	// ensure only non-empty primitives are added
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.Indices.Num() > 0)
		{
			RuntimeLOD.Primitives.Add(MoveTemp(Primitive));
		}
	}

	return true;

}

bool UglTFRuntimeFBXFunctionLibrary::FillFBXPrimitives(UglTFRuntimeAsset* Asset, TSharedPtr<FglTFRuntimeFBXCacheData> RuntimeFBXCacheData, struct ufbx_node* Node, const int32 PrimitiveBase, TArray<FglTFRuntimePrimitive>& Primitives, const TMap<uint32, TArray<TPair<int32, float>>>& JointsWeightsMap, const int32 JointsWeightsGroups, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	ufbx_mesh* Mesh = Node->mesh;
	if (!Mesh)
	{
		return false;
	}

	uint32 NumTriangleIndices = Mesh->max_face_triangles * 3;
	TArray<uint32> TriangleIndices;
	TriangleIndices.AddUninitialized(NumTriangleIndices);

	const bool bIsSkeletal = JointsWeightsMap.Num() > 0;

	uint32 NumMaterials = Node->materials.count;
	if (NumMaterials == 0)
	{
		// fallback to the default material
		NumMaterials = 1;
	}

	Primitives.AddDefaulted(NumMaterials);

	TMap<FString, ufbx_blend_shape*> MorphTargets;

	for (uint32 BlendDeformerIndex = 0; BlendDeformerIndex < Mesh->blend_deformers.count; BlendDeformerIndex++)
	{
		for (uint32 BlendDeformerChannelIndex = 0; BlendDeformerChannelIndex < Mesh->blend_deformers.data[BlendDeformerIndex]->channels.count; BlendDeformerChannelIndex++)
		{
			MorphTargets.Add(UTF8_TO_TCHAR(Mesh->blend_deformers.data[BlendDeformerIndex]->channels.data[BlendDeformerChannelIndex]->name.data)
				, Mesh->blend_deformers.data[BlendDeformerIndex]->channels.data[BlendDeformerChannelIndex]->target_shape);
		}
	}

	for (uint32 PrimitiveIndex = PrimitiveBase; PrimitiveIndex < PrimitiveBase + NumMaterials; PrimitiveIndex++)
	{
		FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveIndex];

		if (Node->materials.count > 0)
		{
			ufbx_material* MeshMaterial = Mesh->materials.data[PrimitiveIndex - PrimitiveBase];

			if (!MaterialsConfig.bSkipLoad)
			{
				Primitive.Material = glTFRuntimeFBX::LoadMaterial(Asset, RuntimeFBXCacheData.ToSharedRef(), MeshMaterial, MaterialsConfig, Primitive.MaterialName);
			}
		}

		if (bIsSkeletal)
		{
			for (int32 JWIndex = 0; JWIndex < JointsWeightsGroups; JWIndex++)
			{
				Primitive.Joints.AddDefaulted();
				Primitive.Weights.AddDefaulted();
			}

			Primitive.bHighPrecisionWeights = true;
		}

		if (Mesh->vertex_uv.exists)
		{
			Primitive.UVs.AddDefaulted();
		}

		for (const TPair<FString, ufbx_blend_shape*>& Pair : MorphTargets)
		{
			FglTFRuntimeMorphTarget MorphTarget;
			MorphTarget.Name = Pair.Key;
			Primitive.MorphTargets.Add(MoveTemp(MorphTarget));
		}
	}

	for (uint32 FaceIndex = 0; FaceIndex < Mesh->num_faces; FaceIndex++)
	{
		uint32 MaterialIndex = 0;
		if (FaceIndex < Mesh->face_material.count)
		{
			MaterialIndex = Mesh->face_material.data[FaceIndex];
		}

		if (Primitives.IsValidIndex(PrimitiveBase + MaterialIndex))
		{
			FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveBase + MaterialIndex];

			ufbx_face Face = Mesh->faces.data[FaceIndex];
			uint32 NumTriangles = ufbx_triangulate_face(TriangleIndices.GetData(), NumTriangleIndices, Mesh, Face);

			for (uint32 VertexIndex = 0; VertexIndex < NumTriangles * 3; VertexIndex++)
			{
				const uint32 Index = TriangleIndices[VertexIndex];

				if (bIsSkeletal)
				{
					for (int32 JWIndex = 0; JWIndex < JointsWeightsGroups; JWIndex++)
					{
						Primitive.Joints[JWIndex].AddZeroed();
						Primitive.Weights[JWIndex].AddZeroed();
					}

					if (JointsWeightsMap.Contains(Mesh->vertex_indices.data[Index]))
					{
						const TArray<TPair<int32, float>>& JointsWeights = JointsWeightsMap[Mesh->vertex_indices.data[Index]];
						int32 BoneIndex = 0;
						for (const TPair<int32, float>& Pair : JointsWeights)
						{
							Primitive.Joints[BoneIndex / 4].Last()[BoneIndex % 4] = Pair.Key;
							Primitive.Weights[BoneIndex / 4].Last()[BoneIndex % 4] = Pair.Value;
							BoneIndex++;
						}
					}
				}

				const ufbx_vec3 Position = ufbx_get_vertex_vec3(&Mesh->vertex_position, Index);

				if (bIsSkeletal)
				{
					Primitive.Positions.Add(glTFRuntimeFBX::GetTransform(Asset, Node->local_transform).TransformPosition(Asset->GetParser()->TransformPosition(FVector(Position.x, Position.y, Position.z))));
				}
				else
				{
					if (Node->has_geometry_transform)
					{
						Primitive.Positions.Add(glTFRuntimeFBX::GetTransform(Asset, Node->geometry_transform).TransformPosition(Asset->GetParser()->TransformPosition(FVector(Position.x, Position.y, Position.z))));
					}
					else
					{
						Primitive.Positions.Add(Asset->GetParser()->TransformPosition(FVector(Position.x, Position.y, Position.z)));
					}
				}

				for (FglTFRuntimeMorphTarget& MorphTarget : Primitive.MorphTargets)
				{
					ufbx_blend_shape* BlendShape = MorphTargets[MorphTarget.Name];
					const ufbx_vec3 MorphTargetPosition = ufbx_get_blend_shape_vertex_offset(BlendShape, Mesh->vertex_indices.data[Index]);
					MorphTarget.Positions.Add(Asset->GetParser()->TransformPosition(FVector(MorphTargetPosition.x, MorphTargetPosition.y, MorphTargetPosition.z)));
				}

				if (Mesh->vertex_normal.exists)
				{
					const ufbx_vec3 Normal = ufbx_get_vertex_vec3(&Mesh->vertex_normal, Index);
					if (bIsSkeletal)
					{
						Primitive.Normals.Add(glTFRuntimeFBX::GetTransform(Asset, Node->local_transform).TransformVector(Asset->GetParser()->TransformVector(FVector(Normal.x, Normal.y, Normal.z))));
					}
					else
					{
						Primitive.Normals.Add(Asset->GetParser()->TransformVector(FVector(Normal.x, Normal.y, Normal.z)));
					}
				}

				if (Mesh->vertex_uv.exists)
				{
					const ufbx_vec2 UV = ufbx_get_vertex_vec2(&Mesh->vertex_uv, Index);
					Primitive.UVs[0].Add(FVector2D(UV.x, 1 - UV.y));
				}

				if (Mesh->vertex_color.exists)
				{
					const ufbx_vec4 Color = ufbx_get_vertex_vec4(&Mesh->vertex_color, Index);
					Primitive.Colors.Add(FVector4(Color.x, Color.y, Color.z, Color.w));
				}

				Primitive.Indices.Add(Primitive.Positions.Num() - 1);
			}
		}
	}

	return true;
}

int32 UglTFRuntimeFBXFunctionLibrary::GetFBXSkinDeformersNum(UglTFRuntimeAsset* Asset)
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

	return static_cast<int32>(RuntimeFBXCacheData->Scene->skin_deformers.count);
}

ULightComponent* UglTFRuntimeFBXFunctionLibrary::LoadFBXLight(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig)
{
	if (!Asset)
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

	if (!RuntimeFBXCacheData->NodesMap.Contains(FBXNode.Id))
	{
		return nullptr;
	}

	ufbx_node* Node = RuntimeFBXCacheData->NodesMap[FBXNode.Id];

	if (!Node->light)
	{
		return nullptr;
	}

	ufbx_light_type LightType = Node->light->type;

	if (LightType == ufbx_light_type::UFBX_LIGHT_POINT)
	{
		UPointLightComponent* PointLight = NewObject<UPointLightComponent>(Actor);
		PointLight->SetLightColor(FLinearColor(Node->light->color.x, Node->light->color.y, Node->light->color.z));

		PointLight->SetIntensityUnits(ELightUnits::Candelas);
		PointLight->bUseInverseSquaredFalloff = true;

		const double Intensity = Node->light->intensity;
		PointLight->SetIntensity(Intensity);

		const float DefaultAttenuation = Intensity * LightConfig.DefaultAttenuationMultiplier;

		PointLight->SetAttenuationRadius(DefaultAttenuation * Asset->GetParser()->GetSceneScale());

		return PointLight;
	}
	else if (LightType == ufbx_light_type::UFBX_LIGHT_DIRECTIONAL)
	{
		UDirectionalLightComponent* DirectionalLight = NewObject<UDirectionalLightComponent>(Actor);
		DirectionalLight->SetLightColor(FLinearColor(Node->light->color.x, Node->light->color.y, Node->light->color.z));

		const double Intensity = Node->light->intensity;
		DirectionalLight->SetIntensity(Intensity);

		return DirectionalLight;
	}
	else if (LightType == ufbx_light_type::UFBX_LIGHT_SPOT)
	{
		USpotLightComponent* SpotLight = NewObject<USpotLightComponent>(Actor);
		SpotLight->SetLightColor(FLinearColor(Node->light->color.x, Node->light->color.y, Node->light->color.z));

		SpotLight->SetIntensityUnits(ELightUnits::Candelas);
		SpotLight->bUseInverseSquaredFalloff = true;

		const double Intensity = Node->light->intensity;
		SpotLight->SetIntensity(Intensity);

		const float DefaultAttenuation = Intensity * LightConfig.DefaultAttenuationMultiplier;

		SpotLight->SetAttenuationRadius(DefaultAttenuation * Asset->GetParser()->GetSceneScale());


		const float InnerConeAngle = Node->light->inner_angle;
		const float OuterConeAngle = Node->light->outer_angle;

		SpotLight->SetInnerConeAngle(FMath::RadiansToDegrees(InnerConeAngle));
		SpotLight->SetOuterConeAngle(FMath::RadiansToDegrees(OuterConeAngle));

		return SpotLight;
	}

	return nullptr;
}