/*
* Copyright (c) <2021> Side Effects Software Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. The name of Side Effects Software may not be used to endorse or
*    promote products derived from this software without specific prior
*    written permission.
*
* THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
* NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "HoudiniAnimationTranslator.h"

#include "UObject/TextProperty.h"

#include "HoudiniEngine.h"
#include "HoudiniEngineUtils.h"
#include "HoudiniEnginePrivatePCH.h"
#include "HoudiniInputObject.h"

#include "UnrealObjectInputRuntimeTypes.h"
#include "UnrealObjectInputUtils.h"
#include "UnrealObjectInputRuntimeUtils.h"
#include "HoudiniEngineRuntimeUtils.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"

//static void FHoudiniAnimationTranslator::IsAnimationPart(void);

bool FHoudiniAnimationTranslator::IsAnimationPart(const HAPI_NodeId& GeoId, const HAPI_NodeId& PartId)
{
	HAPI_AttributeInfo TransformInfo;
	FHoudiniApi::AttributeInfo_Init(&TransformInfo);

	HAPI_Result TransformInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		GeoId, PartId,
		"transform", HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &TransformInfo);

	HAPI_AttributeInfo LocalTransformInfo;
	FHoudiniApi::AttributeInfo_Init(&LocalTransformInfo);

	HAPI_Result LocalTransformInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		GeoId, PartId,
		"localtransform", HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &LocalTransformInfo);

	HAPI_AttributeInfo TimeInfo;
	FHoudiniApi::AttributeInfo_Init(&TimeInfo);

	HAPI_Result TimeInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		GeoId, PartId,
		"time", HAPI_AttributeOwner::HAPI_ATTROWNER_PRIM, &TimeInfo);



	return TransformInfo.exists && LocalTransformInfo.exists && TimeInfo.exists;
}

void 
FHoudiniAnimationTranslator::CreateAnimSequenceFromOutput(
	UHoudiniOutput* InOutput,
	const FHoudiniPackageParams& InPackageParams,
	UObject* InOuterComponent)
{

	//Loop over hgpo in Output cand 
	for (auto&& HGPO : InOutput->GetHoudiniGeoPartObjects())
	{
		CreateAnimationFromMotionClip(InOutput, HGPO, InPackageParams, InOuterComponent);
	}
	
}

UAnimSequence*
FHoudiniAnimationTranslator::CreateNewAnimation(FHoudiniPackageParams& InPackageParams, const FHoudiniGeoPartObject& HGPO, const FString& InSplitIdentifier)
{
	// Update the current Obj/Geo/Part/Split IDs
	InPackageParams.ObjectId = HGPO.ObjectId;
	InPackageParams.GeoId = HGPO.GeoId;
	InPackageParams.PartId = HGPO.PartId;
	InPackageParams.SplitStr = InSplitIdentifier;

	UAnimSequence* NewAnimation = InPackageParams.CreateObjectAndPackage<UAnimSequence>();

	UE_LOG(LogTemp, Log, TEXT("Animation Path %s "), *NewAnimation->GetPathName());
	if (!IsValid(NewAnimation))
		return nullptr;

	FAssetRegistryModule::AssetCreated(NewAnimation);

	return NewAnimation;

}


//Creates SkelatalMesh and Skeleton Assets and Packages, and adds them to OutputObjects
bool FHoudiniAnimationTranslator::CreateAnimationFromMotionClip(UHoudiniOutput* InOutput, const FHoudiniGeoPartObject& HGPO, const FHoudiniPackageParams& InPackageParams, UObject* InOuterComponent)
{
	FHoudiniOutputObjectIdentifier OutputObjectIdentifier(
		HGPO.ObjectId, HGPO.GeoId, HGPO.PartId, "");
	OutputObjectIdentifier.PartName = HGPO.PartName;

	FHoudiniOutputObject& OutputObject = InOutput->GetOutputObjects().FindOrAdd(OutputObjectIdentifier);
	FHoudiniPackageParams PackageParams = InPackageParams;
	UAnimSequence* NewAnimation = CreateNewAnimation(PackageParams, HGPO, OutputObjectIdentifier.SplitIdentifier);
	OutputObject.OutputObject = NewAnimation;
	OutputObject.bProxyIsCurrent = false;


	FString SkeletonAssetPathString;

	bool bSkeletonExists = FHoudiniEngineUtils::HapiCheckAttributeExists(HGPO.GeoId, HGPO.PartId, "unreal_skeleton");
	if (bSkeletonExists)  //Panel NodeSync Settings Overrides unreal_skeleton  Attribute
	{
		HAPI_AttributeInfo UnrealSkeletonInfo;
		FHoudiniApi::AttributeInfo_Init(&UnrealSkeletonInfo);
		TArray<FString> UnrealSkeletonData;
		FHoudiniEngineUtils::HapiGetAttributeDataAsString(HGPO.GeoId, HGPO.PartId, HAPI_UNREAL_ATTRIB_SKELETON, UnrealSkeletonInfo, UnrealSkeletonData);
		if (UnrealSkeletonData.Num() <= 0)
		{
			return false;
		}

		SkeletonAssetPathString = UnrealSkeletonData[0];
	}
	else
	{
		SkeletonAssetPathString = TEXT("/Script/Engine.Skeleton'/Game/Characters/Mannequin_UE4/Meshes/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton'");

	}

	const FSoftObjectPath SkeletonAssetPath(SkeletonAssetPathString);
	USkeleton* MySkeleton = nullptr;
	MySkeleton = Cast<USkeleton>(SkeletonAssetPath.TryLoad());
	if (!IsValid(MySkeleton))
	{
		return false;
	}


	//BoneNames-----------------------------------------------------------------------------------------------------------------
	HAPI_AttributeInfo BoneNameInfo;
	FHoudiniApi::AttributeInfo_Init(&BoneNameInfo);
	HAPI_Result AttributeInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		HGPO.GeoId, HGPO.PartId,
		"name", HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &BoneNameInfo);

	TArray<FString> BoneNameData;
	FHoudiniEngineUtils::HapiGetAttributeDataAsString(HGPO.GeoId, HGPO.PartId, "name", BoneNameInfo, BoneNameData);
	if (BoneNameData.Num() <= 0)
	{
		return false;
	}

	//Get unique Set of Bones
	TSet<FName> BoneNames;
	for (auto BoneString : BoneNameData)
	{
		BoneNames.Add(FName(BoneString));
	}

	//Point (WorldTransform translation)---------------------------------------------------------------------------
	HAPI_AttributeInfo PointInfo;
	FHoudiniApi::AttributeInfo_Init(&PointInfo);

	HAPI_Result PointInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		HGPO.GeoId, HGPO.PartId,
		HAPI_UNREAL_ATTRIB_POSITION, HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &PointInfo);

	TArray<FVector3f> PositionData;
	PositionData.SetNum(PointInfo.count);  //dont need * PositionInfo.tupleSize, its already a vector container
	FHoudiniApi::GetAttributeFloatData(FHoudiniEngine::Get().GetSession(), HGPO.GeoId, HGPO.PartId, HAPI_UNREAL_ATTRIB_POSITION, &PointInfo, -1, (float*)&PositionData[0], 0, PointInfo.count);

	//WorldTransform---------------------------------------------------------------------------
	HAPI_AttributeInfo WorldTransformInfo;
	FHoudiniApi::AttributeInfo_Init(&WorldTransformInfo);

	HAPI_Result WorldTransformInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		HGPO.GeoId, HGPO.PartId,
		"transform", HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &WorldTransformInfo);

	TArray<float> WorldTransformData;
	TArray<int> WorldTransformSizesFixedArray;
	//WorldTransformData.SetNum(WorldTransformInfo.totalArrayElements);
	WorldTransformData.SetNum(WorldTransformInfo.count * WorldTransformInfo.tupleSize);
	WorldTransformSizesFixedArray.SetNum(WorldTransformInfo.count);
	HAPI_Result WorldTransformDataResult = FHoudiniApi::GetAttributeFloatArrayData(FHoudiniEngine::Get().GetSession(), HGPO.GeoId, HGPO.PartId, "transform", &WorldTransformInfo, &WorldTransformData[0], WorldTransformInfo.count * WorldTransformInfo.tupleSize, &WorldTransformSizesFixedArray[0], 0, WorldTransformInfo.count);

	//LocalTransform---------------------------------------------------------------------------
	HAPI_AttributeInfo LocalTransformInfo;
	FHoudiniApi::AttributeInfo_Init(&LocalTransformInfo);

	HAPI_Result LocalTransformInfoResult = FHoudiniApi::GetAttributeInfo(
		FHoudiniEngine::Get().GetSession(),
		HGPO.GeoId, HGPO.PartId,
		"localtransform", HAPI_AttributeOwner::HAPI_ATTROWNER_POINT, &LocalTransformInfo);

	TArray<float> LocalTransformData;
	TArray<int> LocalTransformSizesFixedArray;
	//LocalTransformData.SetNum(LocalTransformInfo.totalArrayElements);
	LocalTransformData.SetNum(LocalTransformInfo.count * LocalTransformInfo.tupleSize);
	LocalTransformSizesFixedArray.SetNum(LocalTransformInfo.count);
	HAPI_Result LocalTransformDataResult = FHoudiniApi::GetAttributeFloatArrayData(FHoudiniEngine::Get().GetSession(), HGPO.GeoId, HGPO.PartId, "localtransform", &LocalTransformInfo, &LocalTransformData[0], LocalTransformInfo.count * LocalTransformInfo.tupleSize, &LocalTransformSizesFixedArray[0], 0, LocalTransformInfo.count);

	TMap <FName, TArray<FVector3f>> PosMap;
	TMap <FName, TArray<FQuat4f>> RotMap;
	TMap <FName, TArray<FVector3f>> ScaleMap;

	//Build Matrix
	int BoneCount = BoneNames.Num();
	// int FrameCount = LocalTransformData.Num() / (16 * BoneCount);
	int FrameIndex = 0;
	int BoneCounter = 0;
	TMap <int, TMap <FName, FTransform>> FrameBoneTransformMap;  //For each frame, store bone and transform

	TMap <FName, TArray<FTransform>> BoneKeyMap;  //For each Bone, store bone and transform
	TMap <FName, TArray<FVector3f>> BonePosMap;  //For each Bone, store bone and transform
	TMap <FName, TArray<FQuat4f>> BoneRotMap;  //For each Bone, store bone and transform
	TMap <FName, TArray<FVector3f>> BoneScaleMap;  //For each Bone, store bone and transform

	//TArray<FMatrix> MatrixData;
	//BoneNameData has like 4147
	const int32 StrideSize = BoneCount;
	for (int BoneDataIndex = StrideSize; BoneDataIndex < BoneNameData.Num(); BoneDataIndex++)  //For each frame, store off matrix into BoneMatrixMap
	{
		FString BoneString = BoneNameData[BoneDataIndex];
		FName BoneName = FName(BoneString);


		//Read in 3x3 into Matrix, and append the translation
		FMatrix M44Pose;  //this is unconverted houdini space
		M44Pose.M[0][0] = WorldTransformData[9 * BoneDataIndex + 0];
		M44Pose.M[0][1] = WorldTransformData[9 * BoneDataIndex + 1];
		M44Pose.M[0][2] = WorldTransformData[9 * BoneDataIndex + 2];
		M44Pose.M[0][3] = 0;
		M44Pose.M[1][0] = WorldTransformData[9 * BoneDataIndex + 3];
		M44Pose.M[1][1] = WorldTransformData[9 * BoneDataIndex + 4];
		M44Pose.M[1][2] = WorldTransformData[9 * BoneDataIndex + 5];
		M44Pose.M[1][3] = 0;
		M44Pose.M[2][0] = WorldTransformData[9 * BoneDataIndex + 6];
		M44Pose.M[2][1] = WorldTransformData[9 * BoneDataIndex + 7];
		M44Pose.M[2][2] = WorldTransformData[9 * BoneDataIndex + 8];
		M44Pose.M[2][3] = 0;
		M44Pose.M[3][0] = PositionData[BoneDataIndex].X;
		M44Pose.M[3][1] = PositionData[BoneDataIndex].Y;
		M44Pose.M[3][2] = PositionData[BoneDataIndex].Z;
		M44Pose.M[3][3] = 1;

		//FMatrix M44;
		//int32 row = 0;
		//int32 col = 0;
		//for (int32 i = 0; i < 16; i++)
		//{
		//	M44.M[row][col] = LocalTransformData[16 * BoneIndex + i];  //now need world transform instead
		//	col++;
		//	if (col > 3)
		//	{
		//		row++;
		//		col = 0;
		//	}
		//}
		//MatrixData.Add(M44);

		FTransform PoseTransform = FTransform(M44Pose);//this is in Houdini Space
		//Now convert to unreal
		FQuat PoseQ = PoseTransform.GetRotation();
		FQuat ConvertedPoseQ = FQuat(PoseQ.X, PoseQ.Z, PoseQ.Y, -PoseQ.W) * FQuat::MakeFromEuler({ 90.f, 0.f, 0.f });
		//FQuat ConvertedPoseQ = FQuat::MakeFromEuler({ -90.f, 0.f, 0.f }) * FQuat(PoseQ.X, PoseQ.Z, PoseQ.Y, -PoseQ.W);
		//FQuat ConvertedPoseQ = FQuat::MakeFromEuler({ 90.f, 0.f, 0.f }) * FQuat(PoseQ.X, PoseQ.Z, PoseQ.Y, -PoseQ.W);

		FVector PoseT = PoseTransform.GetLocation();
		FVector ConvertedPoseT = FVector(PoseT.X, PoseT.Z, PoseT.Y);
		FVector PoseS = PoseTransform.GetScale3D();
		FTransform UnrealPoseTransform = FTransform(ConvertedPoseQ, ConvertedPoseT * 100, PoseS * 100);

		//TArray<FTransform>& MatrixArray = MatrixMap.FindOrAdd(BoneName);
		//MatrixArray.Add(UnrealPoseTransform);//need to convert

		TMap <FName, FTransform>& BoneTransformMap = FrameBoneTransformMap.FindOrAdd(FrameIndex);
		BoneTransformMap.Add(BoneName, UnrealPoseTransform);

		UE_LOG(LogTemp, Log, TEXT("Adding Frame %i Bone %s BoneCounter %i "), FrameIndex, *BoneName.ToString(), BoneCounter);

		BoneCounter++;
		if (BoneCounter > BoneCount - 1)
		{
			BoneCounter = 0;
			FrameIndex++;
		}
	}


	//Second Pass to calc ParentBoneSpace Transforms
	//For each frame, loop over
	TArray<int> Frames;
	FrameBoneTransformMap.GetKeys(Frames);
	BoneCounter = 0;

	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();

	for (int Frame : Frames)
	{
		TMap <FName, FTransform>* BoneTransformMap = FrameBoneTransformMap.Find(Frame);
		for (auto& Elem : *BoneTransformMap)
		{
			const FName CurrentBoneName = Elem.Key;
			const int32 BoneRefIndex = RefSkeleton.FindBoneIndex(CurrentBoneName);
			FName ParentBoneName = CurrentBoneName;
			if (BoneRefIndex > 0)
			{
				int32 ParentBoneIndex = 0;
				ParentBoneIndex = RefSkeleton.GetParentIndex(BoneRefIndex);
				ParentBoneName = RefSkeleton.GetBoneName(ParentBoneIndex);
			}
			if (!BoneTransformMap->Contains(ParentBoneName))
			{
				continue;
			}
			FTransform ParentCSXform = *BoneTransformMap->Find(ParentBoneName);
			
			FTransform BoneCSXform = Elem.Value;
			FTransform BoneLXform = BoneCSXform * ParentCSXform.Inverse(); //Final
			//Store
			TArray<FTransform>& KeyTransformArray = BoneKeyMap.FindOrAdd(CurrentBoneName);
			KeyTransformArray.Add(BoneLXform);
			TArray<FVector3f>& KeyPosArray = BonePosMap.FindOrAdd(CurrentBoneName);
			FVector Pos = BoneLXform.GetLocation();
			KeyPosArray.Add(FVector3f(Pos));
			TArray<FQuat4f>& KeyRotArray = BoneRotMap.FindOrAdd(CurrentBoneName);
			FQuat Q = BoneLXform.GetRotation();
			KeyRotArray.Add(FQuat4f(Q));
			TArray<FVector3f>& KeyScaleArray = BoneScaleMap.FindOrAdd(CurrentBoneName);
			FVector Scale = BoneLXform.GetScale3D();
			KeyScaleArray.Add(FVector3f(Scale));
		}
	}
	

	const int32 FrameRate30 = 30;
	NewAnimation->ResetAnimation();
	NewAnimation->SetSkeleton(MySkeleton);
	NewAnimation->ImportFileFramerate = (float)FrameRate30;
	NewAnimation->ImportResampleFramerate = FrameRate30;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 2
	// Initialize data model
	IAnimationDataController& AnimController = NewAnimation->GetController();
	AnimController.InitializeModel();

	AnimController.OpenBracket(NSLOCTEXT("MyNamespace", "InitializeAnimation", "Initialize New Anim Sequence"));
	{

		// This line is to set actual frame rate
		AnimController.SetFrameRate(FFrameRate(FrameRate30, 1), true);

		//AnimController.SetPlayLength(33.0f, true);
		const int32 NumKeys = FrameIndex;
		AnimController.SetNumberOfFrames(NumKeys - 1);

		//rgc
		bool bShouldTransact = true;
		for (auto Bone : BoneNames)  //loop over all bones
		{
			//QUESTION:  What to do if no tracks for bone? Skip?
			//TODO Need to insert tracks for missing bones?
			if (BonePosMap.Contains(Bone) && BoneRotMap.Contains(Bone) && BoneScaleMap.Contains(Bone))
			{
				AnimController.AddBoneCurve(Bone, bShouldTransact);

				AnimController.SetBoneTrackKeys(Bone, *BonePosMap.Find(Bone), *BoneRotMap.Find(Bone), *BoneScaleMap.Find(Bone), bShouldTransact);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("Missing Bone %s "), Bone);
			}
		}
		
		AnimController.NotifyPopulated();
	}
	AnimController.CloseBracket();

#endif

	return true;
}

