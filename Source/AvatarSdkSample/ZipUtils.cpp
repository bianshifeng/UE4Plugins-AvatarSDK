/* Copyright (C) Itseez3D, Inc. - All Rights Reserved
* You may not use this file except in compliance with an authorized license
* Unauthorized copying of this file, via any medium is strictly prohibited
* Proprietary and confidential
* UNLESS REQUIRED BY APPLICABLE LAW OR AGREED BY ITSEEZ3D, INC. IN WRITING, SOFTWARE DISTRIBUTED UNDER THE LICENSE IS DISTRIBUTED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
* CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED
* See the License for the specific language governing permissions and limitations under the License.
* Written by Itseez3D, Inc. <support@itseez3D.com>, April 2017
*/

#include "ZipUtils.h"

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#include <string>
#include <vector>
#include <fstream>

#include "Paths.h"

#include "minizip/unzip.h"


DEFINE_LOG_CATEGORY_STATIC(LogZipUtils, All, All)


namespace
{
	bool DoUnzip(unzFile hFile, const FString &directory)
	{
		unz_global_info globalInfo = { 0 };
		if (unzGetGlobalInfo(hFile, &globalInfo) != UNZ_OK)
		{
			UE_LOG(LogZipUtils, Error, TEXT("unzGetGlobalInfo error"));
			return false;
		}

		if (unzGoToFirstFile(hFile) != UNZ_OK)
		{
			UE_LOG(LogZipUtils, Error, TEXT("unzGoToFirstFile error"));
			return false;
		}

		constexpr int maxNameLength = 1 << 10;
		char filename[maxNameLength];

		constexpr int sizeBuffer = 1 << 15;
		std::vector<char> buffer(sizeBuffer);

		do
		{
			if (unzOpenCurrentFile(hFile) != UNZ_OK)
			{
				UE_LOG(LogZipUtils, Error, TEXT("unzOpenCurrentFile error"));
				return false;
			}

			unz_file_info fileInfo;
			
			if (unzGetCurrentFileInfo(hFile, &fileInfo, filename, maxNameLength, 0, 0, 0, 0) != UNZ_OK)
			{
				UE_LOG(LogZipUtils, Error, TEXT("unzGetCurrentFileInfo error"));
				unzCloseCurrentFile(hFile);
				return false;
			}

			const auto absoluteFilename = FPaths::Combine(directory, FString(UTF8_TO_TCHAR(filename)));
			UE_LOG(LogZipUtils, Log, TEXT("Unzipping file %s..."), *absoluteFilename);

			const std::string absoluteFilenameStr{ TCHAR_TO_UTF8(*absoluteFilename) };
			
			std::ofstream file{ absoluteFilenameStr, std::ios::binary | std::ios::out };
			int readSize, totalSize = 0;
			while ((readSize = unzReadCurrentFile(hFile, buffer.data(), sizeBuffer)) > 0)
			{
				file.write(buffer.data(), readSize);
				totalSize += readSize;
			}

			UE_LOG(LogZipUtils, Log, TEXT("Total file size %d"), totalSize);

			unzCloseCurrentFile(hFile);
		} while (unzGoToNextFile(hFile) == UNZ_OK);

		return true;
	}
}


bool ItSeez3D::UnzipFile(const FString &path)
{
	const auto directory = FPaths::GetPath(path);

	UE_LOG(LogZipUtils, Log, TEXT("Unzipping %s to directory %s..."), *path, *directory);
	const std::string zipFilename{ TCHAR_TO_UTF8(*path) };

	unzFile hFile = unzOpen(zipFilename.c_str());
	if (!hFile)
	{
		UE_LOG(LogZipUtils, Error, TEXT("Unable to open file %s"), *path);
		return false;
	}

	const bool success = DoUnzip(hFile, directory);
	unzClose(hFile);
	UE_LOG(LogZipUtils, Log, TEXT("Unzipping finished, success: %d"), success);
	return success;
}
