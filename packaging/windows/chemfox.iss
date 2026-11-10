; Inno Setup script for ChemFox. Built by deploy.ps1 -Installer, which passes:
;   /DAppVersion=x.y.z  /DSourceDir=<dist\ChemFox>  /DOutputDir=<dist>
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; A fixed id ties upgrades and the uninstaller together; never change it.
AppId={{0CFB8206-DF21-4CD8-985C-CAE0E269E87C}
AppName=ChemFox
AppVersion={#AppVersion}
AppVerName=ChemFox {#AppVersion}
AppPublisher=ChemFox
DefaultDirName={autopf}\ChemFox
DefaultGroupName=ChemFox
DisableProgramGroupPage=yes
; Installs for the current user without an admin prompt; "Install for all users" is offered.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=ChemFox-{#AppVersion}-setup
SetupIconFile=..\icon\chemfox.ico
UninstallDisplayIcon={app}\ChemFox.exe
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "associate"; Description: "Open .cmfx files with ChemFox"; GroupDescription: "Files:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\ChemFox"; Filename: "{app}\ChemFox.exe"
Name: "{group}\ChemFox samples"; Filename: "{app}\samples"
Name: "{autodesktop}\ChemFox"; Filename: "{app}\ChemFox.exe"; Tasks: desktopicon

[Registry]
; .cmfx (and the older .chemfox and .chemdoc) open in ChemFox.
Root: HKA; Subkey: "Software\Classes\.cmfx"; ValueType: string; ValueName: ""; ValueData: "ChemFox.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.chemfox"; ValueType: string; ValueName: ""; ValueData: "ChemFox.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.chemdoc"; ValueType: string; ValueName: ""; ValueData: "ChemFox.Document"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\ChemFox.Document"; ValueType: string; ValueName: ""; ValueData: "ChemFox document"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\ChemFox.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\ChemFox.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\ChemFox.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\ChemFox.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\ChemFox.exe"; Description: "Start ChemFox"; Flags: nowait postinstall skipifsilent
