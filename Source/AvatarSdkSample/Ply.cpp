/* Copyright (C) Itseez3D, Inc. - All Rights Reserved
* You may not use this file except in compliance with an authorized license
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* UNLESS REQUIRED BY APPLICABLE LAW OR AGREED BY ITSEEZ3D, INC. IN WRITING, SOFTWARE DISTRIBUTED UNDER THE LICENSE IS DISTRIBUTED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED
* See the License for the specific language governing permissions and limitations under the License.
* Written by Itseez3D, Inc. <support@itseez3D.com>, April 2017
*/

#include "Ply.h"

#include <string>
#include <cassert>


DEFINE_LOG_CATEGORY_STATIC(LogPly, All, All)


namespace
{
	void parsePlyHeader(
		std::istream &inputMesh,
		bool &existVertices,
		bool &existVerticesNormals,
		bool &existFaces,
		bool &existUvMapping,
		size_t &countElementVertexInMesh,
		size_t &countElementFacesInMesh
	)
	{
		std::string line;
		while (std::getline(inputMesh, line))
		{
			if (line.find("element vertex ") != std::string::npos)
			{
				countElementVertexInMesh = atoi(line.substr(line.find_last_of(" ") + 1).c_str());
				continue;
			}

			if (line.find("property float x") != std::string::npos)
			{
				existVertices = true;
				continue;
			}

			if (line.find("property float nx") != std::string::npos)
			{
				existVerticesNormals = true;
				continue;
			}

			if (line.find("element face ") != std::string::npos)
			{
				countElementFacesInMesh = atoi(line.substr(line.find_last_of(" ") + 1).c_str());
				continue;
			}

			if (line.find("property list uchar int vertex_indices") != std::string::npos)
			{
				existFaces = true;
				continue;
			}

			if (line.find("property list uchar float texcoord") != std::string::npos)
			{
				existUvMapping = true;
				continue;
			}

			if (line.find("end_header") != std::string::npos)
				break;
		}
	}
}


void ItSeez3D::LoadModelFromBinPLY(
	std::istream &inputMesh,
	TArray<FVector> *vertices,
	TArray<FVector> *verticesNormals,
	TArray<int32> *faces,
	TArray<TArray<FVector2D>> *uvMapping
)
{
	bool existVertices = false, loadVertices = vertices != 0;
	bool existVerticesNormals = false, loadVerticesNormals = verticesNormals != 0;
	bool existFaces = false, loadFaces = faces != 0;
	bool existUvMapping = false, loadUvMapping = uvMapping != 0;

	const int sizeofInt = sizeof(int);
	const int sizeofChar = sizeof(char);
	const int sizeofFloat = sizeof(float);
	assert(4 * sizeofChar == sizeofFloat);

	size_t countElementVertexInMesh, countElementFacesInMesh;
	parsePlyHeader(inputMesh, existVertices, existVerticesNormals, existFaces, existUvMapping, countElementVertexInMesh, countElementFacesInMesh);

	if (loadVertices && !existVertices)
	{
		UE_LOG(LogPly, Error, TEXT("Error: vertices don't exist in mesh file."));
		loadVertices = false;
	}
	if (loadVerticesNormals && !existVerticesNormals)
	{
		UE_LOG(LogPly, Error, TEXT("Error: normals don't exist in mesh file."));
		loadVerticesNormals = false;
	}
	if (loadFaces && !existFaces)
	{
		UE_LOG(LogPly, Error, TEXT("Error: faces don't exist in mesh file."));
		loadFaces = false;
	}
	if (loadUvMapping && !existUvMapping)
	{
		UE_LOG(LogPly, Error, TEXT("Error: uv mapping does not exist in mesh file."));
		loadUvMapping = false;
	}

	if (loadVertices)
		vertices->Init(FVector(), countElementVertexInMesh);
	if (loadVerticesNormals)
		verticesNormals->Init(FVector(), countElementVertexInMesh);
	if (loadFaces)
		faces->Init(0, countElementFacesInMesh * 3);
	if (loadUvMapping)
		uvMapping->Init(TArray<FVector2D>(), countElementFacesInMesh);

	assert(existVertices || !existVerticesNormals);
	if (existVertices || existVerticesNormals)
	{
		const int verticesValuesInLine = (existVertices ? 3 : 0) + (existVerticesNormals ? 3 : 0);
		for (size_t i = 0; i < countElementVertexInMesh; ++i)
		{
			TArray<float> values;
			values.SetNum(verticesValuesInLine);
			inputMesh.read((char *)values.GetData(), sizeofFloat * verticesValuesInLine);

			if (loadVertices)
				(*vertices)[i] = FVector(values[0], values[1], values[2]);
			if (loadVerticesNormals)
				(*verticesNormals)[i] = FVector(values[3], values[4], values[5]);
		}
	}

	assert(existFaces || !existUvMapping);
	if (loadFaces || loadUvMapping)
	{
		for (size_t i = 0; i < countElementFacesInMesh; ++i)
		{
			uint8_t countVertices;
			inputMesh.read((char *)&countVertices, sizeofChar);
			assert(countVertices == 3);

			if (loadFaces)
			{
				const int idx = i * countVertices;
				inputMesh.read((char *)(faces->GetData() + idx), sizeof((*faces)[0]) * countVertices);
			}

			if (loadUvMapping)
			{
				(*uvMapping)[i].SetNum(countVertices);
				uint8_t countUvMapping;
				inputMesh.read((char *)&countUvMapping, sizeofChar);
				assert(countUvMapping == 6);

				TArray<float> values;
				values.SetNum(countUvMapping);
				inputMesh.read((char *)values.GetData(), sizeofFloat * countUvMapping);
				for (size_t j = 0; j < countVertices; ++j)
					(*uvMapping)[i][j] = FVector2D(values[2 * j], 1 - values[2 * j + 1]);
			}
		}
	}
}

void ItSeez3D::FlipNormals(TArray<int32> &faces, TArray<TArray<FVector2D>> &faceUv)
{
	assert(faces.Num() % 3 == 0);
	for (int i = 0; i < faces.Num() / 3; ++i)
	{
		std::swap(faces[3 * i + 1], faces[3 * i + 2]);
		std::swap(faceUv[i][1], faceUv[i][2]);
	}
}

void ItSeez3D::ConvertToUnrealFormat(
	const TArray<FVector> &originalVertices,
	const TArray<TArray<FVector2D>> &faceUv,
	TArray<int32> &faces,
	TArray<FVector> &vertices,
	TArray<FVector2D> &uv,
	TArray<int> &indexMap
)
{
	// If different uv coordinates correspond to single vertex we need to
	// duplicate this vertex in order to comply with Unreal mesh format.
	// This array holds indices of created duplicates.
	TArray<int> duplicate;
	duplicate.Init(-1, faces.Num());

	// maximum number of vertices after transformation is number of faces * 3 (which is faces.Num)
	// (theoretically each vertex in each face can have unique uv coord pair)
	FVector2D uninitialized{ -1, -1 };
	TArray<FVector2D> vertexUv;
	vertexUv.Init(uninitialized, faces.Num());

	int numFaces = faces.Num() / 3, numVertices = originalVertices.Num();
	for (int faceIdx = 0; faceIdx < numFaces; ++faceIdx)
	{
		for (int j = 0; j < 3; ++j)
		{
			int vertexIdx = faces[faceIdx * 3 + j];
			FVector2D currentUv = faceUv[faceIdx][j];

			// Iterate over duplicates of this vertex until we find copy with exact same uv.
			// Create new duplicate vertex if none were found.
			while (vertexUv[vertexIdx] != uninitialized && vertexUv[vertexIdx] != currentUv)
			{
				if (duplicate[vertexIdx] == -1)
					duplicate[vertexIdx] = numVertices++;  // "allocate" new vertex and save link to it
				vertexIdx = duplicate[vertexIdx];
			}

			vertexUv[vertexIdx] = currentUv;
			faces[faceIdx * 3 + j] = vertexIdx;
		}
	}

	vertices.SetNum(numVertices);
	indexMap.SetNum(numVertices);

	for (int i = 0; i < originalVertices.Num(); ++i)
	{
		vertices[i] = originalVertices[i];
		indexMap[i] = i;
	}

	for (int i = 0; i < numVertices; ++i)
		if (duplicate[i] != -1)
		{
			vertices[duplicate[i]] = vertices[i];
			indexMap[duplicate[i]] = i;
		}

	uv.SetNum(numVertices);
	for (int i = 0; i < numVertices; ++i)
		uv[i] = vertexUv[i];

	UE_LOG(LogPly, Log, TEXT("Before transformation: %d vertices, after: %d vertices"), originalVertices.Num(), vertices.Num());
}

void ItSeez3D::AdjustPhysicalUnits(TArray<FVector> &vertices, float scale)
{
	for (auto &p : vertices)
		p *= scale;
}
