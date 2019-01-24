/* Copyright (C) Itseez3D, Inc. - All Rights Reserved
* You may not use this file except in compliance with an authorized license
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* UNLESS REQUIRED BY APPLICABLE LAW OR AGREED BY ITSEEZ3D, INC. IN WRITING, SOFTWARE DISTRIBUTED UNDER THE LICENSE IS DISTRIBUTED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED
* See the License for the specific language governing permissions and limitations under the License.
* Written by Itseez3D, Inc. <support@itseez3D.com>, April 2017
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Runtime/Online/HTTP/Public/Http.h"

#include "GameAvatar.generated.h"


UCLASS()
class AVATARSDKSAMPLE_API AGameAvatar : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AGameAvatar();

	// Called every frame
	virtual void Tick(float DeltaTime) override;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "AvatarSDK")
	void GenerateAvatar();

private:
	void SetCommonHeaders(const TSharedRef<IHttpRequest> &req) const;
	TSharedRef<IHttpRequest> GetRequest(const FString &url);
	TSharedRef<IHttpRequest> PostRequest(const FString &url, class MultipartRequestBody &form);
	static bool HandleResponse(FHttpResponsePtr response, bool bWasSuccessful);
	static TSharedPtr<class FJsonObject> HandleJsonResponse(FHttpResponsePtr response, bool bWasSuccessful);
	static TSharedPtr<class FJsonValue> HandleJsonArrayResponse(FHttpResponsePtr response, bool bWasSuccessful);
	static const TArray<uint8> & HandleDataResponse(FHttpResponsePtr response, bool bWasSuccessful, bool &bIsOk);

	void Authorize();
	void OnAuthorizationCompleted(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful);

	void RegisterPlayer();
	void OnPlayerRegistered(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful);

	void CreateAvatarWithPhotoFromWeb(const FString &url);
	void CreateAvatarWithPhotoFilesystem();
	void OnPhotoUploaded(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful);

	void CheckAvatarStatus();
	void OnAvatarStatusRequested(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful);

	void DownloadHeadMesh();
	void DownloadHeadTexture();

	void DisplayAvatar();

	void GetHaircuts();
	void OnHaircutsRequested(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful);

	void DownloadHaircutMesh();
	void DownloadHaircutTexture();
	void DownloadHaircutPoints();

	void DisplayHaircut();

private:
	class FHttpModule *http;

	FTimerHandle awaitTimer;

	TSharedPtr<struct AvatarData> currAvatar;
	FString meshPath, texturePath;

	TSharedPtr<struct HaircutData> currHaircut;
	bool haircutMeshDownloaded = false, haircutTextureDownloaded = false, haircutPointsDownloaded = false;

	// authentication data
	FString tokenType, accessToken, playerUID;

	// UE objects
	UPROPERTY(VisibleAnywhere, Category = "AvatarSDK")
	class USceneComponent *avatarComponent;
	UPROPERTY(VisibleAnywhere, Category = "AvatarSDK")
	class UProceduralMeshComponent *headMesh;
	UPROPERTY(VisibleAnywhere, Category = "AvatarSDK")
	class UProceduralMeshComponent *haircutMesh;


	UPROPERTY(EditAnywhere, Category = "AvatarSDK")
	class UMaterialInterface *headMaterial;
	UPROPERTY(EditAnywhere, Category = "AvatarSDK")
	class UMaterialInterface *hairMaterial;
};
