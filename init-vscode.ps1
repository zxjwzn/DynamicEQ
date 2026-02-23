<#
.SYNOPSIS
    为 JUCE Projucer 项目自动生成 VS Code 开发配置 + CI/CD 构建文件

.DESCRIPTION
    读取 .jucer 文件，自动检测项目名、JUCE 模块路径、VS 版本等信息，生成：
    - .vscode/ (tasks.json / launch.json / c_cpp_properties.json / settings.json)
    - CMakeLists.txt（用于 GitHub Actions CI 构建，基于 FetchContent）
    - .github/workflows/release.yml（自动构建、打包、发布 Release）
    - .gitignore
    支持所有标准 Projucer 导出的 Visual Studio 项目。

.PARAMETER ProjectDir
    JUCE 项目根目录（包含 .jucer 文件的目录）。默认为当前目录。

.PARAMETER JuceVersion
    CI 构建使用的 JUCE 版本 (git tag)。默认为 "8.0.4"。

.EXAMPLE
    .\init-vscode.ps1
    .\init-vscode.ps1 -ProjectDir "D:\MyOtherPlugin"
    .\init-vscode.ps1 -JuceVersion "7.0.12"
#>

param(
    [string]$ProjectDir = (Get-Location).Path,
    [string]$JuceVersion = "8.0.4"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ==================== 递归收集 .jucer 源文件 ====================
function Get-JucerSourceFiles {
    param([System.Xml.XmlNode]$Node)
    $result = @()
    foreach ($child in $Node.ChildNodes) {
        if ($child.LocalName -eq "FILE" -and $child.file) {
            $result += $child.file
        }
        elseif ($child.LocalName -eq "GROUP") {
            $result += Get-JucerSourceFiles -Node $child
        }
    }
    return $result
}

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

# ==================== 收集源文件 ====================
$sourceFiles = Get-JucerSourceFiles -Node $jucer.JUCERPROJECT.MAINGROUP
Write-Host "[*] 源文件数量: $($sourceFiles.Count)" -ForegroundColor Cyan

# ==================== 读取插件元数据 ====================
# 使用 GetAttribute() 安全访问可能不存在的 XML 属性（兼容 StrictMode）
$jucerRoot = $jucer.JUCERPROJECT

$pluginManufacturer     = $jucerRoot.GetAttribute("pluginManufacturer")
if (-not $pluginManufacturer) { $pluginManufacturer = $jucerRoot.GetAttribute("companyName") }
if (-not $pluginManufacturer) { $pluginManufacturer = "yourcompany" }

$pluginManufacturerCode = $jucerRoot.GetAttribute("pluginManufacturerCode")
if (-not $pluginManufacturerCode) { $pluginManufacturerCode = "Manu" }

$pluginCode             = $jucerRoot.GetAttribute("pluginCode")
if (-not $pluginCode) { $pluginCode = "Plgn" }

$projectVersion         = $jucerRoot.GetAttribute("version")
if (-not $projectVersion) { $projectVersion = "1.0.0" }

# 检测插件格式
$pluginFormats = @()
if ($defines -contains "JucePlugin_Build_VST3=1") { $pluginFormats += "VST3" }
if ($defines -contains "JucePlugin_Build_Standalone=1") { $pluginFormats += "Standalone" }
if ($defines -contains "JucePlugin_Build_AU=1") { $pluginFormats += "AU" }
if ($defines -contains "JucePlugin_Build_LV2=1") { $pluginFormats += "LV2" }
if ($pluginFormats.Count -eq 0) { $pluginFormats = @("VST3", "Standalone") }
$pluginFormatsStr = $pluginFormats -join " "

Write-Host "[*] 插件格式: $pluginFormatsStr" -ForegroundColor Cyan

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

# ==================== 生成 CMakeLists.txt (用于 CI) ====================
$cmakeListsPath = Join-Path $ProjectDir "CMakeLists.txt"

# 标准 JUCE 模块（CMake FetchContent 可直接使用，排除 _plugin_client）
$standardJuceModules = @(
    "juce_audio_basics", "juce_audio_devices", "juce_audio_formats",
    "juce_audio_processors", "juce_audio_utils", "juce_core",
    "juce_data_structures", "juce_dsp", "juce_events", "juce_graphics",
    "juce_gui_basics", "juce_gui_extra", "juce_opengl", "juce_osc",
    "juce_product_unlocking", "juce_video"
)
$cmakeModules = $modules | Where-Object { $_ -in $standardJuceModules }
$cmakeModulesStr = ($cmakeModules | ForEach-Object { "        juce::$_" }) -join "`n"
$cmakeSourcesStr = ($sourceFiles | ForEach-Object { "        $_" }) -join "`n"

# JUCE 选项 → CMake compile definitions
$cmakeDefinesList = @()
if ($juceOptions) {
    foreach ($attr in $juceOptions.Attributes) {
        $cmakeDefinesList += "        $($attr.Name)=$($attr.Value)"
    }
}
$cmakeDefinesList += "        JUCE_WEB_BROWSER=0"
$cmakeDefinesList += "        JUCE_USE_CURL=0"
$cmakeDefinesStr = $cmakeDefinesList -join "`n"

$cmakeTemplate = @'
# ==============================================================================
#  __PROJECT_NAME__ -- CMake build (CI / cross-platform)
#
#  Auto-generated by init-vscode.ps1. For GitHub Actions CI.
#  Local development uses Projucer + Visual Studio.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(__PROJECT_NAME__
    VERSION     __PROJECT_VERSION__
    LANGUAGES   CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# -- Fetch JUCE ----------------------------------------------------------------
include(FetchContent)

set(JUCE_GIT_TAG "__JUCE_VERSION__" CACHE STRING "JUCE version (git tag)")

FetchContent_Declare(
    juce
    GIT_REPOSITORY  https://github.com/juce-framework/JUCE.git
    GIT_TAG         ${JUCE_GIT_TAG}
    GIT_SHALLOW     ON
)
FetchContent_MakeAvailable(juce)

# -- Plugin Target -------------------------------------------------------------
juce_add_plugin(__PROJECT_NAME__
    COMPANY_NAME                "__MANUFACTURER__"
    PLUGIN_MANUFACTURER_CODE    __MANUFACTURER_CODE__
    PLUGIN_CODE                 __PLUGIN_CODE__
    FORMATS                     __FORMATS__
    PRODUCT_NAME                "__PROJECT_NAME__"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             "Fx"
)

juce_generate_juce_header(__PROJECT_NAME__)

# -- Sources -------------------------------------------------------------------
target_sources(__PROJECT_NAME__
    PRIVATE
__SOURCES__
)

target_include_directories(__PROJECT_NAME__
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# -- Compile Definitions -------------------------------------------------------
target_compile_definitions(__PROJECT_NAME__
    PUBLIC
__DEFINES__
)

if(MSVC)
    target_compile_options(__PROJECT_NAME__ PRIVATE /utf-8)
endif()

# -- Link JUCE Modules --------------------------------------------------------
target_link_libraries(__PROJECT_NAME__
    PRIVATE
__MODULES__
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
'@

$cmakeContent = $cmakeTemplate `
    -replace '__PROJECT_NAME__', $projectName `
    -replace '__PROJECT_VERSION__', $projectVersion `
    -replace '__JUCE_VERSION__', $JuceVersion `
    -replace '__MANUFACTURER__', $pluginManufacturer `
    -replace '__MANUFACTURER_CODE__', $pluginManufacturerCode `
    -replace '__PLUGIN_CODE__', $pluginCode `
    -replace '__FORMATS__', $pluginFormatsStr `
    -replace '__SOURCES__', $cmakeSourcesStr `
    -replace '__DEFINES__', $cmakeDefinesStr `
    -replace '__MODULES__', $cmakeModulesStr

Set-Content -Path $cmakeListsPath -Value $cmakeContent -Encoding UTF8
Write-Host "[+] 已生成: $cmakeListsPath" -ForegroundColor Green

# ==================== 生成 .github/workflows/release.yml ====================
$workflowDir = Join-Path $ProjectDir ".github" "workflows"
if (-not (Test-Path $workflowDir)) {
    New-Item -ItemType Directory -Path $workflowDir -Force | Out-Null
}
$workflowPath = Join-Path $workflowDir "release.yml"

$yamlTemplate = @'
# ==============================================================================
#  __PROJECT_NAME__ -- Build / Package / Release
#
#  Auto-generated by init-vscode.ps1
#  Tag push (v*) -> build + release / main push & PR -> CI only
# ==============================================================================

name: Build & Release

on:
  push:
    tags:
      - "v*"
    branches:
      - main
  pull_request:
    branches:
      - main
  workflow_dispatch:
    inputs:
      create_release:
        description: "Create a GitHub Release?"
        type: boolean
        default: false
      release_tag:
        description: "Release tag (e.g. v1.0.0). Required when creating a release."
        type: string
        required: false

permissions:
  contents: write

env:
  BUILD_TYPE: Release

jobs:
  build-windows:
    name: Windows x64
    runs-on: windows-latest

    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Cache JUCE
        uses: actions/cache@v4
        with:
          path: build/_deps
          key: juce-${{ runner.os }}-${{ hashFiles('CMakeLists.txt') }}
          restore-keys: |
            juce-${{ runner.os }}-

      - name: Configure CMake
        run: cmake -B build

      - name: Build
        run: cmake --build build --config ${{ env.BUILD_TYPE }} --parallel

      - name: Package Standalone
        shell: pwsh
        run: |
          $src = "build/__PROJECT_NAME___artefacts/${{ env.BUILD_TYPE }}/Standalone"
          if (-not (Test-Path $src)) {
            Write-Error "Standalone artefact directory not found: $src"
            exit 1
          }
          Compress-Archive -Path "$src/*" -DestinationPath "__PROJECT_NAME__-Standalone-Win64.zip" -Force

      - name: Package VST3
        shell: pwsh
        run: |
          $src = "build/__PROJECT_NAME___artefacts/${{ env.BUILD_TYPE }}/VST3"
          if (-not (Test-Path $src)) {
            Write-Error "VST3 artefact directory not found: $src"
            exit 1
          }
          Compress-Archive -Path "$src/*" -DestinationPath "__PROJECT_NAME__-VST3-Win64.zip" -Force

      - name: Upload artefacts
        uses: actions/upload-artifact@v4
        with:
          name: __PROJECT_NAME__-Win64
          path: |
            __PROJECT_NAME__-Standalone-Win64.zip
            __PROJECT_NAME__-VST3-Win64.zip
          retention-days: 30

      - name: Determine release tag
        id: tag
        if: |
          startsWith(github.ref, 'refs/tags/v') ||
          (github.event_name == 'workflow_dispatch' && inputs.create_release)
        shell: pwsh
        run: |
          if ("${{ github.ref }}".StartsWith("refs/tags/")) {
            $tag = "${{ github.ref_name }}"
          } else {
            $tag = "${{ inputs.release_tag }}"
          }
          if (-not $tag) {
            Write-Error "Release tag is empty. Please provide a tag."
            exit 1
          }
          echo "TAG=$tag" >> $env:GITHUB_OUTPUT

      - name: Create GitHub Release
        if: steps.tag.outputs.TAG
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ steps.tag.outputs.TAG }}
          name: __PROJECT_NAME__ ${{ steps.tag.outputs.TAG }}
          files: |
            __PROJECT_NAME__-Standalone-Win64.zip
            __PROJECT_NAME__-VST3-Win64.zip
          generate_release_notes: true
          draft: false
          prerelease: false
'@

$yamlContent = $yamlTemplate -replace '__PROJECT_NAME__', $projectName

Set-Content -Path $workflowPath -Value $yamlContent -Encoding UTF8
Write-Host "[+] 已生成: $workflowPath" -ForegroundColor Green

# ==================== 生成 .gitignore ====================
$gitignorePath = Join-Path $ProjectDir ".gitignore"
$existingGitignore = ""
if (Test-Path $gitignorePath) {
    $existingGitignore = (Get-Content -Path $gitignorePath -Raw -ErrorAction SilentlyContinue)
    if ($existingGitignore) { $existingGitignore = $existingGitignore.Trim() }
}

if (-not $existingGitignore) {
    $gitignoreTemplate = @'
# ==============================================================================
#  .gitignore (auto-generated by init-vscode.ps1)
# ==============================================================================

# -- CMake build (CI) ----------------------------------------------------------
build/
cmake-build-*/
CMakeUserPresets.json

# -- Visual Studio (local) ----------------------------------------------------
__VS_TARGET__/x64/
__VS_TARGET__/.vs/
*.user
*.suo
*.ncb
*.sdf
*.opensdf
*.opendb
*.db
*.ipch

# -- OS / IDE ------------------------------------------------------------------
.DS_Store
Thumbs.db
*.swp
*~
'@

    $gitignoreContent = $gitignoreTemplate -replace '__VS_TARGET__', $targetFolder

    Set-Content -Path $gitignorePath -Value $gitignoreContent -Encoding UTF8
    Write-Host "[+] 已生成: $gitignorePath" -ForegroundColor Green
}
else {
    Write-Host "[=] 已存在: $gitignorePath (跳过)" -ForegroundColor Yellow
}

# ==================== 完成 ====================
Write-Host ""
Write-Host "===== 项目配置生成完毕 =====" -ForegroundColor Green
Write-Host "项目: $projectName ($projectVersion)"
Write-Host "构建: Ctrl+Shift+B"
Write-Host "调试: F5 (Standalone)"
Write-Host "CI:   .github/workflows/release.yml"
Write-Host "CMake: CMakeLists.txt (JUCE $JuceVersion)"
Write-Host "前置: 安装 C/C++ 扩展 (ms-vscode.cpptools)"
Write-Host ""
