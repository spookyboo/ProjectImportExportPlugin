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
#include "unzip.h"
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

	#define WRITEBUFFERSIZE (262144)
	#define MAX_FILENAME 512
	#define READ_SIZE 32768

	static const String gImportMenuText = "Import HLMS Editor project";
	static const String gExportMenuText = "Export current HLMS Editor project";
	static String gTempString = "";
	//---------------------------------------------------------------------
	ProjectImportExportPlugin::ProjectImportExportPlugin()
	{
		mProperties.clear();
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
	bool ProjectImportExportPlugin::isImport(void) const
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
	unsigned int ProjectImportExportPlugin::getActionFlag(void)
	{
		// Import flags 
		// 1. Open a file dialog to selected the imported file
		// 2. Create the project directory
		// 3. Open a project file after import
		// 4. Save resource locations after import

		// Export flags
		// 5. Open a settings dialog before export
		// 6. Open a dialog to directory were the exported files are saved
		// 7. The HLMS Editor passes all texture filenames used by the datablocks in the material browser to the plugin
		return PAF_PRE_IMPORT_OPEN_FILE_DIALOG | 
			PAF_PRE_IMPORT_MK_DIR |
			PAF_POST_IMPORT_OPEN_PROJECT | 
			PAF_POST_IMPORT_SAVE_RESOURCE_LOCATIONS |
			PAF_PRE_EXPORT_SETTINGS_DIALOG |
			PAF_PRE_EXPORT_OPEN_DIR_DIALOG |
			PAF_PRE_EXPORT_TEXTURES_USED_BY_DATABLOCK;
	}
	//---------------------------------------------------------------------
	std::map<std::string, HlmsEditorPluginData::PLUGIN_PROPERTY> ProjectImportExportPlugin::getProperties(void)
	{
		// Include mesh files
		HlmsEditorPluginData::PLUGIN_PROPERTY property;
		property.propertyName = "include_meshes";
		property.labelName = "Add current mesh file to the project";
		property.info = "If this property is set to 'true' the current mesh is included in the zip.\n";
		property.type = HlmsEditorPluginData::BOOL;
		property.boolValue = false;
		mProperties[property.propertyName] = property;

		return mProperties;
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
		// As a result of the flag PAF_PRE_IMPORT_MK_DIR, the editor is triggered to create a subdir (in a platform independant way)

		// Error in case no materials available
		if (data->mInFileDialogName.empty())
		{
			data->mOutErrorText = "No import file selected";
			return false;
		}

		// Determine the destination path where the project files are copied; this is a newly created dir, based on the import (zip) file
		mProjectPath = data->mInImportPath + data->mInFileDialogBaseName + "/";

		// 1. Copy the zipfile to the target path
		String sourceZip = data->mInExportPath + data->mInFileDialogName;
		String baseName = sourceZip.substr(sourceZip.find_last_of("/\\") + 1);
		String destinationZip = mProjectPath + baseName;
		copyFile(sourceZip, destinationZip);

		// 1. Validate the selected project export file
		char zipFile[1024];
		memset(zipFile, 0, sizeof(char)*1024);
		strcpy(zipFile, destinationZip.c_str());
		if (!validateZip(zipFile, data))
			return false;

		// 2. Unzip the selected file to the created subdir (mProjectPath)
		if (!unzip(zipFile, data))
			return false;

		// 3 Remove the zip file, because it is not used anymore
		std::remove(destinationZip.c_str());

		// 4. Create the project file (.hlmp) with the references to the material- and texture cfg files
		if (!createProjectFileForImport(data))
		{
			data->mOutErrorText = "Could not create project file";
			return false;
		}

		// 5. Re-create the material cfg file with the mProjectPath
		if (!createMaterialCfgFileForImport(data))
		{
			data->mOutErrorText = "Could not create materials file";
			return false;
		}

		// 6. Re-create the texture cfg file with the mProjectPath
		if (!createTextureCfgFileForImport(data))
		{
			data->mOutErrorText = "Could not create textures file";
			return false;
		}

		// 7. Re-create the meshes cfg file with the mProjectPath if the file exists
		createMeshesCfgFileForImport(data);

		// 8. Add the subdir - containing the upzipped project files - to the Ogre resources (and update resources.cfg)
		// Note, that mProjectPath cannot be used, because it contains a trailing '/'
		// The flag PAF_POST_IMPORT_SAVE_RESOURCE_LOCATIONS triggers the editor to perform the save action (which is already implemented by the editor)
		Root* root = Root::getSingletonPtr();
		root->addResourceLocation(data->mInImportPath + data->mInFileDialogBaseName, "FileSystem", "General");

		// 9. Open the .hlmp project file (must be done by the editor)
		// he flag PAF_POST_IMPORT_OPEN_PROJECT triggers the editor to perform the 'load project' action
		data->mOutReference = mFileNameProject;

		return true;
	}
	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::executeExport (HlmsEditorPluginData* data)
	{
		mFileNamesDestination.clear();
		mUniqueTextureFiles.clear();

		// Do not quit when data->mInTexturesUsedByDatablocks and/or data->mInMaterialFileNameVector is empty!!

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
			baseName = fileNameSource.substr(fileNameSource.find_last_of("/\\") + 1);
			fileNameDestination = data->mInExportPath + baseName;
			if (!isDestinationFileAvailableInVector(fileNameDestination))
			{
				mFileNamesDestination.push_back(fileNameDestination); // Only push unique names
				mUniqueTextureFiles.push_back(baseName);
			}
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
			fileNameTextureDestination = data->mInExportPath + baseNameTexture;
			if (!isDestinationFileAvailableInVector(fileNameTextureDestination))
			{
				mFileNamesDestination.push_back(fileNameTextureDestination); // Only push unique names
				mUniqueTextureFiles.push_back(baseNameTexture);
			}
			copyFile(fileNameTextureSource, fileNameTextureDestination);
		}

		// 3. Copy all Json (material) files
		itStart = materials.begin();
		itEnd = materials.end();
		String thumbFileNameSource;
		String thumbFileNameDestination;
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
			baseName = fileName.substr(fileName.find_last_of("/\\") + 1);
			fileNameDestination = data->mInExportPath + baseName;
			mFileNamesDestination.push_back(fileNameDestination);
			copyFile(fileName, fileNameDestination);

			// Copy the thumb files
			thumbFileNameSource = "../common/thumbs/" + baseName + ".png";
			thumbFileNameDestination = data->mInExportPath + baseName + ".png";
			mFileNamesDestination.push_back(thumbFileNameDestination);
			copyFile(thumbFileNameSource, thumbFileNameDestination);
		}

		// 4. Create project file for export (without paths)
		createProjectFileForExport(data);

		// 5. Create material config file for export (without paths)
		// This file does not contain any path info; when the exported project is imported again, the 
		// material cfg file is enriched with the path of the import directory
		createMaterialCfgFileForExport(data);

		// 6. Create texture config file for export (without paths)
		// This file does not contain any path info; when the exported project is imported again, the 
		// texture cfg file is enriched with the path of the import directory
		createTextureCfgFileForExport(data);

		// 7. (Optional) copy current meshes to the export
		std::map<std::string, Ogre::HlmsEditorPluginData::PLUGIN_PROPERTY> properties = data->mInPropertiesMap;
		std::map<std::string, Ogre::HlmsEditorPluginData::PLUGIN_PROPERTY>::iterator itProperties = properties.find("include_meshes");
		String fileNameMesh;
		String fileNameMeshSource;
		if (itProperties != properties.end())
		{
			// Property found; determine its value
			if ((itProperties->second).boolValue)
			{
				if (data->mInMeshFileNames.size() > 0)
				{
					// Copy meshes
					std::vector<String> meshes;
					meshes = data->mInMeshFileNames;
					std::vector<String>::iterator itMeshes;
					std::vector<String>::iterator itStartMeshes = meshes.begin();
					std::vector<String>::iterator itEndMeshes = meshes.end();
					for (itMeshes = itStartMeshes; itMeshes != itEndMeshes; ++itMeshes)
					{
						// Get the filename of the meshes
						fileNameMesh = *itMeshes;
						fileNameMeshSource = fileNameMesh;
						if (!fileNameMesh.empty())
						{
							// Copy the meshs file(s)
							baseName = fileNameMesh.substr(fileNameMesh.find_last_of("/\\") + 1);
							fileNameDestination = data->mInExportPath + baseName;
							mFileNamesDestination.push_back(fileNameDestination);
							copyFile(fileNameMeshSource, fileNameDestination);
						}
					}
					
					// 8. Create meshes config file for export (without paths)
					// This file does not contain any path info; when the exported project is imported again, the 
					// texture cfg file is enriched with the path of the import directory
					createMeshesCfgFileForExport(data);
				}
			}
		}

		// 9. Zip all files
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
		String zipName = data->mInExportPath + data->mInProjectName + ".hlmp.zip";
		char zipFile[1024];
		memset(zipFile, 0, sizeof(char) * 1024);
		char filenameInZip[1024];
		memset(filenameInZip, 0, sizeof(char) * 1024);
		strcpy(zipFile, zipName.c_str());

//#ifdef USEWIN32IOAPI
		//zlib_filefunc64_def ffunc;
		//fill_win32_filefunc64A(&ffunc);
		//zf = zipOpen2_64(zipFile, 0, NULL, &ffunc);
//#else
		//zf = zipOpen(zipFile, 0);
		zf = zipOpen64(zipFile, 0);
//#endif

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

		// Remark: Deleting the copied files here results in a corrupted zip file, so put that as a separate post-export action

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
			hlmsJson.loadMaterials(fileName, 
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, 
				jsonAsChar); // The fileName is only used for logging and has no purpose
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
	bool ProjectImportExportPlugin::unzip (const char* zipfilename, HlmsEditorPluginData* data)
	{
		// Open the zip file
		unzFile zipfile;
		//zipfile = unzOpen(zipfilename);
		zipfile = unzOpen64(zipfilename);
		if (zipfile == NULL)
		{
			data->mOutErrorText = "Error while opening import file: " + data->mInExportPath + data->mInFileDialogName;
			return false;
		}

		// Get info about the zip file
		unz_global_info global_info;
		if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK)
		{
			data->mOutErrorText = "Error while readinf import file";
			unzClose(zipfile);
			return false;
		}

		// Buffer to hold data read from the zip file.
		char read_buffer[READ_SIZE];

		// Loop to extract all files
		uLong i;
		for (i = 0; i < global_info.number_entry; ++i)
		{
			// Get info about current file.
			unz_file_info file_info;
			char filename[MAX_FILENAME];
			if (unzGetCurrentFileInfo(
				zipfile,
				&file_info,
				filename,
				MAX_FILENAME,
				NULL, 0, NULL, 0) != UNZ_OK)
			{
				data->mOutErrorText = "Error while reading info file";
				unzClose(zipfile);
				return false;
			}

			// Entry is always a file, so extract it.
			if (unzOpenCurrentFile(zipfile) != UNZ_OK)
			{
				data->mOutErrorText = "Could not open a file in the import";
				unzClose(zipfile);
				return false;
			}

			// Open a file to write out the data.
			String f(filename);
			f = mProjectPath + f;
			FILE *out = fopen(f.c_str(), "wb");
			if (out == NULL)
			{
				data->mOutErrorText = "Could not create a destination file";
				unzCloseCurrentFile(zipfile);
				unzClose(zipfile);
				return false;
			}

			int error = UNZ_OK;
			do
			{
				error = unzReadCurrentFile(zipfile, read_buffer, READ_SIZE);
				if (error < 0)
				{
					data->mOutErrorText = "Error while creating file";
					unzCloseCurrentFile(zipfile);
					unzClose(zipfile);
					return false;
				}

				// Write data to file.
				if (error > 0)
				{
					fwrite(read_buffer, error, 1, out); // You should check return of fwrite...
				}
			} while (error > 0);

			fclose(out);
			unzCloseCurrentFile(zipfile);

			// Go the the next entry listed in the zip file.
			if ((i + 1) < global_info.number_entry)
			{
				if (unzGoToNextFile(zipfile) != UNZ_OK)
				{
					data->mOutErrorText = "Could not read next file in import";
					unzClose(zipfile);
					return false;
				}
			}
		}

		unzClose(zipfile);
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::validateZip(const char* zipfilename, HlmsEditorPluginData* data)
	{
		// Check whether the zip file is a valid HLMS Editor project export
		// This means that at least the files 'project.txt', 'materials.cfg' and 'textures.cfg'
		// must be present
		bool projectPresent = false;
		bool materialsPresent = false;
		bool texturesPresent = false;

		// Open the zip file
		unzFile zipfile;
		//zipfile = unzOpen(zipfilename);
		zipfile = unzOpen64(zipfilename);
		if (zipfile == NULL)
		{
			data->mOutErrorText = "Error while opening import file: " + data->mInExportPath + data->mInFileDialogName;
			return false;
		}

		// Get info about the zip file
		unz_global_info global_info;
		if (unzGetGlobalInfo(zipfile, &global_info) != UNZ_OK)
		{
			data->mOutErrorText = "Error while readinf import file";
			unzClose(zipfile);
			return false;
		}

		// Loop to extract all files
		uLong i;
		for (i = 0; i < global_info.number_entry; ++i)
		{
			// Get info about current file.
			unz_file_info file_info;
			char filename[MAX_FILENAME];
			if (unzGetCurrentFileInfo(
				zipfile,
				&file_info,
				filename,
				MAX_FILENAME,
				NULL, 0, NULL, 0) != UNZ_OK)
			{
				data->mOutErrorText = "Error while reading info file";
				unzClose(zipfile);
				return false;
			}

			// Check the name
			String f(filename);
			if (Ogre::StringUtil::match(f, "project.txt"))
				projectPresent = true;
			if (Ogre::StringUtil::match(f, "materials.cfg"))
				materialsPresent = true;
			if (Ogre::StringUtil::match(f, "textures.cfg"))
				texturesPresent = true;

			// Go the the next entry listed in the zip file.
			if ((i + 1) < global_info.number_entry)
			{
				if (unzGoToNextFile(zipfile) != UNZ_OK)
				{
					data->mOutErrorText = "Could not read next file in import";
					unzClose(zipfile);
					return false;
				}
			}
		}

		unzClose(zipfile);

		if (projectPresent && materialsPresent && texturesPresent)
			return true;

		data->mOutErrorText = "File is not a valid project export";
		return false;
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

			//printf("File : %s is %lld bytes\n", filename, pos);

			if (pos >= 0xffffffff)
				largeFile = 1;

			fclose(pFile);
		}

		return largeFile;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createProjectFileForImport (HlmsEditorPluginData* data)
	{
		// File projects.txt must exist
		String fileName = mProjectPath + "project.txt";
		std::ifstream src(fileName);
		if (!src)
			return false;

		// Get the project name
		mNameProject = "";
		std::getline (src, mNameProject);

		// Write the project file
		mFileNameProject = mProjectPath + mNameProject + ".hlmp";
		mFileNameMaterials = mProjectPath + mNameProject + "_materials.cfg";
		mFileNameTextures = mProjectPath + mNameProject + "_textures.cfg";
		mFileNameMeshes = mProjectPath + mNameProject + "_meshes.cfg";
		std::ofstream dst(mFileNameProject);
		dst << "hlmsEditor v1.0\n";
		dst << mFileNameMaterials
			<< "\n";
		dst << mFileNameTextures
			<< "\n";

		// Only write the entry when the meshes.cfg is available in the .zip file
		if (isMeshesCfgFileForImport(data))
		{
			dst << mFileNameMeshes
				<< "\n";
		}

		src.close();
		dst.close();

		// Remove the project.txt, because it is not used anymore
		std::remove(fileName.c_str());

		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createMaterialCfgFileForImport(HlmsEditorPluginData* data)
	{
		// File materials.cfg must exist
		String materialsName = mProjectPath + "materials.cfg";
		std::ifstream src(materialsName);
		if (!src)
			return false;

		// Read materials.cfg and create <project>_materials.cfg, including path
		std::ofstream dst(mFileNameMaterials);
		std::string line;
		int topLevelId;
		int parentId;
		int resourceId;
		int resourceType;
		String resourceName;
		String fullQualifiedName;
		String thumbFileNameSource;
		String thumbFileNameDestination;
		while (std::getline(src, line))
		{
			// Read
			std::istringstream iss(line);
			iss >> topLevelId
				>> parentId
				>> resourceId
				>> resourceType
				>> resourceName
				>> fullQualifiedName;
			if (resourceType == 3)
			{
				// Enrich the fullQualifiedName
				fullQualifiedName = mProjectPath + fullQualifiedName; // Only enrich type = 3 (assets) and not groups

				// Copy the thumb images
				thumbFileNameSource = mProjectPath + resourceName + ".png";
				thumbFileNameDestination = "../common/thumbs/" + resourceName + ".png";
				copyFile(thumbFileNameSource, thumbFileNameDestination);
				std::remove(thumbFileNameSource.c_str());
			}

			// Write
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
				<< fullQualifiedName
				<< "\n";
		}
		src.close();
		dst.close();

		// Remove the materials.txt, because it is not used anymore
		std::remove(materialsName.c_str());
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createTextureCfgFileForImport(HlmsEditorPluginData* data)
	{
		// File textures.cfg must exist
		String texturesName = mProjectPath + "textures.cfg";
		std::ifstream src(texturesName);
		if (!src)
			return false;

		// Read materials.cfg and create <project>_materials.cfg, including path
		std::ofstream dst(mFileNameTextures);
		std::string line;
		int topLevelId;
		int parentId;
		int resourceId;
		int resourceType;
		String resourceName;
		String fullQualifiedName;
		while (std::getline(src, line))
		{
			std::istringstream iss(line);
			iss >> topLevelId
				>> parentId
				>> resourceId
				>> resourceType
				>> resourceName
				>> fullQualifiedName;
			if (resourceType == 3)
				fullQualifiedName = mProjectPath + fullQualifiedName; // Only enrich type = 3 (assets) and not groups
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
				<< fullQualifiedName
				<< "\n";
		}
		src.close();
		dst.close();

		// Remove the textures.txt, because it is not used anymore
		std::remove(texturesName.c_str());
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::isMeshesCfgFileForImport (HlmsEditorPluginData* data)
	{
		bool result = false;
		String meshesName = mProjectPath + "meshes.cfg";
		std::ifstream src(meshesName);
		if (src)
			result = true;

		src.close();
		return result;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createMeshesCfgFileForImport (HlmsEditorPluginData* data)
	{
		// File meshes.cfg is optional
		String meshesName = mProjectPath + "meshes.cfg";
		std::ifstream src(meshesName);
		if (!src)
			return true; // Always return true, because it is optional

		// Read meshes.cfg and create <project>_meshes.cfg, including path
		std::ofstream dst(mFileNameMeshes);
		std::string line;
		while (std::getline(src, line))
		{
			line = mProjectPath + line;
			dst << line;
		}
		src.close();
		dst.close();

		// Remove the meshes.txt, because it is not used anymore
		std::remove(meshesName.c_str());
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createProjectFileForExport(HlmsEditorPluginData* data)
	{
		// Add a project.txt to the zip, containing the projectname
		// This is only to set the name of the project. This name is used later when the .zip file is imported again
		String fileName = data->mInExportPath + "project.txt";
		std::ofstream file(fileName);
		file << data->mInProjectName;
		mFileNamesDestination.push_back(fileName);
		file.close();
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createMaterialCfgFileForExport(HlmsEditorPluginData* data)
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
		String fileNameMaterialDestination = data->mInExportPath + "materials.cfg";
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
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createTextureCfgFileForExport (HlmsEditorPluginData* data)
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
		String baseNameTexture;
		String fileNameTextureDestination = data->mInExportPath + "textures.cfg";
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

			// Strip the path from the resource
			baseNameTexture = resourceName.substr(resourceName.find_last_of("/\\") + 1);
			dst << topLevelId
				<< "\t"
				<< parentId
				<< "\t"
				<< resourceId
				<< "\t"
				<< resourceType
				<< "\t"
				<< baseNameTexture
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
		resourceType = 3; // It must be an asset
		while (itTex != itTexEnd)
		{
			fileName = *itTex;

			// Write the entry
			if (!fileName.empty())
			{
				resourceId++;
				dst << topLevelId
					<< "\t"
					<< topLevelId
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
		return true;
	}

	//---------------------------------------------------------------------
	bool ProjectImportExportPlugin::createMeshesCfgFileForExport (HlmsEditorPluginData* data)
	{
		// Create the meshes cfg file with all unique meshes. For nowm this is just one mesh
		String fileNameMeshesSource = data->mInMeshesFileName;
		String baseNameMeshes = fileNameMeshesSource.substr(fileNameMeshesSource.find_last_of("/\\") + 1);
		String fileNameMeshesDestination = data->mInExportPath + "meshes.cfg";
		std::ofstream dst(fileNameMeshesDestination);

		std::vector<String> meshes;
		meshes = data->mInMeshFileNames;
		std::vector<String>::iterator itMeshes;
		std::vector<String>::iterator itStartMeshes = meshes.begin();
		std::vector<String>::iterator itEndMeshes = meshes.end();
		String fileNameMesh;
		String baseName;
		for (itMeshes = itStartMeshes; itMeshes != itEndMeshes; ++itMeshes)
		{
			// Get the filename of the meshes
			fileNameMesh = *itMeshes;
			if (!fileNameMesh.empty())
			{
				baseName = fileNameMesh.substr(fileNameMesh.find_last_of("/\\") + 1);
				dst << baseName << "\n";
			}
		}

		dst.close();
		mFileNamesDestination.push_back(fileNameMeshesDestination);
		return true;
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
				itDest = mUniqueTextureFiles.erase(itDest);
				itDestEnd = mUniqueTextureFiles.end();
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
		if (Ogre::StringUtil::match(fileNameSource, fileNameDestination))
			return;

		std::ifstream src(fileNameSource.c_str(), std::ios::binary);
		std::ofstream dst(fileNameDestination.c_str(), std::ios::binary);
		dst << src.rdbuf();
		dst.close();
		src.close();
		//LogManager::getSingleton().logMessage("Copied files: " + fileNameSource + " to " + fileNameDestination);
	}

	//---------------------------------------------------------------------
	void ProjectImportExportPlugin::mySleep (clock_t sec)
	{
		clock_t start_time = clock();
		clock_t end_time = sec * 1000 + start_time;
		while (clock() != end_time);
	}
}
