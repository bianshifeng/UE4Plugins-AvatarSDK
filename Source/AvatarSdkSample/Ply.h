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

#include <istream>

#include "CoreMinimal.h"


namespace ItSeez3D
{
	void LoadModelFromBinPLY(
		std::istream &inputMesh,
		TArray<FVector> *vertices = nullptr,
		TArray<FVector> *verticesNormals = nullptr,
		TArray<int32> *faces = nullptr,
		TArray<TArray<FVector2D>> *uvMapping = nullptr
	);

	void FlipNormals(
		TArray<int32> &faces,
		TArray<TArray<FVector2D>> &faceUv
	);

	void ConvertToUnrealFormat(
		const TArray<FVector> &originalVertices,
		const TArray<TArray<FVector2D>> &faceUv,
		TArray<int32> &faces,
		TArray<FVector> &vertices,
		TArray<FVector2D> &uv,
		TArray<int> &indexMap
	);

	void AdjustPhysicalUnits(
		TArray<FVector> &vertices,
		float scale = 100
	);
}
