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
	static TArray<FglTFRuntimeFBXAnim> GetFBXAnimations(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static bool GetFBXDefaultAnimation(UglTFRuntimeAsset* Asset, FglTFRuntimeFBXAnim& FBXAnim);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static bool IsFBXNodeBone(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "StaticMeshMaterialsConfig,SkeletalMeshMaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeMeshLOD& RuntimeLOD, bool& bIsSkeletal, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime|FBX")
	static UAnimSequence* LoadFBXExternalAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
};
