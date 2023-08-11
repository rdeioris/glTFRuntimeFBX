// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFBXFunctionLibrary.generated.h"

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

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime|STL")
	static bool LoadFBXAsRuntimeLODByNodeId(UglTFRuntimeAsset* Asset, const int64 NodeId, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
};
