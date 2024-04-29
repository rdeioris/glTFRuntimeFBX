// Copyright 2023-2024 - Roberto De Ioris


#include "glTFRuntimeFBXAssetActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"

// Sets default values
AglTFRuntimeFBXAssetActor::AglTFRuntimeFBXAssetActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;
}

// Called when the game starts or when spawned
void AglTFRuntimeFBXAssetActor::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset)
	{
		return;
	}

	double LoadingStartTime = FPlatformTime::Seconds();

	float BestDuration = 0;
	FglTFRuntimeFBXAnim BestFBXAnim;
	for (const FglTFRuntimeFBXAnim& FBXAnim : UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(Asset))
	{
		//if (FBXAnim.Duration > BestDuration)
		{
			BestDuration = FBXAnim.Duration;
			BestFBXAnim = FBXAnim;
		}
	}

	FglTFRuntimeFBXNode RootFBXNode = UglTFRuntimeFBXFunctionLibrary::GetFBXRootNode(Asset);

	for (const FglTFRuntimeFBXNode& ChildNode : UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(Asset, RootFBXNode))
	{
		ProcessNode(RootComponent, ChildNode, BestFBXAnim);
	}

	UE_LOG(LogGLTFRuntime, Log, TEXT("Asset loaded in %f seconds"), FPlatformTime::Seconds() - LoadingStartTime);
}

// Called every frame
void AglTFRuntimeFBXAssetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AglTFRuntimeFBXAssetActor::ProcessNode(USceneComponent* CurrentParentComponent, const FglTFRuntimeFBXNode& FBXNode, const FglTFRuntimeFBXAnim& FBXAnim)
{
	USceneComponent* SceneComponent = nullptr;
	if (FBXNode.bHasMesh)
	{
		FglTFRuntimeMeshLOD LOD;
		if (UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNode(Asset, FBXNode, LOD, StaticMeshConfig.MaterialsConfig, SkeletalMeshConfig.MaterialsConfig))
		{
			if (LOD.Skeleton.Num() > 0)
			{
				USkeletalMeshComponent* NewSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, GetSafeNodeName<USkeletalMeshComponent>(FBXNode));
				USkeletalMesh* SkeletalMesh = Asset->LoadSkeletalMeshFromRuntimeLODs({ LOD }, -1, SkeletalMeshConfig);
				if (SkeletalMesh)
				{
					NewSkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);

					if (FBXAnim.Duration > 0)
					{
						UAnimSequence* NewAnimSequence = UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(Asset, FBXAnim, FBXNode, SkeletalMesh, SkeletalAnimationConfig);
						if (NewAnimSequence)
						{
							NewSkeletalMeshComponent->AnimationData.AnimToPlay = NewAnimSequence;
							NewSkeletalMeshComponent->AnimationData.bSavedLooping = true;
							NewSkeletalMeshComponent->AnimationData.bSavedPlaying = true;
							NewSkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
						}
					}
				}
				SceneComponent = NewSkeletalMeshComponent;
			}
			else
			{
				UStaticMeshComponent* NewStaticMeshComponent = NewObject<UStaticMeshComponent>(this, GetSafeNodeName<UStaticMeshComponent>(FBXNode));
				UStaticMesh* StaticMesh = Asset->LoadStaticMeshFromRuntimeLODs({ LOD }, StaticMeshConfig);
				if (StaticMesh)
				{
					NewStaticMeshComponent->SetStaticMesh(StaticMesh);
				}
				SceneComponent = NewStaticMeshComponent;
			}
		}
	}

	if (!SceneComponent)
	{
		SceneComponent = NewObject<USceneComponent>(this, GetSafeNodeName<USceneComponent>(FBXNode));
	}

	// skeletal meshes have the node transform baked in
	if (SceneComponent->IsA<USkeletalMeshComponent>())
	{
		SceneComponent->SetupAttachment(GetRootComponent());
	}
	else
	{
		SceneComponent->SetupAttachment(CurrentParentComponent);
		SceneComponent->SetRelativeTransform(FBXNode.Transform);
	}
	SceneComponent->RegisterComponent();
	
	AddInstanceComponent(SceneComponent);

	for (const FglTFRuntimeFBXNode& ChildNode : UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(Asset, FBXNode))
	{
		ProcessNode(SceneComponent, ChildNode, FBXAnim);
	}
}