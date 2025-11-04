// Copyright 2023-2024 - Roberto De Ioris

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFBXFunctionLibrary.generated.h"

USTRUCT(BlueprintType)
struct FglTFRuntimeFBXNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	bool bHasMesh = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	bool bIsLight = false;

	uint32 Id = 0;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeFBXAnim
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	float Duration = 0;

	uint32 Id = 0;
};

/**
 * 
 */
UCLASS()
class GLTFRUNTIMEFBX_API UglTFRuntimeFBXFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static FglTFRuntimeFBXNode GetFBXRootNode(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<FglTFRuntimeFBXNode> GetFBXNodes(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<FglTFRuntimeFBXNode> GetFBXNodesMeshes(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<FglTFRuntimeFBXNode> GetFBXNodeChildren(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static bool GetFBXNodeParent(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeFBXNode& FBXParentNode);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<FglTFRuntimeFBXAnim> GetFBXAnimations(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static bool GetFBXDefaultAnimation(UglTFRuntimeAsset* Asset, FglTFRuntimeFBXAnim& FBXAnim);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static bool IsFBXNodeBone(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "StaticMeshMaterialsConfig,SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeMeshLOD& RuntimeLOD, bool& bIsSkeletal, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadAndMergeFBXAsRuntimeLODBySkinDeformer(UglTFRuntimeAsset* Asset, const int32 SkinDeformerIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadAndMergeFBXAsRuntimeLODByBiggestSkinDeformer(UglTFRuntimeAsset* Asset, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadAndMergeFBXAsRuntimeLODsGroupBySkinDeformer(UglTFRuntimeAsset* Asset, TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXExternalAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXExternalAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXRawAnimAsSkeletalAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeleton* Skeleton, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static int32 GetFBXSkinDeformersNum(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, meta = (AutoCreateRefTerm = "StaticMeshMaterialsConfig,SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static ULightComponent* LoadFBXLight(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig);


	static bool FillFBXPrimitives(UglTFRuntimeAsset* Asset, TSharedPtr<struct FglTFRuntimeFBXCacheData> RuntimeFBXCacheData, struct ufbx_node* Node, const int32 PrimitiveBase, TArray<FglTFRuntimePrimitive>& Primitives, const TMap<uint32, TArray<TPair<int32, float>>>& JointsWeightsMap, const int32 JointsWeightsGroups, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	static bool FillFBXSkinDeformer(UglTFRuntimeAsset* Asset, struct ufbx_skin_deformer* SkinDeformer, TArray<FglTFRuntimeBone>& Skeleton, TMap<uint32, TArray<TPair<int32, float>>>& JointsWeightsMap, int32& JointsWeightsGroups);
};
