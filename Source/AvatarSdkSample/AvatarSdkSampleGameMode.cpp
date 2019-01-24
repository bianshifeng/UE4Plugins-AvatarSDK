// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#include "AvatarSdkSampleGameMode.h"
#include "AvatarSdkSampleHUD.h"
#include "AvatarSdkSampleCharacter.h"
#include "UObject/ConstructorHelpers.h"

AAvatarSdkSampleGameMode::AAvatarSdkSampleGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPersonCPP/Blueprints/FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	// use our custom HUD class
	HUDClass = AAvatarSdkSampleHUD::StaticClass();
}
