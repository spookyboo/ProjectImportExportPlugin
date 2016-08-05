/*
  -----------------------------------------------------------------------------
  This source file is part of OGRE
  (Object-oriented Graphics Rendering Engine)
  For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2014 Torus Knot Software Ltd

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
  -----------------------------------------------------------------------------
*/

#include "OgreRoot.h"
#include "ProjectImportExportPlugin.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreHlmsUnlit.h"
#include "OgreHlmsUnlitDatablock.h"
#include "OgreHlmsJson.h"
#include "OgreHlmsManager.h"
#include "OgreLogManager.h"
#include "OgreItem.h"
#include "zip.h"
#include <iostream>
#include <fstream>

namespace Ogre
{
#ifdef __APPLE__
	// In darwin and perhaps other BSD variants off_t is a 64 bit value, hence no need for specific 64 bit functions
#define FOPEN_FUNC(filename, mode) fopen(filename, mode)
#define FTELLO_FUNC(stream) ftello(stream)
#define FSEEKO_FUNC(stream, offset, origin) fseeko(stream, offset, origin)
#else
#define FOPEN_FUNC(filename, mode) fopen64(filename, mode)
#define FTELLO_FUNC(stream) ftello64(stream)
#define FSEEKO_FUNC(stream, offset, origin) fseeko64(stream, offset, origin)
#endif
#define WRITEBUFFERSIZE (16384)

	static const String gImportMenuText = "ProjectImportExport: Import HLMS Editor project (from zip)";
	static const String gExportMenuText = "ProjectImportExport: Export HLMS Editor project (to zip)";
	static String gTempString = "";
	//---------------------------------------------------------------------
	ProjectImportExportPlugin::ProjectImportExportPlugin()
	{
	}
	//---------------------------------------------------------------------
	const String& ProjectImportExportPlugin::getName() const
	{
		return GENERAL_HLMS_PLUGIN_NAME;
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::install()
	{
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::initialise()
	{
		// nothing to do
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::shutdown()
	{
		// nothing to do
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::uninstall()
	{
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isOpenFileDialogForImport(void) const
	{
		return false;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isImport(void) const
	{
		return false;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isOpenFileDialogForExport(void) const
	{
		return true;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isTexturesUsedByDatablocksForExport(void) const
	{
		return true;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isExport(void) const
	{
		return true;
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::performPreImportActions(void)
	{
		// Nothing to do
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::performPostImportActions(void)
	{
		// Nothing to do
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::performPreExportActions(void)
	{
		// Nothing to do
	}
	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::performPostExportActions(void)
	{
		// Delete the copied files
		// Note: Deleting the files as part of the export and just after creating a zip file
		// results in a corrupted zip file. Apparently, the files are still in use, even if the zip file
		// is already closed
		mySleep(1);
		std::vector<String>::iterator it = mFileNamesDestination.begin();
		std::vector<String>::iterator itEnd = mFileNamesDestination.end();
		String fileName;
		while (it != itEnd)
		{
			fileName = *it;
			std::remove(fileName.c_str());
			++it;
		}
	}
	//---------------------------------------------------------------------
	const String& ProjectImportExportPlugin::getImportMenuText(void) const
	{
		return gImportMenuText;
	}
	//---------------------------------------------------------------------
	const String& ProjectImportExportPlugin::getExportMenuText(void) const
	{
		return gExportMenuText;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::executeImport(HlmsEditorPluginData* data)
	{
		// nothing to do
		return true;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::executeExport(HlmsEditorPluginData* data)
	{
		mFileNamesDestination.clear();
		mUniqueTextureFiles.clear();

		// Error in case no materials available
		if (data->mInMaterialFileNameVector.size() == 0)
		{
			data->mOutSuccessText = "Nothing to export";
			return true;
		}

		// 1. Copy texture files from the material (Json) files
		// This is needed in case the texture is not available in the texture browser; the exported zip file
		// contains both the images/textures from the texture browser and the references in the material/json files

		// Iterate through the json files of the material browser and load them into Ogre
		std::vector<String> materials;
		materials = data->mInMaterialFileNameVector;
		std::vector<String>::iterator it;
		std::vector<String>::iterator itStart = materials.begin();
		std::vector<String>::iterator itEnd = materials.end();
		String fileName;
		for (it = itStart; it != itEnd; ++it)
		{
			// Load the materials
			fileName = *it;
			if (fileName.empty())
			{
				data->mOutErrorText = "Trying to process a non-existing material filename";
				return false;
			}

			// If an Exception is thrown, it may be because the loaded material is already available; just ignore it
			if (!loadMaterial(fileName))
			{
				data->mOutErrorText = "Error while processing the materials";
				return false;
			}
		}

		// Retrieve all the texturenames from the loaded datablocks
		std::vector<String> v = data->mInTexturesUsedByDatablocks;
		if (data->mInTexturesUsedByDatablocks.size() == 0)
		{
			data->mOutErrorText = "No textures to export";
			return false;
		}

		// vector v only contains basenames; Get the full qualified name instead
		std::vector<String>::iterator itBaseNames;
		std::vector<String>::iterator itBaseNamesStart = v.begin();
		std::vector<String>::iterator itBaseNamesEnd = v.end();
		String baseName;
		std::vector<String> fileNamesSource;
		for (itBaseNames = itBaseNamesStart; itBaseNames != itBaseNamesEnd; ++itBaseNames)
		{
			baseName = *itBaseNames;

			// Search in the Ogre resources
			fileName = getFullFileNameFromResources(baseName, data);
			if (fileName.empty())
			{
				// It cannot be found in the resources, try it in the texture list from the project
				fileName = getFullFileNameFromTextureList(baseName, data);
			}

			if (!fileName.empty())
			{
				fileNamesSource.push_back(fileName);
				//LogManager::getSingleton().logMessage("Texture to export: " + fileName);
			}
		}

		// Copy all textures to the export dir
		std::vector<String>::iterator itFileNamesSource;
		std::vector<String>::iterator itFileNamesSourceStart = fileNamesSource.begin();
		std::vector<String>::iterator itFileNamesSourceEnd = fileNamesSource.end();
		String fileNameSource;
		String fileNameDestination;
		for (itFileNamesSource = itFileNamesSourceStart; itFileNamesSource != itFileNamesSourceEnd; ++itFileNamesSource)
		{
			fileNameSource = *itFileNamesSource;
			//std::ifstream src(fileNameSource, std::ios::binary);
			baseName = fileNameSource.substr(fileNameSource.find_last_of("/\\") + 1);
			fileNameDestination = data->mInImportExportPath + baseName;
			if (!isDestinationFileAvailableInVector(fileNameDestination))
			{
				mFileNamesDestination.push_back(fileNameDestination); // Only push unique names
				mUniqueTextureFiles.push_back(baseName);
			}
			//std::ofstream dst(fileNameDestination, std::ios::binary);
			//dst << src.rdbuf();
			//dst.close();
			//src.close();
			copyFile(fileNameSource, fileNameDestination);
		}

		// 2. Copy texture files from the texture browser
		std::vector<String>::iterator itTextures;
		std::vector<String>::iterator itTexturesStart = data->mInTextureFileNameVector.begin();
		std::vector<String>::iterator itTexturesEnd = data->mInTextureFileNameVector.end();
		String fileNameTextureSource;
		String fileNameTextureDestination;
		String baseNameTexture;
		for (itTextures = itTexturesStart; itTextures != itTexturesEnd; ++itTextures)
		{
			fileNameTextureSource = *itTextures;
			baseNameTexture = fileNameTextureSource.substr(fileNameTextureSource.find_last_of("/\\") + 1);
			//std::ifstream src(fileNameTextureSource, std::ios::binary);
			fileNameTextureDestination = data->mInImportExportPath + baseNameTexture;
			if (!isDestinationFileAvailableInVector(fileNameTextureDestination))
			{
				mFileNamesDestination.push_back(fileNameTextureDestination); // Only push unique names
				mUniqueTextureFiles.push_back(baseNameTexture);
			}
			//std::ofstream dst(fileNameTextureDestination, std::ios::binary);
			//dst << src.rdbuf();
			//dst.close();
			//src.close();
			copyFile(fileNameTextureSource, fileNameTextureDestination);
		}

		// 3. Copy all Json (material) files to the export dir
		itStart = materials.begin();
		itEnd = materials.end();
		for (it = itStart; it != itEnd; ++it)
		{
			// Load the materials
			fileName = *it;
			if (fileName.empty())
			{
				data->mOutErrorText = "Trying to process a non-existing material filename";
				return false;
			}

			// Copy the json (material) files
			//std::ifstream src(fileName, std::ios::binary);
			baseName = fileName.substr(fileName.find_last_of("/\\") + 1);
			fileNameDestination = data->mInImportExportPath + baseName;
			mFileNamesDestination.push_back(fileNameDestination);
			//std::ofstream dst(fileNameDestination, std::ios::binary);
			//dst << src.rdbuf();
			//dst.close();
			//src.close();
			copyFile(fileName, fileNameDestination);
		}

		// 4. Create material config file for export (without paths)
		// This file does not contain any path info; when the exported project is imported again, the 
		// material cfg file is enriched with the path of the import directory
		createMaterialCfgFileForExport(data);

		// 5. Create texture config file for export (without paths)
		// This file does not contain any path info; when the exported project is imported again, the 
		// texture cfg file is enriched with the path of the import directory
		createTextureCfgFileForExport(data);

		
		// Zip all files
		zipFile zf;
		int err;
		int errclose;
		const char* password = NULL;
		void* buf = NULL;
		int size_buf = 0;
		size_buf = WRITEBUFFERSIZE;
		int opt_compress_level = Z_DEFAULT_COMPRESSION;
		buf = (void*)malloc(WRITEBUFFERSIZE);
		if (buf == NULL)
		{
			LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error allocating memory");
			return false;
		}
		String zipName = data->mInImportExportPath + data->mInProjectName + ".hlmp.zip";
		char zipFile[1024];
		char filenameInZip[1024];
		strcpy(zipFile, zipName.c_str());

#ifdef USEWIN32IOAPI
		zlib_filefunc64_def ffunc;
		fill_win32_filefunc64A(&ffunc);
		zf = zipOpen2_64(zipFile, 0, NULL, &ffunc);
#else
		zf = zipOpen64(zipFile, 0);
#endif

		if (zf == NULL)
		{
			LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error opening " + String(zipFile));
		}
		else
		{
			LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Creating  " + String(zipFile));

			// Add the copied texture files to the zipfile
			std::vector<String>::iterator itDest = mFileNamesDestination.begin();
			std::vector<String>::iterator itDestEnd = mFileNamesDestination.end();
			while (itDest != itDestEnd)
			{
				fileNameDestination = *itDest;
				strcpy(filenameInZip, fileNameDestination.c_str());

				FILE* fin;
				int size_read;
				zip_fileinfo zi;
				unsigned long crcFile = 0;
				int zip64 = isLargeFile(filenameInZip);
				const char *savefilenameInZip;
				savefilenameInZip = filenameInZip;

				// The path name saved, should not include a leading slash.
				// If it did, windows/xp and dynazip couldn't read the zip file.
				while (savefilenameInZip[0] == '\\' || savefilenameInZip[0] == '/')
				{
					savefilenameInZip++;
				}

				// Strip the path
				const char *tmpptr;
				const char *lastslash = 0;
				for (tmpptr = savefilenameInZip; *tmpptr; tmpptr++)
				{
					if (*tmpptr == '\\' || *tmpptr == '/')
					{
						lastslash = tmpptr;
					}
				}
				if (lastslash != NULL)
				{
					savefilenameInZip = lastslash + 1; // base filename follows last slash.
				}

				err = zipOpenNewFileInZip3_64(zf, savefilenameInZip, &zi,
					NULL, 0, NULL, 0, NULL /* comment*/,
					(opt_compress_level != 0) ? Z_DEFLATED : 0,
					opt_compress_level, 0,
					/* -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, */
					-MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
					password, crcFile, zip64);

				if (err != ZIP_OK)
				{
					LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error adding " + String(filenameInZip) + " to zipfile");
					return false;
				}
				else
				{
					fin = FOPEN_FUNC(filenameInZip, "rb");
					if (fin == NULL)
					{
						LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error opening " + String(filenameInZip));
						return false;
					}
				}

				if (err == ZIP_OK)
				{
					do
					{
						err = ZIP_OK;
						size_read = (int)fread(buf, 1, size_buf, fin);
						if (size_read < size_buf)
							if (feof(fin) == 0)
							{
								LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error reading " + String(filenameInZip));
								return false;
							}

						if (size_read > 0)
						{
							err = zipWriteInFileInZip(zf, buf, size_read);
							if (err < 0)
							{
								LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error in writing " + String(filenameInZip) + " in zipfile");
								return false;
							}

						}
					} while ((err == ZIP_OK) && (size_read>0));
				}

				// Close the file that is added to the zip
				if (fin)
					fclose(fin);

				if (err < 0)
					err = ZIP_ERRNO;
				else
				{
					err = zipCloseFileInZip(zf);
					if (err != ZIP_OK)
					{
						LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error in closing " + String(filenameInZip) + " in zipfile");
						return false;
					}
				}

				// Delete the file from the filesystem
				//std::remove(filenameInZip);

				// Next file
				++itDest;
			}
		}

		// Close the zipfile
		errclose = zipClose(zf, NULL);
		if (errclose != ZIP_OK)
		{
			LogManager::getSingleton().logMessage("ProjectImportExportPlugin: Error in closing " + String(zipFile));
			return false;
		}

		free(buf);
		data->mOutSuccessText = "Exported project to " + zipName;

		// Deleting the copied files here, results in a corrupted zip file, so put that as a separate post-export action
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::loadMaterial(const String& fileName)
	{
		// Read the json file as text file and feed it to the HlmsManager::loadMaterials() function
		// Note, that the resources (textures, etc.) must be present (in resource loacation)

		Root* root = Root::getSingletonPtr();
		HlmsManager* hlmsManager = root->getHlmsManager();
		HlmsJson hlmsJson(hlmsManager);

		// Read the content of the file into a string/char*
		std::ifstream inFile;
		inFile.open(fileName);

		std::stringstream strStream;
		strStream << inFile.rdbuf();
		String jsonAsString = strStream.str();

		std::cout << jsonAsString << std::endl;
		inFile.close();
		const char* jsonAsChar = jsonAsString.c_str();

		try
		{
			// Load the datablocks (which also creates them)
			hlmsJson.loadMaterials(fileName, jsonAsChar); // The fileName is only used for logging and has no purpose
		}

		// If an Exception is thrown, it may be because the loaded material is already available; just ignore it
		catch (Exception e) {}

		return true;
	}

	//---------------------------------------------------------------------
	const String& ProjectImportExportPlugin::getFullFileNameFromTextureList(const String& baseName, HlmsEditorPluginData* data)
	{
		gTempString = "";
		std::vector<String>::iterator it;
		std::vector<String>::iterator itStart = data->mInTextureFileNameVector.begin();
		std::vector<String>::iterator itEnd = data->mInTextureFileNameVector.end();
		String compareBaseName;
		String fileName;
		for (it = itStart; it != itEnd; ++it)
		{
			fileName = *it;
			//LogManager::getSingleton().logMessage("getFullFileNameFromTextureList - Texture in vector: " + fileName);
			compareBaseName = fileName.substr(fileName.find_last_of("/\\") + 1);
			if (baseName == compareBaseName)
			{
				gTempString = fileName;
				return gTempString;
			}
		}

		return gTempString;
	}

	//---------------------------------------------------------------------
	const String& ProjectImportExportPlugin::getFullFileNameFromResources(const String& baseName, HlmsEditorPluginData* data)
	{
		// Only search in the default resource group, because that is the only group the HLMS Editor uses
		gTempString = "";
		String filename;
		String path;
		FileInfoListPtr list = ResourceGroupManager::getSingleton().listResourceFileInfo(ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		FileInfoList::iterator it;
		FileInfoList::iterator itStart = list->begin();
		FileInfoList::iterator itEnd = list->end();
		for (it = itStart; it != itEnd; ++it)
		{
			FileInfo& fileInfo = (*it);
			if (fileInfo.basename == baseName)
			{
				gTempString = fileInfo.archive->getName() + "/" + baseName;
				return gTempString;
			}
		}

		return gTempString;
	}

	//---------------------------------------------------------------------
	int ProjectImportExportPlugin::isLargeFile(const char* filename)
	{
		int largeFile = 0;
		ZPOS64_T pos = 0;
		FILE* pFile = FOPEN_FUNC(filename, "rb");

		if (pFile != NULL)
		{
			int n = FSEEKO_FUNC(pFile, 0, SEEK_END);
			pos = FTELLO_FUNC(pFile);

			printf("File : %s is %lld bytes\n", filename, pos);

			if (pos >= 0xffffffff)
				largeFile = 1;

			fclose(pFile);
		}

		return largeFile;
	}

	//---------------------------------------------------------------------
	/*
	void ProjectImportExportPlugin::createProjectFile(HlmsEditorPluginData* data)
	{
		String fileNameProject = data->mInImportExportPath + data->mInProjectName + ".hlmp";
		String fileNameMaterials = data->mInMaterialFileName;
		String baseNameMaterials = fileNameMaterials.substr(fileNameMaterials.find_last_of("/\\") + 1);
		fileNameMaterials = data->mInImportExportPath + baseNameMaterials;
		String fileNameTextures = data->mInTextureFileName;
		String baseNameTextures = fileNameTextures.substr(fileNameTextures.find_last_of("/\\") + 1);
		fileNameTextures = data->mInImportExportPath + baseNameTextures;

		// Write to file
		std::ofstream file(fileNameProject);
		file << "hlmsEditor v1.0\n";
		file << fileNameMaterials
			<< "\n";
		file << fileNameTextures
			<< "\n";
		file.close();
		mFileNamesDestination.push_back(fileNameProject);
	}
	*/

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::createMaterialCfgFileForExport(HlmsEditorPluginData* data)
	{
		/*
		Create the material cfg file with all material files (in Jsonformat).
		*/
		std::vector<String> materials;
		materials = data->mInMaterialFileNameVector;
		std::vector<String>::iterator it;
		std::vector<String>::iterator itStart = materials.begin();
		std::vector<String>::iterator itEnd = materials.end();
		String fileNameMaterialSource = data->mInMaterialFileName;
		std::ifstream src(fileNameMaterialSource);
		String baseNameMaterial = fileNameMaterialSource.substr(fileNameMaterialSource.find_last_of("/\\") + 1);
		String fileNameMaterialDestination = data->mInImportExportPath + baseNameMaterial;
		std::ofstream dst(fileNameMaterialDestination);
		String topLevelId;
		String parentId;
		String resourceId;
		String resourceType;
		String resourceName;
		String fullQualifiedName;
		std::string line;
		while (std::getline(src, line))
		{
			std::istringstream iss(line);
			iss >> topLevelId
				>> parentId
				>> resourceId
				>> resourceType
				>> resourceName;
			dst << topLevelId
				<< "\t"
				<< parentId
				<< "\t"
				<< resourceId
				<< "\t"
				<< resourceType
				<< "\t"
				<< resourceName
				<< "\t"
				<< resourceName
				<< "\n";
		}
		src.close();
		dst.close();
		mFileNamesDestination.push_back(fileNameMaterialDestination);
	}

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::createTextureCfgFileForExport(HlmsEditorPluginData* data)
	{
		/*
		Create the texture cfg file with all unique textures. This concerns both the texture from the texture browser, but also the 
		textures used in the material files, which are not present in the texture browser.
		*/
		std::vector<String> textures;
		textures = data->mInTextureFileNameVector;
		std::vector<String>::iterator it;
		std::vector<String>::iterator itStart = textures.begin();
		std::vector<String>::iterator itEnd = textures.end();
		String fileNameTextureSource = data->mInTextureFileName;
		std::ifstream src(fileNameTextureSource);
		String baseNameTexture = fileNameTextureSource.substr(fileNameTextureSource.find_last_of("/\\") + 1);
		String fileNameTextureDestination = data->mInImportExportPath + baseNameTexture;
		std::ofstream dst(fileNameTextureDestination);
		int topLevelId;
		int parentId;
		int resourceId;
		int maxResourceId = 0;
		int resourceType;
		String resourceName;
		String fullQualifiedName;
		std::string line;
		while (std::getline(src, line))
		{
			std::istringstream iss(line);
			iss >> topLevelId
				>> parentId
				>> resourceId
				>> resourceType
				>> resourceName;
			
			if (resourceId > maxResourceId)
				maxResourceId = resourceId;

			dst << topLevelId
				<< "\t"
				<< parentId
				<< "\t"
				<< resourceId
				<< "\t"
				<< resourceType
				<< "\t"
				<< resourceName
				<< "\t"
				<< resourceName
				<< "\n";
			removeFromUniqueTextureFiles(resourceName);
		}
		src.close();

		// Do not forget to add any leftover textures which are not in the texture browser, but come from the Ogre sources
		// Note, that if any textures with the same base filename from different locations are overwritten by duplicate files (with the same base file name, but
		// different paths).
		// Keep the base name of the file unique to prevent this !
		std::vector<String>::iterator itTex = mUniqueTextureFiles.begin();
		std::vector<String>::iterator itTexEnd = mUniqueTextureFiles.end();
		String fileName;
		resourceId = maxResourceId;
		while (itTex != itTexEnd)
		{
			fileName = *itTex;

			// Write the entry
			if (!fileName.empty())
			{
				resourceId++;
				dst << topLevelId
					<< "\t"
					<< parentId
					<< "\t"
					<< resourceId
					<< "\t"
					<< resourceType
					<< "\t"
					<< fileName
					<< "\t"
					<< fileName
					<< "\n";
			}

			itTex++;
		}

		dst.close();
		mFileNamesDestination.push_back(fileNameTextureDestination);
	}

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::removeFromUniqueTextureFiles(const String& fileName)
	{
		std::vector<String>::iterator itDest = mUniqueTextureFiles.begin();
		std::vector<String>::iterator itDestEnd = mUniqueTextureFiles.end();
		String compareFileName = fileName;
		Ogre::StringUtil::toUpperCase(compareFileName);
		String destFileName;
		while (itDest != itDestEnd)
		{
			destFileName = *itDest;
			Ogre::StringUtil::toUpperCase(destFileName);
			if (Ogre::StringUtil::match(compareFileName, destFileName))
			{
				mUniqueTextureFiles.erase(itDest);
				return;
			}

			itDest++;
		}
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isDestinationFileAvailableInVector (const String& fileName)
	{
		std::vector<String>::iterator itDest = mFileNamesDestination.begin();
		std::vector<String>::iterator itDestEnd = mFileNamesDestination.end();
		String compareFileName = fileName;
		Ogre::StringUtil::toUpperCase(compareFileName);
		String destFileName;
		while (itDest != itDestEnd)
		{
			destFileName = *itDest;
			Ogre::StringUtil::toUpperCase(destFileName);
			if (Ogre::StringUtil::match(compareFileName, destFileName))
				return true;
			
			itDest++;
		}

		return false;
	}

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::copyFile(const String& fileNameSource, const String& fileNameDestination)
	{
		std::ifstream src(fileNameSource, std::ios::binary);
		std::ofstream dst(fileNameDestination, std::ios::binary);
		dst << src.rdbuf();
		dst.close();
		src.close();
	}

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::mySleep (clock_t sec)
	{
		clock_t start_time = clock();
		clock_t end_time = sec * 1000 + start_time;
		while (clock() != end_time);
	}
}