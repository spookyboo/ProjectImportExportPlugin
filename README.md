# Project Import Export plugin

The __Project Import Export Plugin__ is a plugin for the HLMS Editor and exports a complete project in an intermediate zip format (including textures, config files, etc.).
This __Project Import Export Plugin__ Github repository contains Visual Studio ProjectImportExport.sln / ProjectImportExport.vcxproj files for convenience (do not forget to 
change the properties to the correct include files, because it makes use of both HLMS Editor and Ogre3d include files).
The __Project Import Export Plugin__ makes use of the generic plugin mechanism of Ogre3D.

**Installation:**  
Just add the plugin entry _Plugin=ProjectImportExport_ to the plugins.cfg file (under HLMSEditor/bin); the HLMS Editor recognizes whether it is a valid plugin.  
Important note: The plugin only works when you create directory __HLMSEditor/import__. This directory contains all imported projects. Also note, that this directory can by changed
in the file __HLMSEditor/bin/settings.cfg__