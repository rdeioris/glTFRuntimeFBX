// Copyright 2023 - Roberto De Ioris

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
	int64 ID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime|FBX")
	bool bHasMesh;
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
	static TArray<FString> GetFBXNodeNames(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<FglTFRuntimeFBXNode> GetFBXNodes(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime|FBX")
	static TArray<int64> GetFBXNodeIDs(UglTFRuntimeAsset* Asset);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& Node, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime|FBX")
	static bool LoadFBXAsRuntimeLODByNodeId(UglTFRuntimeAsset* Asset, const int64 NodeId, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
};
