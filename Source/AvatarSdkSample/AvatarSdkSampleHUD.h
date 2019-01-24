// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AvatarSdkSampleHUD.generated.h"

UCLASS()
class AAvatarSdkSampleHUD : public AHUD
{
	GENERATED_BODY()

public:
	AAvatarSdkSampleHUD();

	/** Primary draw call for the HUD */
	virtual void DrawHUD() override;

private:
	/** Crosshair asset pointer */
	class UTexture2D* CrosshairTex;

};

