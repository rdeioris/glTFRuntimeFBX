// Copyright 2023-2024 - Roberto De Ioris


#include "glTFRuntimeFBXAssetActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"

// Sets default values
AglTFRuntimeFBXAssetActor::AglTFRuntimeFBXAssetActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	DefaultAnimation = EglTFRuntimeFBXAssetActorDefaultAnimation::Default;
	bDefaultAnimationLoop = true;
	bLoadLights = true;

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

	FglTFRuntimeFBXNode RootFBXNode = UglTFRuntimeFBXFunctionLibrary::GetFBXRootNode(Asset);

	for (const FglTFRuntimeFBXNode& ChildNode : UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(Asset, RootFBXNode))
	{
		ProcessNode(RootComponent, ChildNode, NAME_None);
	}

	// attach to skeletons
	for (const TPair<USceneComponent*, FName>& Pair : DiscoveredAttachments)
	{
		for (const TPair<USkeletalMeshComponent*, FglTFRuntimeFBXNode>& PairSkeletalMesh : DiscoveredSkeletalMeshes)
		{
			if (PairSkeletalMesh.Key->DoesSocketExist(Pair.Value))
			{
				Pair.Key->AttachToComponent(PairSkeletalMesh.Key, FAttachmentTransformRules::KeepRelativeTransform, Pair.Value);
				break;
			}
		}
	}

	if (DefaultAnimation != EglTFRuntimeFBXAssetActorDefaultAnimation::None)
	{
		const TArray<FglTFRuntimeFBXAnim> Animations = UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(Asset);
		if (Animations.Num() > 0)
		{
			if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::Default)
			{
				FglTFRuntimeFBXAnim DefaultFBXAnim;
				if (UglTFRuntimeFBXFunctionLibrary::GetFBXDefaultAnimation(Asset, DefaultFBXAnim))
				{
					PlayFBXAnimation(DefaultFBXAnim, bDefaultAnimationLoop);
				}
			}
			else if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::First)
			{
				PlayFBXAnimation(Animations[0], bDefaultAnimationLoop);
			}
			else if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::Last)
			{
				PlayFBXAnimation(Animations.Last(), bDefaultAnimationLoop);
			}
			else if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::Random)
			{
				PlayFBXAnimation(Animations[FMath::RandRange(0, Animations.Num() - 1)], bDefaultAnimationLoop);
			}
			else if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::Shortest)
			{
				float BestDuration = MAX_FLT;
				FglTFRuntimeFBXAnim BestFBXAnim;
				for (const FglTFRuntimeFBXAnim& FBXAnim : UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(Asset))
				{
					if (FBXAnim.Duration < BestDuration)
					{
						BestDuration = FBXAnim.Duration;
						BestFBXAnim = FBXAnim;
					}
				}
				PlayFBXAnimation(BestFBXAnim, bDefaultAnimationLoop);
			}
			else if (DefaultAnimation == EglTFRuntimeFBXAssetActorDefaultAnimation::Longest)
			{
				float BestDuration = 0;
				FglTFRuntimeFBXAnim BestFBXAnim;
				for (const FglTFRuntimeFBXAnim& FBXAnim : UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(Asset))
				{
					if (FBXAnim.Duration > BestDuration)
					{
						BestDuration = FBXAnim.Duration;
						BestFBXAnim = FBXAnim;
					}
				}
				PlayFBXAnimation(BestFBXAnim, bDefaultAnimationLoop);
			}
		}
	}


	UE_LOG(LogGLTFRuntime, Log, TEXT("Asset loaded in %f seconds"), FPlatformTime::Seconds() - LoadingStartTime);

	ReceiveOnScenesLoaded();
}

// Called every frame
void AglTFRuntimeFBXAssetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AglTFRuntimeFBXAssetActor::ProcessNode(USceneComponent* CurrentParentComponent, const FglTFRuntimeFBXNode& FBXNode, const FName SocketName)
{
	if (UglTFRuntimeFBXFunctionLibrary::IsFBXNodeBone(Asset, FBXNode))
	{
		const FName NewSocketName = *FBXNode.Name;
		for (const FglTFRuntimeFBXNode& ChildNode : UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(Asset, FBXNode))
		{
			ProcessNode(CurrentParentComponent, ChildNode, NewSocketName);
		}

		return;
	}

	USceneComponent* SceneComponent = nullptr;
	if (FBXNode.bHasMesh)
	{
		FglTFRuntimeMeshLOD LOD;
		bool bIsSkeletal = false;
		if (UglTFRuntimeFBXFunctionLibrary::LoadFBXAsRuntimeLODByNode(Asset, FBXNode, LOD, bIsSkeletal, StaticMeshConfig.MaterialsConfig, SkeletalMeshConfig.MaterialsConfig))
		{
			if (bIsSkeletal)
			{
				USkeletalMeshComponent* NewSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, GetSafeNodeName<USkeletalMeshComponent>(FBXNode));
#ifdef GLTFRUNTIME_HAS_BONE_REMAPPER_LOD
				Asset->GetParser()->RemapRuntimeLODBoneNames(LOD, SkeletalMeshConfig.SkeletonConfig);
#endif
				USkeletalMesh* SkeletalMesh = Asset->LoadSkeletalMeshFromRuntimeLODs({ LOD }, -1, SkeletalMeshConfig);
				if (SkeletalMesh)
				{
					NewSkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
					DiscoveredSkeletalMeshes.Add(TPair<USkeletalMeshComponent*, FglTFRuntimeFBXNode>(NewSkeletalMeshComponent, FBXNode));
				}
				ReceiveOnSkeletalMeshComponentCreated(NewSkeletalMeshComponent);
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
				ReceiveOnStaticMeshComponentCreated(NewStaticMeshComponent);
				SceneComponent = NewStaticMeshComponent;
			}
		}
	}
	else if (bLoadLights && FBXNode.bIsLight)
	{
		SceneComponent = UglTFRuntimeFBXFunctionLibrary::LoadFBXLight(Asset, FBXNode, this, FglTFRuntimeLightConfig());
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
		if (SocketName == NAME_None)
		{
			SceneComponent->SetupAttachment(CurrentParentComponent);
		}
		else
		{
			DiscoveredAttachments.Add(TPair<USceneComponent*, FName>(SceneComponent, SocketName));
		}
		SceneComponent->SetRelativeTransform(FBXNode.Transform);
	}
	SceneComponent->RegisterComponent();

	AddInstanceComponent(SceneComponent);

	for (const FglTFRuntimeFBXNode& ChildNode : UglTFRuntimeFBXFunctionLibrary::GetFBXNodeChildren(Asset, FBXNode))
	{
		ProcessNode(SceneComponent, ChildNode, NAME_None);
	}
}

TArray<FglTFRuntimeFBXAnim> AglTFRuntimeFBXAssetActor::GetFBXAnimations() const
{
	if (!Asset)
	{
		return {};
	}

	return UglTFRuntimeFBXFunctionLibrary::GetFBXAnimations(Asset);
}

void AglTFRuntimeFBXAssetActor::PlayFBXAnimation(const FglTFRuntimeFBXAnim& FBXAnim, const bool bLoop)
{
	if (FBXAnim.Duration > 0)
	{
		for (const TPair<USkeletalMeshComponent*, FglTFRuntimeFBXNode>& Pair : DiscoveredSkeletalMeshes)
		{
#if ENGINE_MAJOR_VERSION >= 5
			UAnimSequence* NewAnimSequence = UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(Asset, FBXAnim, Pair.Value, Pair.Key->GetSkeletalMeshAsset(), SkeletalAnimationConfig);
#else
			UAnimSequence* NewAnimSequence = UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(Asset, FBXAnim, Pair.Value, Pair.Key->SkeletalMesh, SkeletalAnimationConfig);
#endif
			if (NewAnimSequence)
			{
				Pair.Key->AnimationData.AnimToPlay = NewAnimSequence;
				Pair.Key->AnimationData.bSavedLooping = bLoop;
				Pair.Key->AnimationData.bSavedPlaying = true;
				Pair.Key->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			}
		}
	}
}

UAnimSequence* AglTFRuntimeFBXAssetActor::LoadFBXAnimation(USkeletalMeshComponent* SkeletalMeshComponent, const FglTFRuntimeFBXAnim& FBXAnim)
{
	if (FBXAnim.Duration > 0)
	{
		for (const TPair<USkeletalMeshComponent*, FglTFRuntimeFBXNode>& Pair : DiscoveredSkeletalMeshes)
		{
			if (Pair.Key == SkeletalMeshComponent)
			{
#if ENGINE_MAJOR_VERSION >= 5
				return UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(Asset, FBXAnim, Pair.Value, SkeletalMeshComponent->GetSkeletalMeshAsset(), SkeletalAnimationConfig);
#else
				return UglTFRuntimeFBXFunctionLibrary::LoadFBXAnimAsSkeletalMeshAnimation(Asset, FBXAnim, Pair.Value, SkeletalMeshComponent->SkeletalMesh, SkeletalAnimationConfig);
#endif
			}
		}
	}
	return nullptr;
}

UAnimSequence* AglTFRuntimeFBXAssetActor::LoadFBXAnimationByName(USkeletalMeshComponent* SkeletalMeshComponent, const FString& AnimationName)
{
	for (const FglTFRuntimeFBXAnim& FBXAnim : GetFBXAnimations())
	{
		if (FBXAnim.Name == AnimationName)
		{
			return LoadFBXAnimation(SkeletalMeshComponent, FBXAnim);
		}
	}

	return nullptr;
}

void AglTFRuntimeFBXAssetActor::ReceiveOnStaticMeshComponentCreated_Implementation(UStaticMeshComponent* StaticMeshComponent)
{

}

void AglTFRuntimeFBXAssetActor::ReceiveOnSkeletalMeshComponentCreated_Implementation(USkeletalMeshComponent* SkeletalMeshComponent)
{

}

void AglTFRuntimeFBXAssetActor::ReceiveOnScenesLoaded_Implementation()
{

}