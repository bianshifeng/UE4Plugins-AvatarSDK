/* Copyright (C) Itseez3D, Inc. - All Rights Reserved
* You may not use this file except in compliance with an authorized license
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* UNLESS REQUIRED BY APPLICABLE LAW OR AGREED BY ITSEEZ3D, INC. IN WRITING, SOFTWARE DISTRIBUTED UNDER THE LICENSE IS DISTRIBUTED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED
* See the License for the specific language governing permissions and limitations under the License.
* Written by Itseez3D, Inc. <support@itseez3D.com>, April 2017
*/


#include "GameAvatar.h"

#include <map>
#include <vector>
#include <fstream>
#include <sstream>

#if PLATFORM_IOS
	#import <Foundation/Foundation.h>
#endif

#include "TimerManager.h"
#include "EngineGlobals.h"
#include "ModuleManager.h"
#include "ProceduralMeshComponent.h"

#include <Runtime/Engine/Classes/Engine/Engine.h>
#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Engine/Classes/Materials/MaterialInterface.h"
#include "Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h"

#include "Runtime/ImageWrapper/Public/Interfaces/IImageWrapper.h"
#include "Runtime/ImageWrapper/Public/Interfaces/IImageWrapperModule.h"

#include "Runtime/Json/Public/Json.h"
#include "Runtime/JsonUtilities/Public/JsonUtilities.h"

#include "Ply.h"
#include "ZipUtils.h"


namespace
{
	const char *clientId = "";
	const char *clientSecret = "";

	void SaveTArray(const TArray<uint8> &arr, const FString &filename)
	{
		const std::string path{ TCHAR_TO_UTF8(*filename) };
		std::ofstream f{ path, std::ios::out | std::ios::binary };
		f.write((const char *)arr.GetData(), arr.Num());
	}

	/// Returns number of bytes read.
	size_t LoadTArray(const FString &filename, TArray<uint8> &arr)
	{
		const std::string path{ TCHAR_TO_UTF8(*filename) };
		std::ifstream stream{ path, std::ios::in | std::ios::binary };

		stream.seekg(0, std::ios::end);
		const auto size = stream.tellg();
		stream.seekg(0);
		arr.SetNum(size);
		if (stream.read((char *)arr.GetData(), size))
			return size;
		else
			return 0;
	}

	FString EnsureDirectoryExists(const FString &dir)
	{
#if PLATFORM_IOS
		const char *locationUTF8 = TCHAR_TO_UTF8(*dir);
		NSString *dataPath = [NSString stringWithUTF8String : locationUTF8];

		if (![[NSFileManager defaultManager] fileExistsAtPath:dataPath])
			[[NSFileManager defaultManager] createDirectoryAtPath:dataPath withIntermediateDirectories : YES attributes : nil error : nil];
#else
		auto &platformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!platformFile.DirectoryExists(*dir))
			platformFile.CreateDirectory(*dir);
#endif
		return dir;
	}

	FString DownloadLocation()
	{
#if PLATFORM_IOS
		NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
		NSString *documentsDirectory = [paths objectAtIndex : 0];
		const char *docPath = [documentsDirectory UTF8String];
		const auto location = FString(UTF8_TO_TCHAR(docPath));
#else
		const auto location = FPaths::GamePersistentDownloadDir();
#endif
		return location;
	}

	FString DownloadLocation(const FString &avatarCode)
	{
		const auto location = FPaths::Combine(DownloadLocation(), avatarCode);
		return EnsureDirectoryExists(location);
	}

	FString HaircutDownloadLocation()
	{
		const auto location = FPaths::Combine(DownloadLocation(), TEXT("haircuts"));
		return EnsureDirectoryExists(location);
	}

	enum class AvatarFile
	{
		HAIRCUT_POINTS_ZIP,
		HAIRCUT_POINTS_PLY,
	};

	enum class HaircutFile
	{
		MESH_ZIP,
		MESH,
		TEXTURE,
	};

	FString HaircutFilePath(HaircutFile file, const FString &haircutId)
	{
		static const std::map<HaircutFile, FString> ext =
		{
			{ HaircutFile::MESH_ZIP, TEXT("zip") },
			{ HaircutFile::MESH, TEXT("ply") },
			{ HaircutFile::TEXTURE, TEXT("png") },
		};
		const auto fname = FString::Printf(TEXT("%s.%s"), *haircutId, *ext.at(file));
		return FPaths::Combine(HaircutDownloadLocation(), fname);
	}

	FString HaircutAvatarFilePath(AvatarFile file, const FString &avatar, const FString &haircutId)
	{
		static const std::map<AvatarFile, FString> names =
		{
			{ AvatarFile::HAIRCUT_POINTS_ZIP, TEXT("cloud_%s.zip") },
			{ AvatarFile::HAIRCUT_POINTS_PLY, TEXT("cloud_%s.ply") },
		};
		const auto fname = FString::Printf(*names.at(file), *haircutId);
		return FPaths::Combine(DownloadLocation(avatar), fname);
	}

	FString GetRootUrl()
	{
		return "https://avatar-api.itseez3d.com";
	}

	FString Join(const FString &token)
	{
		return token;
	}

	template<typename... Args>
	FString Join(const FString &token, Args... tokens)
	{
		return token + "/" + Join(std::forward<Args>(tokens)...);
	}

	template<typename... Args>
	FString Url(Args... tokens)
	{
		return Join(GetRootUrl(), std::forward<Args>(tokens)...) + "/";
	}

	bool IsHttpCodeGood(int code)
	{
		return code >= 200 && code < 400;
	}
}

struct AvatarData
{
	AvatarData(const FJsonObject &json)
	{
		code = json.GetStringField("code");
		status = json.GetStringField("status");
		json.TryGetStringField("mesh", mesh);
		json.TryGetStringField("texture", texture);
		json.TryGetStringField("haircuts", haircuts);
		progress = json.GetIntegerField("progress");
	}

	FString code, status;
	FString mesh, texture, haircuts;
	int progress;
};

struct HaircutData
{
	HaircutData(const FJsonObject &json)
	{
		id = json.GetStringField("identity");
		json.TryGetStringField("mesh", mesh);
		json.TryGetStringField("texture", texture);
		json.TryGetStringField("pointcloud", pointCloud);
	}

	FString id;
	FString mesh, texture, pointCloud;
};

// multipart form utils
class MultipartRequestBody
{
public:
	MultipartRequestBody()
	{
		// generate multipart form boundary
		const auto guid = FGuid::NewGuid().ToString();
		boundary = std::string(TCHAR_TO_UTF8(*guid));
		separator = "\r\n--" + boundary + "\r\n";
	}

	~MultipartRequestBody()
	{
		LogBody();
	}

	void TextField(const std::string &name, const std::string &value)
	{
		body << separator;
		body << "Content-Disposition: form-data; name=\"" << name << "\"\r\n";
		body << "Content-Type: text/plain; encoding=utf-8\r\n\r\n";
		body << value;
	}

	void FileField(const std::string &name, const std::string &filename, const char *data, size_t size)
	{
		body << separator;
		body << "Content-Disposition: file; name=\"" << name << "\"; ";
		body << "filename=\"" << filename << "\"\r\n";
		body << "Content-Type: application/octet-stream\r\n\r\n";
		body.write(data, size);
	}

	void Footer()
	{
		body << "\r\n--" << boundary << "--\r\n";
	}

	TSharedRef<TArray<uint8>> GetBody() const
	{
		TSharedRef<TArray<uint8>> content{ new TArray<uint8>() };
		const auto bodyStr = body.str();
		content->Append((const uint8 *)bodyStr.c_str(), bodyStr.length());
		return content;
	}

	FString GetContentType() const
	{
		return "multipart/form-data; boundary=\"" + FString(UTF8_TO_TCHAR(boundary.c_str())) + "\"";
	}

	void LogBody() const
	{
		const FString bodyStr = UTF8_TO_TCHAR(body.str().c_str());
		UE_LOG(LogClass, Log, TEXT("content %s"), *bodyStr);
	}

private:
	std::string boundary, separator;
	std::ostringstream body;
};


// Sets default values
AGameAvatar::AGameAvatar()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	http = &FHttpModule::Get();

	avatarComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Avatar"));
	RootComponent = avatarComponent;

	headMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeadMesh"), true);
	headMesh->SetupAttachment(RootComponent);
	haircutMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HaircutMesh"), true);
	haircutMesh->SetupAttachment(RootComponent);
}

// Called when the game starts or when spawned
void AGameAvatar::BeginPlay()
{
	Super::BeginPlay();
}

void AGameAvatar::GenerateAvatar()
{
	UE_LOG(LogClass, Log, TEXT("Starting..."));
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Starting!")));
	Authorize();
}

void AGameAvatar::SetCommonHeaders(const TSharedRef<IHttpRequest> &req) const
{
	req->SetHeader("User-Agent", "X-UnrealEngineAvatarPlugin-Agent");
	if (!accessToken.IsEmpty())
		req->SetHeader("Authorization", tokenType + " " + accessToken);
	if (!playerUID.IsEmpty())
		req->SetHeader("X-PlayerUID", playerUID);
}

TSharedRef<IHttpRequest> AGameAvatar::GetRequest(const FString &url)
{
	UE_LOG(LogClass, Log, TEXT("Url %s"), *url);
	auto req = http->CreateRequest();
	req->SetURL(url);
	req->SetVerb("GET");
	SetCommonHeaders(req);
	return req;
}

TSharedRef<IHttpRequest> AGameAvatar::PostRequest(const FString &url, MultipartRequestBody &form)
{
	UE_LOG(LogClass, Log, TEXT("Url %s"), *url);
	auto req = http->CreateRequest();
	req->SetURL(url);
	req->SetVerb("POST");
	req->SetContent(form.GetBody().Get());
	req->SetHeader("Content-Type", form.GetContentType());
	SetCommonHeaders(req);
	return req;
}

bool AGameAvatar::HandleResponse(FHttpResponsePtr response, bool bWasSuccessful)
{
	const auto responseContent = response->GetContentAsString();
	const auto code = response->GetResponseCode();
	UE_LOG(LogClass, Log, TEXT("Request completed. good: %d, Content: %s, Code: %d"), int(bWasSuccessful), *responseContent, code);

	if (!bWasSuccessful || !IsHttpCodeGood(code))
	{
		UE_LOG(LogClass, Error, TEXT("Request was not successful"));
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> AGameAvatar::HandleJsonResponse(FHttpResponsePtr response, bool bWasSuccessful)
{
	TSharedPtr<FJsonObject> data;

	if (!HandleResponse(response, bWasSuccessful))
		return data;

	auto jsonReader = TJsonReaderFactory<>::Create(response->GetContentAsString());
	if (FJsonSerializer::Deserialize(jsonReader, data))
		return data;
	else
	{
		UE_LOG(LogClass, Error, TEXT("Json parsing failed!"));
		return TSharedPtr<FJsonObject>();
	}
}

TSharedPtr<FJsonValue> AGameAvatar::HandleJsonArrayResponse(FHttpResponsePtr response, bool bWasSuccessful)
{
	TSharedPtr<FJsonValue> data;
	if (!HandleResponse(response, bWasSuccessful))
		return data;
	
	auto jsonReader = TJsonReaderFactory<>::Create(response->GetContentAsString());
	if (FJsonSerializer::Deserialize(jsonReader, data))
		return data;
	else
	{
		UE_LOG(LogClass, Error, TEXT("Json array parsing failed!"));
		return TSharedPtr<FJsonValue>();
	}
}

const TArray<uint8> & AGameAvatar::HandleDataResponse(FHttpResponsePtr response, bool bWasSuccessful, bool &bIsOk)
{
	const auto code = response->GetResponseCode();
	UE_LOG(LogClass, Log, TEXT("Request completed. good: %d, Code: %d"), int(bWasSuccessful), code);

	bIsOk = true;
	if (!bWasSuccessful || !IsHttpCodeGood(code))
	{
		UE_LOG(LogClass, Error, TEXT("Data request was not successful"));
		bIsOk = false;
	}

	return response->GetContent();
}

void AGameAvatar::Authorize()
{
	MultipartRequestBody form;
	form.TextField("grant_type", "client_credentials");
	form.TextField("client_id", clientId);
	form.TextField("client_secret", clientSecret);
	form.Footer();

	auto request = PostRequest(Url("o", "token"), form);
	request->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnAuthorizationCompleted);
	request->ProcessRequest();
}

void AGameAvatar::OnAuthorizationCompleted(FHttpRequestPtr req, FHttpResponsePtr response, bool bWasSuccessful)
{
	const auto credentials = HandleJsonResponse(response, bWasSuccessful);
	if (!credentials.IsValid())
		return;

	tokenType = credentials->GetStringField("token_type");
	accessToken = credentials->GetStringField("access_token");
	UE_LOG(LogClass, Log, TEXT("Auth: %s  --  %s"), *tokenType, *accessToken);

	RegisterPlayer();
}

void AGameAvatar::RegisterPlayer()
{
	MultipartRequestBody form;
	form.TextField("comment", "test_unreal_player");
	form.Footer();

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Authorizing...")));
	auto request = PostRequest(Url("players"), form);
	request->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnPlayerRegistered);
	request->ProcessRequest();
}

void AGameAvatar::OnPlayerRegistered(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	auto playerResponse = HandleJsonResponse(response, bWasSuccessful);
	if (!playerResponse.IsValid())
		return;

	playerUID = playerResponse->GetStringField("code");

	// use CreateAvatarWithPhotoFilesystem to provide photo as a local file
	CreateAvatarWithPhotoFromWeb(TEXT("https://s3.amazonaws.com/itseez3d-unreal/test_selfie.jpg"));
}

void AGameAvatar::CreateAvatarWithPhotoFromWeb(const FString &url)
{
	UE_LOG(LogClass, Log, TEXT("photo url %s"), *url);
	auto photoRequest = http->CreateRequest();
	photoRequest->SetURL(url);
	photoRequest->SetVerb("GET");

	photoRequest->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto photoBytes = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;

		MultipartRequestBody form;
		form.TextField("name", "test_avatar_unreal");
		form.TextField("description", "test_description_unreal");

		form.FileField("photo", "photo.jpg", (const char*)photoBytes.GetData(), photoBytes.Num());
		form.Footer();

		auto avatarRequest = PostRequest(Url("avatars"), form);
		avatarRequest->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnPhotoUploaded);
		avatarRequest->ProcessRequest();
	});
	UE_LOG(LogClass, Log, TEXT("Downloading photo from web"));
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Getting photo...")));
	photoRequest->ProcessRequest();
}

void AGameAvatar::CreateAvatarWithPhotoFilesystem()
{
	MultipartRequestBody form;
	form.TextField("name", "test_avatar_unreal");
	form.TextField("description", "test_description_unreal");

	std::ifstream photoFile{ R"(C:\Users\objscan\Pictures\selfies\test_selfie.jpg)", std::ios::in | std::ios::binary };
	std::vector<char> photoBytes{ std::istreambuf_iterator<char>(photoFile), std::istreambuf_iterator<char>() };
	form.FileField("photo", "photo.jpg", photoBytes.data(), photoBytes.size());
	form.Footer();

	auto request = PostRequest(Url("avatars"), form);
	request->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnPhotoUploaded);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Uploading photo to server...")));
	request->ProcessRequest();
}

void AGameAvatar::OnPhotoUploaded(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	auto avatarResponse = HandleJsonResponse(response, bWasSuccessful);
	if (!avatarResponse.IsValid())
		return;

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Photo uploaded!")));
	currAvatar = MakeShareable(new AvatarData(*avatarResponse));
	GetWorldTimerManager().SetTimer(awaitTimer, this, &AGameAvatar::CheckAvatarStatus, 4.0f, false);
}

void AGameAvatar::CheckAvatarStatus()
{
	auto request = GetRequest(Url("avatars", currAvatar->code));
	request->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnAvatarStatusRequested);

	UE_LOG(LogClass, Log, TEXT("Updating status for avatar: %s"), *(currAvatar->code));
	request->ProcessRequest();
}

void AGameAvatar::OnAvatarStatusRequested(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	auto avatarResponse = HandleJsonResponse(response, bWasSuccessful);
	if (!avatarResponse.IsValid())
		return;

	GEngine->AddOnScreenDebugMessage(-1, 13.f, FColor::Green, FString::Printf(TEXT("Avatar calculation status: %s, progress: %d"), *(currAvatar->status), currAvatar->progress));

	currAvatar = MakeShareable(new AvatarData(*avatarResponse.Get()));
	if (currAvatar->status == "Failed" || currAvatar->status == "Timed Out")
	{
		UE_LOG(LogClass, Warning, TEXT("Avatar calculations failed with status: %s"), *currAvatar->status);
		return;
	}

	if (currAvatar->status == "Completed")
	{
		UE_LOG(LogClass, Log, TEXT("Avatar calculations finished with status: %s"), *currAvatar->status);
		DownloadHeadMesh();
		DownloadHeadTexture();
		GetHaircuts();
		return;
	}

	GetWorldTimerManager().SetTimer(awaitTimer, this, &AGameAvatar::CheckAvatarStatus, 4.0f, false);
}

void AGameAvatar::DownloadHeadMesh()
{
	auto request = GetRequest(currAvatar->mesh);
	request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr req, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto meshResponse = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;
		
		const auto meshArchivePath = FPaths::Combine(DownloadLocation(currAvatar->code), TEXT("model.zip"));
		SaveTArray(meshResponse, meshArchivePath);

		if (ItSeez3D::UnzipFile(meshArchivePath))
		{
			UE_LOG(LogClass, Log, TEXT("Unzip completed for mesh archive!"));
			meshPath = FPaths::Combine(DownloadLocation(currAvatar->code), TEXT("model.ply"));
			DisplayAvatar();
		}
	});
	UE_LOG(LogClass, Log, TEXT("Downloading mesh for avatar: %s"), *currAvatar->code);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Downloading mesh...")));
	request->ProcessRequest();
}

void AGameAvatar::DownloadHeadTexture()
{
	auto request = GetRequest(currAvatar->texture);
	request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr req, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto textureBytes = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;

		texturePath = FPaths::Combine(DownloadLocation(currAvatar->code), TEXT("model.jpg"));
		SaveTArray(textureBytes, texturePath);
		DisplayAvatar();
	});
	UE_LOG(LogClass, Log, TEXT("Downloading texture for avatar: %s"), *currAvatar->code);
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Downloading texture...")));
	request->ProcessRequest();
}

void AGameAvatar::DisplayAvatar()
{
	if (meshPath.IsEmpty() || texturePath.IsEmpty())
	{
		UE_LOG(LogClass, Log, TEXT("Mesh %s, texture %s. Not all data downloaded, still waiting..."), *meshPath, *texturePath);
		return;
	}

	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, FString::Printf(TEXT("Displaying avatar!")));
	UE_LOG(LogClass, Log, TEXT("Mesh %s, texture %s. All downloaded! Displaying avatar in a scene..."), *meshPath, *texturePath);

	std::string meshPathStr = std::string(TCHAR_TO_UTF8(*meshPath));
	std::ifstream meshStream{ meshPathStr.c_str(), std::ios::in | std::ios::binary };
	TArray<FVector> originalVertices;
	TArray<int32> faces;
	TArray<TArray<FVector2D>> faceUv;
	ItSeez3D::LoadModelFromBinPLY(meshStream, &originalVertices, nullptr, &faces, &faceUv);
	ItSeez3D::FlipNormals(faces, faceUv);

	TArray<FVector> vertices;
	TArray<FVector2D> uv;
	TArray<int> indexMap;
	ItSeez3D::ConvertToUnrealFormat(originalVertices, faceUv, faces, vertices, uv, indexMap);
	ItSeez3D::AdjustPhysicalUnits(vertices);

	headMesh->CreateMeshSection_LinearColor(0, vertices, faces, TArray<FVector>(), uv, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
	headMesh->AddLocalRotation(FRotator(0, 180, -90));

	auto material = headMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, headMaterial);

	UTexture2D *texture;
	IImageWrapperModule& imageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	IImageWrapperPtr imageWrapper = imageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	TArray<uint8> textureData;
	if (LoadTArray(texturePath, textureData) > 0)
		if (imageWrapper.IsValid())
			if (imageWrapper->SetCompressed(textureData.GetData(), textureData.Num()))
			{
				const TArray<uint8> *uncompressedBGRA = nullptr;

				if (imageWrapper->GetRaw(ERGBFormat::BGRA, 8, uncompressedBGRA))
				{
					const auto w = imageWrapper->GetWidth(), h = imageWrapper->GetHeight();
					texture = UTexture2D::CreateTransient(w, h, PF_B8G8R8A8);

					void *textureBytes = texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
					FMemory::Memcpy(textureBytes, uncompressedBGRA->GetData(), uncompressedBGRA->Num());
					texture->PlatformData->Mips[0].BulkData.Unlock();
					texture->UpdateResource();
					material->SetTextureParameterValue(FName("Tex"), texture);
				}
			}
}

void AGameAvatar::GetHaircuts()
{
	auto request = GetRequest(currAvatar->haircuts);
	request->OnProcessRequestComplete().BindUObject(this, &AGameAvatar::OnHaircutsRequested);

	UE_LOG(LogClass, Log, TEXT("Getting list of haircuts for avatar: %s"), *(currAvatar->code));
	request->ProcessRequest();
}

void AGameAvatar::OnHaircutsRequested(FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
{
	auto haircutsResponse = HandleJsonArrayResponse(response, bWasSuccessful);
	if (!haircutsResponse.IsValid())
		return;

	auto haircutsArray = haircutsResponse->AsArray();
	TArray<TSharedPtr<HaircutData>> availableHaircuts;
	for (auto &haircutJson : haircutsArray)
		availableHaircuts.Emplace(new HaircutData(*haircutJson->AsObject()));

	if (availableHaircuts.Num() == 0)
	{
		UE_LOG(LogClass, Error, TEXT("No haircuts available"));
		return;
	}

	// choose random haircut to display
	const int haircutIdx = rand() % availableHaircuts.Num();
	currHaircut = availableHaircuts[haircutIdx];

	DownloadHaircutMesh();
	DownloadHaircutTexture();
	DownloadHaircutPoints();
}

void AGameAvatar::DownloadHaircutMesh()
{
	if (FPaths::FileExists(HaircutFilePath(HaircutFile::MESH, currHaircut->id)))
	{
		UE_LOG(LogClass, Log, TEXT("Mesh for haircut %s already downloaded!"), *(currHaircut->id));
		haircutMeshDownloaded = true;
		DisplayHaircut();
		return;
	}

	auto request = GetRequest(currHaircut->mesh);
	request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr req, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto meshResponse = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;

		const auto meshArchivePath = HaircutFilePath(HaircutFile::MESH_ZIP, currHaircut->id);
		SaveTArray(meshResponse, meshArchivePath);

		if (ItSeez3D::UnzipFile(meshArchivePath))
		{
			UE_LOG(LogClass, Log, TEXT("Unzip completed for haircut mesh archive!"));
			haircutMeshDownloaded = true;
			DisplayHaircut();
		}
	});
	UE_LOG(LogClass, Log, TEXT("Downloading haircut mesh for avatar: %s"), *currAvatar->code);
	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Downloading haircut mesh...")));
	request->ProcessRequest();
}

void AGameAvatar::DownloadHaircutTexture()
{
	if (FPaths::FileExists(HaircutFilePath(HaircutFile::TEXTURE, currHaircut->id)))
	{
		UE_LOG(LogClass, Log, TEXT("Texture for haircut %s already downloaded!"), *(currHaircut->id));
		haircutTextureDownloaded = true;
		DisplayHaircut();
		return;
	}

	auto request = GetRequest(currHaircut->texture);
	request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto textureBytes = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;

		SaveTArray(textureBytes, HaircutFilePath(HaircutFile::TEXTURE, currHaircut->id));
		haircutTextureDownloaded = true;
		DisplayHaircut();
	});
	UE_LOG(LogClass, Log, TEXT("Downloading haircut texture for avatar: %s"), *currAvatar->code);
	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Downloading haircut texture...")));
	request->ProcessRequest();
}

void AGameAvatar::DownloadHaircutPoints()
{
	auto request = GetRequest(currHaircut->pointCloud);
	request->OnProcessRequestComplete().BindLambda([&](FHttpRequestPtr, FHttpResponsePtr response, bool bWasSuccessful)
	{
		bool bIsOk;
		auto pointsArchiveResponse = HandleDataResponse(response, bWasSuccessful, bIsOk);
		if (!bIsOk)
			return;

		const auto pointsArchivePath = HaircutAvatarFilePath(AvatarFile::HAIRCUT_POINTS_ZIP, currAvatar->code, currHaircut->id);
		SaveTArray(pointsArchiveResponse, pointsArchivePath);

		if (ItSeez3D::UnzipFile(pointsArchivePath))
		{
			UE_LOG(LogClass, Log, TEXT("Unzip completed for haircut points!"));
			haircutPointsDownloaded = true;
			DisplayHaircut();
		}
	});
	UE_LOG(LogClass, Log, TEXT("Downloading haircut points for avatar: %s"), *currAvatar->code);
	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Downloading haircut points...")));
	request->ProcessRequest();
}

void AGameAvatar::DisplayHaircut()
{
	if (!haircutTextureDownloaded || !haircutMeshDownloaded || !haircutPointsDownloaded)
	{
		UE_LOG(LogClass, Log, TEXT("Hair mesh %d, hair texture %d, points %d. Not all data downloaded, still waiting..."), haircutMeshDownloaded, haircutTextureDownloaded, haircutPointsDownloaded);
		return;
	}

	UE_LOG(LogClass, Log, TEXT("Hair mesh %d, hair texture %d, points %d. All downloaded! Displaying haircut in a scene..."), haircutMeshDownloaded, haircutTextureDownloaded, haircutPointsDownloaded);
	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, FString::Printf(TEXT("Displaying haircut in a scene!")));

	std::string pointsPathStr = std::string(TCHAR_TO_UTF8(*HaircutAvatarFilePath(AvatarFile::HAIRCUT_POINTS_PLY, currAvatar->code, currHaircut->id)));
	std::ifstream pointsStream{ pointsPathStr.c_str(), std::ios::in | std::ios::binary };
	TArray<FVector> points;
	ItSeez3D::LoadModelFromBinPLY(pointsStream, &points);

	std::string meshPathStr = std::string(TCHAR_TO_UTF8(*HaircutFilePath(HaircutFile::MESH, currHaircut->id)));
	std::ifstream meshStream{ meshPathStr.c_str(), std::ios::in | std::ios::binary };
	TArray<FVector> originalVertices;
	TArray<int32> faces;
	TArray<TArray<FVector2D>> faceUv;
	ItSeez3D::LoadModelFromBinPLY(meshStream, &originalVertices, nullptr, &faces, &faceUv);
	originalVertices.Empty();
	originalVertices = points;
	ItSeez3D::FlipNormals(faces, faceUv);

	TArray<FVector> vertices;
	TArray<FVector2D> uv;
	TArray<int> indexMap;
	ItSeez3D::ConvertToUnrealFormat(originalVertices, faceUv, faces, vertices, uv, indexMap);
	ItSeez3D::AdjustPhysicalUnits(vertices);

	haircutMesh->CreateMeshSection_LinearColor(0, vertices, faces, TArray<FVector>(), uv, TArray<FLinearColor>(), TArray<FProcMeshTangent>(), true);
	haircutMesh->AddLocalRotation(FRotator(0, 180, -90));

	auto material = haircutMesh->CreateAndSetMaterialInstanceDynamicFromMaterial(0, hairMaterial);

	UTexture2D *texture;
	IImageWrapperModule& imageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	IImageWrapperPtr imageWrapper = imageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	TArray<uint8> textureData;
	if (LoadTArray(HaircutFilePath(HaircutFile::TEXTURE, currHaircut->id), textureData))
		if (imageWrapper.IsValid())
			if (imageWrapper->SetCompressed(textureData.GetData(), textureData.Num()))
			{
				const TArray<uint8> *uncompressedBGRA = nullptr;

				if (imageWrapper->GetRaw(ERGBFormat::BGRA, 8, uncompressedBGRA))
				{
					const auto w = imageWrapper->GetWidth(), h = imageWrapper->GetHeight();
					texture = UTexture2D::CreateTransient(w, h, PF_B8G8R8A8);

					void *textureBytes = texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
					FMemory::Memcpy(textureBytes, uncompressedBGRA->GetData(), uncompressedBGRA->Num());
					texture->PlatformData->Mips[0].BulkData.Unlock();
					texture->UpdateResource();
					material->SetTextureParameterValue(FName("Tex"), texture);
				}
			}

	GEngine->AddOnScreenDebugMessage(-1, 100500.f, FColor::Yellow, FString::Printf(TEXT("Avatar with random haircut was generated. Restart the sample to create another one.")));
}

// Called every frame
void AGameAvatar::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
