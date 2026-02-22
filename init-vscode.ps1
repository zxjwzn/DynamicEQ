<#
.SYNOPSIS
    为 JUCE Projucer 项目自动生成 VS Code 开发配置 (.vscode)

.DESCRIPTION
    读取 .jucer 文件，自动检测项目名、JUCE 模块路径、VS 版本等信息，
    生成 tasks.json / launch.json / c_cpp_properties.json / settings.json。
    支持所有标准 Projucer 导出的 Visual Studio 项目。

.PARAMETER ProjectDir
    JUCE 项目根目录（包含 .jucer 文件的目录）。默认为当前目录。

.EXAMPLE
    .\init-vscode.ps1
    .\init-vscode.ps1 -ProjectDir "D:\MyOtherPlugin"
#>

param(
    [string]$ProjectDir = (Get-Location).Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ==================== 查找 .jucer 文件 ====================
$jucerFile = Get-ChildItem -Path $ProjectDir -Filter "*.jucer" -File | Select-Object -First 1
if (-not $jucerFile) {
    Write-Error "在 '$ProjectDir' 下未找到 .jucer 文件"
    exit 1
}

Write-Host "[*] 解析: $($jucerFile.FullName)" -ForegroundColor Cyan
[xml]$jucer = Get-Content -Path $jucerFile.FullName -Encoding UTF8

$projectName = $jucer.JUCERPROJECT.name
Write-Host "[*] 项目名: $projectName" -ForegroundColor Cyan

# ==================== 查找 VS 导出格式 ====================
$exportFormats = $jucer.JUCERPROJECT.EXPORTFORMATS
$vsNode = $null
$vsVersion = ""

# 按优先级搜索 VS2026 > VS2022 > VS2019 > VS2017
foreach ($ver in @("VS2026", "VS2022", "VS2019", "VS2017")) {
    $node = $exportFormats.SelectSingleNode($ver)
    if ($node) {
        $vsNode = $node
        $vsVersion = $ver
        break
    }
}

if (-not $vsNode) {
    Write-Error "未找到 Visual Studio 导出格式（支持 VS2017/2019/2022/2026）"
    exit 1
}

$targetFolder = $vsNode.targetFolder
Write-Host "[*] VS 版本: $vsVersion, 构建目录: $targetFolder" -ForegroundColor Cyan

# ==================== 读取 JUCE 模块路径 ====================
$modulePaths = @()
foreach ($mp in $vsNode.MODULEPATHS.ChildNodes) {
    if ($mp.path) {
        $modulePaths += $mp.path
    }
}
$uniqueModulePaths = $modulePaths | Sort-Object -Unique

# 转为绝对路径 (基于 .jucer 所在目录)
$juceModulesAbsolute = @()
foreach ($relPath in $uniqueModulePaths) {
    $absPath = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir $relPath))
    if (Test-Path $absPath) {
        $juceModulesAbsolute += $absPath.Replace("\", "/")
    }
}
$juceModulesAbsolute = $juceModulesAbsolute | Sort-Object -Unique

Write-Host "[*] JUCE 模块路径: $($juceModulesAbsolute -join ', ')" -ForegroundColor Cyan

# ==================== 查找 MSBuild (amd64) ====================
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuildPath = ""
if (Test-Path $vswhere) {
    $vsInstallPath = & $vswhere -all -prerelease -latest -property installationPath 2>$null
    if ($vsInstallPath) {
        $candidate = Join-Path $vsInstallPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
        if (Test-Path $candidate) {
            $msbuildPath = $candidate
        }
        else {
            $candidate = Join-Path $vsInstallPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $candidate) {
                $msbuildPath = $candidate
            }
        }
    }
}

if (-not $msbuildPath) {
    Write-Warning "未自动检测到 MSBuild，请手动修改 tasks.json 中的 MSBUILD_PATH"
    $msbuildPath = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\amd64\\MSBuild.exe"
}
Write-Host "[*] MSBuild: $msbuildPath" -ForegroundColor Cyan

# ==================== 查找 cl.exe ====================
$clPath = ""
if ($vsInstallPath) {
    $clCandidates = Get-ChildItem -Path (Join-Path $vsInstallPath "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($clCandidates) {
        $clPath = $clCandidates.FullName.Replace("\", "/")
    }
}

if (-not $clPath) {
    Write-Warning "未自动检测到 cl.exe，请手动修改 c_cpp_properties.json 中的 compilerPath"
    $clPath = ""
}
Write-Host "[*] cl.exe: $clPath" -ForegroundColor Cyan

# ==================== 查找 Windows SDK 完整版本号 ====================
$windowsSdkVersion = "10.0.22621.0"
$sdkIncludeDir = "${env:ProgramFiles(x86)}\Windows Kits\10\Include"
if (Test-Path $sdkIncludeDir) {
    $sdkDirs = Get-ChildItem -Path $sdkIncludeDir -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($sdkDirs) {
        $windowsSdkVersion = $sdkDirs.Name
    }
}
Write-Host "[*] Windows SDK: $windowsSdkVersion" -ForegroundColor Cyan

# ==================== 查找 VST3 SDK 路径 ====================
$vst3SdkPath = ""
foreach ($modDir in $juceModulesAbsolute) {
    $candidate = Join-Path $modDir "juce_audio_processors/format_types/VST3_SDK"
    if (Test-Path $candidate) {
        $vst3SdkPath = $candidate.Replace("\", "/")
        break
    }
    # 也查找 headless 变体
    $candidate2 = Join-Path $modDir "juce_audio_processors_headless/format_types/VST3_SDK"
    if (Test-Path $candidate2) {
        $vst3SdkPath = $candidate2.Replace("\", "/")
        break
    }
}

# ==================== 读取模块列表生成宏定义 ====================
$modules = @()
foreach ($mod in $jucer.JUCERPROJECT.MODULES.ChildNodes) {
    if ($mod.id) {
        $modules += $mod.id
    }
}

$defines = @(
    "_CRT_SECURE_NO_WARNINGS",
    "WIN32",
    "_WINDOWS",
    "DEBUG",
    "_DEBUG",
    "JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1"
)

foreach ($mod in $modules) {
    $defines += "JUCE_MODULE_AVAILABLE_$mod=1"
}

# 读取 JUCEOPTIONS
$juceOptions = $jucer.JUCERPROJECT.JUCEOPTIONS
if ($juceOptions) {
    foreach ($attr in $juceOptions.Attributes) {
        $defines += "$($attr.Name)=$($attr.Value)"
    }
}

$defines += @(
    "JucePlugin_Build_VST=0",
    "JucePlugin_Build_VST3=1",
    "JucePlugin_Build_AU=0",
    "JucePlugin_Build_AUv3=0",
    "JucePlugin_Build_AAX=0",
    "JucePlugin_Build_Standalone=1",
    "JucePlugin_Build_Unity=0",
    "JucePlugin_Build_LV2=0",
    "JUCE_STANDALONE_APPLICATION=JucePlugin_Build_Standalone"
)

# ==================== 构建 VS 文件夹名 ====================
$vsFolderName = $targetFolder -replace "^Builds/", "" -replace "^Builds\\\\", ""

# ==================== 创建 .vscode 目录 ====================
$vscodeDir = Join-Path $ProjectDir ".vscode"
if (-not (Test-Path $vscodeDir)) {
    New-Item -ItemType Directory -Path $vscodeDir | Out-Null
}

# ==================== 生成 tasks.json ====================
$msbuildForward = $msbuildPath.Replace("\", "/")

$tasksJson = @"
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build: Standalone (Debug)",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/p:Configuration=Debug",
                "/p:Platform=x64",
                "/m",
                "/nologo",
                "/v:minimal"
            ],
            "group": "build",
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        },
        {
            "label": "Build: Standalone (Release)",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/p:Configuration=Release",
                "/p:Platform=x64",
                "/m",
                "/nologo",
                "/v:minimal"
            ],
            "group": "build",
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        },
        {
            "label": "Build: All (Debug)",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/p:Configuration=Debug",
                "/p:Platform=x64",
                "/m",
                "/nologo",
                "/v:minimal"
            ],
            "group": { "kind": "build", "isDefault": true },
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        },
        {
            "label": "Build: All (Release)",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/p:Configuration=Release",
                "/p:Platform=x64",
                "/m",
                "/nologo",
                "/v:minimal"
            ],
            "group": "build",
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        },
        {
            "label": "Rebuild: All (Debug)",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/t:Rebuild",
                "/p:Configuration=Debug",
                "/p:Platform=x64",
                "/m",
                "/nologo",
                "/v:minimal"
            ],
            "group": "build",
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        },
        {
            "label": "Clean: All",
            "type": "process",
            "command": "$msbuildForward",
            "args": [
                "`${workspaceFolder}/Builds/$vsFolderName/$projectName.sln",
                "/t:Clean",
                "/p:Configuration=Debug",
                "/p:Platform=x64",
                "/nologo",
                "/v:minimal"
            ],
            "group": "build",
            "problemMatcher": "`$msCompile",
            "presentation": { "reveal": "always", "panel": "shared", "clear": true }
        }
    ]
}
"@

$tasksPath = Join-Path $vscodeDir "tasks.json"
Set-Content -Path $tasksPath -Value $tasksJson -Encoding UTF8
Write-Host "[+] 已生成: $tasksPath" -ForegroundColor Green

# ==================== 生成 Directory.Build.props (UTF-8 编译) ====================
$buildDir = Join-Path $ProjectDir $targetFolder
$propsPath = Join-Path $buildDir "Directory.Build.props"
if (-not (Test-Path $propsPath)) {
    $propsContent = @"
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
"@
    Set-Content -Path $propsPath -Value $propsContent -Encoding UTF8
    Write-Host "[+] 已生成: $propsPath" -ForegroundColor Green
}
else {
    Write-Host "[=] 已存在: $propsPath (跳过)" -ForegroundColor Yellow
}

# ==================== 生成 launch.json ====================
$launchJson = @"
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Standalone Plugin",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "`${workspaceFolder}/Builds/$vsFolderName/x64/Debug/Standalone Plugin/$projectName.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "`${workspaceFolder}",
            "environment": [],
            "console": "integratedTerminal",
            "preLaunchTask": "Build: All (Debug)"
        },
        {
            "name": "Debug Standalone (no build)",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "`${workspaceFolder}/Builds/$vsFolderName/x64/Debug/Standalone Plugin/$projectName.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "`${workspaceFolder}",
            "environment": [],
            "console": "integratedTerminal"
        },
        {
            "name": "Attach to Process",
            "type": "cppvsdbg",
            "request": "attach",
            "processId": "`${command:pickProcess}"
        }
    ]
}
"@

$launchPath = Join-Path $vscodeDir "launch.json"
Set-Content -Path $launchPath -Value $launchJson -Encoding UTF8
Write-Host "[+] 已生成: $launchPath" -ForegroundColor Green

# ==================== 生成 c_cpp_properties.json ====================
$includePathEntries = @(
    '                "${workspaceFolder}/Source"',
    '                "${workspaceFolder}/JuceLibraryCode"'
)
foreach ($modPath in $juceModulesAbsolute) {
    $includePathEntries += "                `"$modPath`""
}
if ($vst3SdkPath) {
    $includePathEntries += "                `"$vst3SdkPath`""
}
$includePathStr = $includePathEntries -join ",`n"

$definesEntries = $defines | ForEach-Object { "                `"$_`"" }
$definesStr = $definesEntries -join ",`n"

$cppPropsJson = @"
{
    "configurations": [
        {
            "name": "JUCE Plugin (MSVC)",
            "intelliSenseMode": "windows-msvc-x64",
            "compilerPath": "$clPath",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "windowsSdkVersion": "$windowsSdkVersion",
            "includePath": [
$includePathStr
            ],
            "defines": [
$definesStr
            ]
        }
    ],
    "version": 4
}
"@

$cppPropsPath = Join-Path $vscodeDir "c_cpp_properties.json"
Set-Content -Path $cppPropsPath -Value $cppPropsJson -Encoding UTF8
Write-Host "[+] 已生成: $cppPropsPath" -ForegroundColor Green

# ==================== 生成 settings.json ====================
$settingsJson = @"
{
    "files.exclude": {
        "**/.vs": true,
        "**/x64": true,
        "Builds/$vsFolderName/x64": true,
        "Builds/$vsFolderName/.vs": true
    },
    "search.exclude": {
        "**/.vs": true,
        "**/x64": true,
        "Builds": true,
        "JuceLibraryCode": true
    },
    "files.associations": {
        "*.jucer": "xml"
    },
    "editor.formatOnSave": false,
    "[cpp]": {
        "editor.defaultFormatter": "ms-vscode.cpptools"
    },
    "[c]": {
        "editor.defaultFormatter": "ms-vscode.cpptools"
    },
    "C_Cpp.default.configurationProvider": "ms-vscode.cpptools",
    "C_Cpp.errorSquiggles": "enabled"
}
"@

$settingsPath = Join-Path $vscodeDir "settings.json"
Set-Content -Path $settingsPath -Value $settingsJson -Encoding UTF8
Write-Host "[+] 已生成: $settingsPath" -ForegroundColor Green

# ==================== 完成 ====================
Write-Host ""
Write-Host "===== VS Code 配置生成完毕 =====" -ForegroundColor Green
Write-Host "项目: $projectName"
Write-Host "构建: Ctrl+Shift+B"
Write-Host "调试: F5 (Standalone)"
Write-Host "前置: 安装 C/C++ 扩展 (ms-vscode.cpptools)"
Write-Host ""
