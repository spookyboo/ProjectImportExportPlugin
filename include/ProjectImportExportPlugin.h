/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

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
#ifndef __ProjectImportExport_H__
#define __ProjectImportExport_H__

#include "ProjectImportExportPluginPrerequisites.h"
#include "OgrePlugin.h"
#include "hlms_editor_plugin.h"
#include "unzip.h"

namespace Ogre
{
    /** Plugin instance for Project Import Export Plugin */
    class ProjectImportExportPlugin : public Plugin, public HlmsEditorPlugin
    {
		public:
			ProjectImportExportPlugin();
		
			/// @copydoc Plugin::getName
			const String& getName() const;

			/// @copydoc Plugin::install
			void install();

			/// @copydoc Plugin::initialise
			void initialise();

			/// @copydoc Plugin::shutdown
			void shutdown();

			/// @copydoc Plugin::uninstall
			void uninstall();

			// Implement HlmsEditorPlugin functions
			virtual bool isOpenFileDialogForImport(void) const;
			virtual bool isOpenFileDialogForExport(void) const;
			virtual bool isTexturesUsedByDatablocksForExport(void) const;
			virtual bool isImport (void) const;
			virtual bool isExport (void) const;
			virtual const String& getImportMenuText (void) const;
			virtual const String& getExportMenuText (void) const;
			virtual bool executeImport(HlmsEditorPluginData* data);
			virtual bool executeExport(HlmsEditorPluginData* data);
			virtual void performPreImportActions(void);
			virtual void performPostImportActions(void);
			virtual void performPreExportActions(void);
			virtual void performPostExportActions(void);
			virtual unsigned int getActionFlag(void);

		protected:
			bool loadMaterial(const String& fileName);
			const String& getFullFileNameFromTextureList(const String& baseName, HlmsEditorPluginData* data);
			const String& getFullFileNameFromResources(const String& baseName, HlmsEditorPluginData* data);
			bool unzip(const char* filename, HlmsEditorPluginData* data);
			int isLargeFile(const char* filename);
			bool createProjectFileForImport(HlmsEditorPluginData* data);
			bool createProjectFileForExport(HlmsEditorPluginData* data);
			bool createMaterialCfgFileForImport(HlmsEditorPluginData* data); // Used to create a material file WITH paths in the file
			bool createMaterialCfgFileForExport(HlmsEditorPluginData* data); // Used to create a base material file without paths in the file
			bool createTextureCfgFileForImport(HlmsEditorPluginData* data); // Used to create a texture file WITH paths in the file
			bool createTextureCfgFileForExport(HlmsEditorPluginData* data); // Used to create a base texture file without paths in the file
			void removeFromUniqueTextureFiles(const String& fileName);
			bool isDestinationFileAvailableInVector (const String& fileName);
			void copyFile(const String& fileNameSource, const String& fileNameDestination);
			void mySleep(clock_t sec);

		private:
			std::vector<String> mFileNamesDestination;
			std::vector<String> mUniqueTextureFiles; // List of all texture files in the zip
			String mProjectPath;
			String mNameProject;
			String mFileNameProject;
			String mFileNameMaterials;
			String mFileNameTextures;

	};
}

#endif
