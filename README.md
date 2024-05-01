# FBX Loader for glTFRuntime

This is an extension for glTFRuntime allowing the load of FBX assets.

![HeatFBX](https://github.com/rdeioris/glTFRuntimeFBX/assets/2234592/ba4ec16f-f5b7-4f31-8136-c5ad16ddab08)

Currently the following features are supported:

* Static Meshes
* Hierarchies
* Skeletal Meshes with unlimited influences
* Morph Targets
* Skeletal Animations (with or without Mesh specified in the asset)
* MorphTarget Animations
* Vertex Colors
* PBR Material (still far from perfect)

Work in progress:

* LOD Groups
* Cameras
* Lights
* Async functions
* Improved material handling
* Raw curves extraction
* Animation retargeting
* Load StaticMeshes as Skeletal (for supporting MorphTargets without a Skeleton)

This plugin makes use of the amazing ufbx (https://github.com/ufbx/ufbx) library for the parsing.

## Installation

* You need an Unreal C++ project (otherwise you will not be able to compile this plugin).
* You need glTFRuntime enabled in your project (at least version 20240427)
* Clone the master branch of this repository in the Plugins/ directory of your project
* Regenerate the solution
* Enjoy!

## Usage

The easiest way is to use the provided Actor, ```glTFRuntimeFBXAssetActor``` that contains the logic for building a scene by iterating the nodes as well as playing animations.

The Actor expects a glTFRuntimeAsset as input. Those objects can be generated by the various glTFRuntime loaders (File, URLs, strings, commands, clipboard...) but requires the 'Blob' mode (that disables the GLTF parsing):

![image](https://github.com/rdeioris/glTFRuntimeFBX/assets/2234592/740944ae-df2b-4221-8421-5cf2523e599f)

As with other glTFRuntime extensions, the loaders can process FBX files compressed as gzip, lz4 or as zip archives (generally the most common way if you want to bundle the external textures in a single file).

Currently loading textures from external files is not supported for security reasons (The FBX format has a weird/risky way of dealing with textures paths...)

Once you have the glTFRuntimeAsset object, you can pass it to a standard Spawn Actor From Class node (or the World->SpawnActor C++ method):

![image](https://github.com/rdeioris/glTFRuntimeFBX/assets/2234592/1fd957de-f119-475d-8110-de77cd0abb48)

## Functions

For more advanced users lower-level functions as provided in a Blueprint Function Library: ```UglTFRuntimeFBXFunctionLibrary```

The glTFRuntimeFBXAssetActor uses those functions to implement its whole logic.

There are two main 'Objects' (well, actually they are Structs) to deal with:

* FBXNode (FglTFRuntimeFBXNode): represents a node in the FBX asset, it is the basic block for the hiearchy (nodes can be plain transforms/locators, meshes, skeletons, bones, cameras, lights...). It exposes a name.
* FBXAnim (FglTFRuntimeFBXAnim): represents an animation, multiple animations can be exposed in the same asset. It exposes a name and the duration of the animation (in seconds).

```cpp
static FglTFRuntimeFBXNode GetFBXRootNode(UglTFRuntimeAsset* Asset);
```

Returns the so called "root node" (the initial node in the tree from which you can start iterating the graph)

```cpp
static TArray<FglTFRuntimeFBXNode> GetFBXNodes(UglTFRuntimeAsset* Asset);
```

Returns all of the nodes in the asset.

```cpp
static TArray<FglTFRuntimeFBXNode> GetFBXNodesMeshes(UglTFRuntimeAsset* Asset);
```

Returns all of the nodes containing a mesh.

```cpp
static TArray<FglTFRuntimeFBXNode> GetFBXNodeChildren(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode);
```

Get the children of a node.

```cpp
static TArray<FglTFRuntimeFBXAnim> GetFBXAnimations(UglTFRuntimeAsset* Asset);
```

Returns all the animations in the asset.

```cpp
static bool GetFBXDefaultAnimation(UglTFRuntimeAsset* Asset, FglTFRuntimeFBXAnim& FBXAnim);
```

An asset can specify a default animation. If available this function will return it in the FBXAnim parameter. (otherwise it returns 'false')

```cpp
static bool IsFBXNodeBone(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode);
```

Returns true if the specified node is a bone.

```cpp
static bool LoadFBXAsRuntimeLODByNode(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXNode& FBXNode, FglTFRuntimeMeshLOD& RuntimeLOD, bool& bIsSkeletal, const FglTFRuntimeMaterialsConfig& StaticMeshMaterialsConfig, const FglTFRuntimeMaterialsConfig& SkeletalMeshMaterialsConfig);
```

Generates a glTFRuntime MeshLOD from an FBXNode. It is the basic structure for generating Static and SkeletalMeshes:

![image](https://github.com/rdeioris/glTFRuntimeFBX/assets/2234592/1519f782-9cb0-487e-8b7f-fd0cab48a500)


```cpp
static UAnimSequence* LoadFBXAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, const FglTFRuntimeFBXNode& FBXNode, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
```

Generates an Animation Asset (supporting both bones and morph targets) starting from an FBXAnim. The Animation curves are generated for the specified FBXNode mesh and the related SkeletalMesh.

This function assumes you are extracting both the mesh and the animation from the same asset. If you need to extract an animation from a different FXB files (like the mixamo 'unskinned' ones) you can use the LoadFBXExternalAnimAsSkeletalMeshAnimation variant.

```cpp
static UAnimSequence* LoadFBXExternalAnimAsSkeletalMeshAnimation(UglTFRuntimeAsset* Asset, const FglTFRuntimeFBXAnim& FBXAnim, USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
```

Generates an Animation Asset (bones only) from an 'unskinned' FBX (animation curves without related meshes):

![image](https://github.com/rdeioris/glTFRuntimeFBX/assets/2234592/92a8e4c0-9f98-4045-9989-a7f51c666546)

The system assumes that every animation curve has the same name of a bone in the provided SkeletalMesh asset. 

## Materials handling

## Support

The 'fbx' channel on the glTFRuntime discord server (https://discord.gg/DzS7MHy)

## Special Thanks

HEAT (https://heat.tech/) for providing the test assets and supporting the development.
