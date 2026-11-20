; Build HP8620-Control.exe and deploy its Qt runtime with build.ps1 before compiling.
; The NI-488.2 driver or the Elektor adapter USB serial driver is installed separately.
#define ReadQuoted(Text) Copy(Text, Pos('"', Text) + 1, Pos('"', Copy(Text, Pos('"', Text) + 1, Len(Text))) - 1)
#define VersionHeader FileOpen(SourcePath + "version.h")
#expr FileRead(VersionHeader)
#expr FileRead(VersionHeader)
#define Hp8620Version ReadQuoted(FileRead(VersionHeader))
#expr FileClose(VersionHeader)
#define IconResource FileOpen(SourcePath + "assets\app.rc")
#define Hp8620Icon ReadQuoted(FileRead(IconResource))
#expr FileClose(IconResource)
#define Hp8620Name "HP8620 Control"
#define Hp8620Exe "HP8620-Control.exe"

[Setup]
AppId={{6C76E0F3-ED7D-48F7-BF6F-92A43C0CD57E}
AppName={#Hp8620Name}
AppVersion={#Hp8620Version}
AppVerName={#Hp8620Name} {#Hp8620Version}
AppPublisher=Ralf Tralow
VersionInfoVersion={#Hp8620Version}
VersionInfoDescription={#Hp8620Name} Setup
DefaultDirName={pf}\{#Hp8620Name}
DefaultGroupName={#Hp8620Name}
AllowNoIcons=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Inno 5.3.5 can see modern Windows as 6.2 due to its legacy manifest.
SetupIconFile=assets\{#Hp8620Icon}
UninstallDisplayIcon={app}\{#Hp8620Exe}
OutputDir=installer
OutputBaseFilename=HP8620-Control-Setup-{#Hp8620Version}-x64
Compression=lzma
SolidCompression=yes

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "build\{#Hp8620Exe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\generic\*.dll"; DestDir: "{app}\generic"; Flags: ignoreversion
Source: "build\iconengines\*.dll"; DestDir: "{app}\iconengines"; Flags: ignoreversion
Source: "build\imageformats\*.dll"; DestDir: "{app}\imageformats"; Flags: ignoreversion
Source: "build\networkinformation\*.dll"; DestDir: "{app}\networkinformation"; Flags: ignoreversion
Source: "build\platforms\*.dll"; DestDir: "{app}\platforms"; Flags: ignoreversion
Source: "build\styles\*.dll"; DestDir: "{app}\styles"; Flags: ignoreversion
Source: "build\tls\*.dll"; DestDir: "{app}\tls"; Flags: ignoreversion
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "licenses\*.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\{#Hp8620Name}"; Filename: "{app}\{#Hp8620Exe}"; WorkingDir: "{app}"
Name: "{commondesktop}\{#Hp8620Name}"; Filename: "{app}\{#Hp8620Exe}"; WorkingDir: "{app}"; Tasks: desktopicon
