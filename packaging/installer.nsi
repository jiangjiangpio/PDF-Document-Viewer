Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!ifndef PAYLOAD
 !define PAYLOAD "../build-windows/package"
!endif
!ifndef OUTPUT
 !define OUTPUT "../artifacts/PaperView-Setup-0.1.0-x64.exe"
!endif
Name "PaperView"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\PaperView"
InstallDirRegKey HKCU "Software\PaperView" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "app.ico"
UninstallIcon "app.ico"
VIProductVersion "0.1.0.0"
VIAddVersionKey /LANG=2052 "LegalCopyright" "PaperView contributors"
VIAddVersionKey /LANG=2052 "ProductName" "PaperView"
VIAddVersionKey /LANG=2052 "FileDescription" "PaperView Windows x64 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "0.1.0"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "安装 PaperView"
!define MUI_WELCOMEPAGE_TEXT "适用于 Windows 10 / 11 的本地 PDF 阅读器。支持触控、多标签和自动保存批注。安装不需要管理员权限。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\PaperView.exe"
!define MUI_FINISHPAGE_RUN_TEXT "打开 PaperView"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Function .onInit
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP "PaperView 需要 64 位 Windows 10 或更新版本。"
  Abort
 ${EndIf}
 ${IfNot} ${AtLeastWin10}
  MessageBox MB_ICONSTOP "PaperView 需要 Windows 10 或更新版本。"
  Abort
 ${EndIf}
 SetRegView 64
 ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuildNumber"
 ${If} $0 != ""
  ${If} $0 < 17763
   MessageBox MB_ICONSTOP "请先将 Windows 10 更新到 1809 或更新版本。"
   Abort
  ${EndIf}
 ${EndIf}
 FindWindow $0 "PaperViewMain"
 ${If} $0 != 0
  MessageBox MB_ICONEXCLAMATION "请先关闭 PaperView，再运行安装程序。"
  Abort
 ${EndIf}
FunctionEnd
Section "PaperView" SEC_MAIN
 SetRegView 64
 SetOutPath "$INSTDIR"
 File /r "${PAYLOAD}/*"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 CreateDirectory "$SMPROGRAMS\PaperView"
 CreateShortcut "$SMPROGRAMS\PaperView\PaperView.lnk" "$INSTDIR\PaperView.exe"
 CreateShortcut "$SMPROGRAMS\PaperView\卸载 PaperView.lnk" "$INSTDIR\Uninstall.exe"
 CreateShortcut "$DESKTOP\PaperView.lnk" "$INSTDIR\PaperView.exe"
 WriteRegStr HKCU "Software\PaperView" "InstallDir" "$INSTDIR"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "DisplayName" "PaperView"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "DisplayVersion" "0.1.0"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "DisplayIcon" "$INSTDIR\PaperView.exe"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "UninstallString" '"$INSTDIR\Uninstall.exe"'
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "NoModify" 1
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView" "NoRepair" 1
 WriteRegStr HKCU "Software\Classes\Applications\PaperView.exe\shell\open\command" "" '"$INSTDIR\PaperView.exe" "%1"'
 WriteRegStr HKCU "Software\Classes\Applications\PaperView.exe\SupportedTypes" ".pdf" ""
 WriteRegStr HKCU "Software\Classes\PaperView.PDF" "" "PDF 文档（PaperView）"
 WriteRegStr HKCU "Software\Classes\PaperView.PDF\shell\open\command" "" '"$INSTDIR\PaperView.exe" "%1"'
 WriteRegStr HKCU "Software\Classes\.pdf\OpenWithProgids" "PaperView.PDF" ""
SectionEnd
Function un.onInit
 SetRegView 64
 FindWindow $0 "PaperViewMain"
 ${If} $0 != 0
  MessageBox MB_ICONEXCLAMATION "请先关闭 PaperView，再卸载。"
  Abort
 ${EndIf}
FunctionEnd
Section "Uninstall"
 SetRegView 64
 Delete "$DESKTOP\PaperView.lnk"
 Delete "$SMPROGRAMS\PaperView\PaperView.lnk"
 Delete "$SMPROGRAMS\PaperView\卸载 PaperView.lnk"
 RMDir "$SMPROGRAMS\PaperView"
 Delete "$INSTDIR\PaperView.exe"
 Delete "$INSTDIR\pdfium.dll"
 Delete "$INSTDIR\使用说明.txt"
 Delete "$INSTDIR\THIRD_PARTY.txt"
 Delete "$INSTDIR\Uninstall.exe"
 RMDir /r "$INSTDIR\ThirdPartyLicenses"
 RMDir "$INSTDIR"
 DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PaperView"
 DeleteRegKey HKCU "Software\Classes\Applications\PaperView.exe"
 DeleteRegKey HKCU "Software\Classes\PaperView.PDF"
 DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "PaperView.PDF"
 DeleteRegKey HKCU "Software\PaperView"
 ; Intentionally preserve user annotations under LocalAppData\PaperView.
SectionEnd
