; Base Jase Plugins - Vinyl Studio (Inno Setup 6). Called by build.bat.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\VST3"
#endif
#ifndef OutDir
  #define OutDir "..\..\dist"
#endif
[Setup]
AppId={{6B1E7A52-3C0D-4E63-9A51-BA5E1A5E0001}
AppName=Vinyl Studio
AppVersion={#AppVersion}
AppPublisher=Base Jase Plugins
AppPublisherURL=https://basejaseplugins.com
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutDir}
OutputBaseFilename=BaseJasePlugins-VinylStudio-{#AppVersion}-Setup
WizardStyle=modern
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard_small.bmp
Compression=lzma2
SolidCompression=yes
Uninstallable=yes
UninstallFilesDir={commonappdata}\Base Jase Plugins\Vinyl Studio
[Messages]
WelcomeLabel2=This installs the Base Jase Plugins Vinyl Studio VST3 plugins (Stereo, Mono, Lite, Lite Mono) into the common VST3 folder.%n%nClose your DAW before you continue. Some DAWs need a plugin rescan afterwards.
[Files]
Source: "{#SourceDir}\*"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion recursesubdirs createallsubdirs
[Dirs]
Name: "{userdocs}\Base Jase Plugins\Vinyl Studio\Presets"
