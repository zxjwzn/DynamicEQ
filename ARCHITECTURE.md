# DynamicEQ 项目架构文档

> 供 LLM 阅读，读完后可直接对本项目进行理解和修改。

---

## 1. 项目概述

DynamicEQ 是一个基于 **JUCE 框架** 的 4 频段动态均衡器 (Dynamic EQ) 音频插件。
支持输出为 **VST3** 和 **Standalone** 两种格式。
视觉风格参考 FabFilter Pro-Q 3 / Xfer Pro-C 2，目标保持为：实时频谱 + 交互式拖拽节点 + 动态压缩反馈。

### 核心功能

- 4 频段参数化 EQ（Low Shelf / Peak / High Shelf）
- 每个频段独立的动态压缩（Threshold / Ratio / Attack / Release）
- 实时 FFT 频谱分析（处理前 / 处理后）
- 可拖拽的 EQ 节点（频率 = X 轴, 增益 = Y 轴, Q = 滚轮）
- 压缩激活时节点视觉下沉 + 增益衰减指示线

---

## 2. 技术栈

| 项 | 值 |
|---|---|
| 框架 | JUCE（模块路径 `../../App/modules`，即 `D:\C++repositories\JUCE\App\modules`） |
| C++ 标准 | C++17 |
| 构建系统 | Visual Studio 2026 / MSBuild (PlatformToolset v145) |
| 项目配置 | Projucer (`.jucer` 文件) → 生成 VS `.sln` |
| 平台 | Windows x64 (Debug / Release) |
| 构建命令 | `MSBuild.exe DynamicEQ.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal` |

### 构建路径

```
Builds/VisualStudio2026/DynamicEQ.sln           ← 解决方案
Builds/VisualStudio2026/x64/Debug/              ← 输出目录
  Standalone Plugin/DynamicEQ.exe               ← 独立运行版
  VST3/DynamicEQ.vst3/                          ← VST3 插件
```

---

## 3. 文件结构与职责

```
DynamicEQ/
├── DynamicEQ.jucer                 ← Projucer 工程文件（定义模块、源文件、导出器）
├── ARCHITECTURE.md                 ← 本文档
├── Source/
│   ├── PluginProcessor.h / .cpp    ← [DSP 核心] 音频处理器 + 参数系统
│   ├── PluginEditor.h / .cpp       ← [GUI 核心] 编辑器窗口 + 控件条
│   ├── DSP/
│   │   ├── SpectrumAnalyzer.h      ← FFT 频谱分析器（仅头文件）
│   │   └── DynamicEQBand.h         ← 单频段滤波器 + 动态压缩（仅头文件）
│   └── UI/
│       └── SpectrumComponent.h     ← 频谱显示 + EQ 曲线 + 交互节点（仅头文件）
├── JuceLibraryCode/                ← Projucer 自动生成，不要手动修改
│   ├── JuceHeader.h
│   └── JucePluginDefines.h
└── Builds/
    └── VisualStudio2026/           ← VS 工程文件（Projucer 生成 + 手动维护）
        ├── DynamicEQ.sln
        ├── DynamicEQ_SharedCode.vcxproj
        ├── DynamicEQ_SharedCode.vcxproj.filters
        └── ...
```

---

## 4. 核心模块详解

### 4.1 PluginProcessor（音频处理器）

**文件**: `Source/PluginProcessor.h` + `Source/PluginProcessor.cpp`

**类**: `DynamicEQAudioProcessor : public juce::AudioProcessor`

**核心职责**:
- 定义所有参数（`createParameterLayout()` 静态方法，返回 `ParameterLayout`）
- 拥有 4 个 `DynamicEQBand` 实例和 2 个 `SpectrumAnalyzer`（pre/post）
- 在 `processBlock()` 中：推送 pre-EQ 频谱 → 更新参数 → 逐频段处理 → 推送 post-EQ 频谱
- 通过 `getAPVTS()` 给 GUI 层暴露参数树

**参数命名约定**:
每个频段的参数 ID 格式为 `band{i}_{param}`，其中 `i` 为 0-3，可选参数名列表：

| 参数名 | 类型 | 范围 | 默认值 |
|--------|------|------|--------|
| `freq` | Float | 20–20000 Hz (skew 0.25) | 100 / 500 / 2000 / 8000 |
| `gain` | Float | -24–+24 dB | 0 |
| `q` | Float | 0.1–10 (skew 0.5) | 1.0 |
| `threshold` | Float | -60–0 dB | -20 |
| `ratio` | Float | 1–20 (skew 0.5) | 4.0 |
| `attack` | Float | 0.1–200 ms (skew 0.4) | 10 |
| `release` | Float | 1–1000 ms (skew 0.4) | 100 |
| `enabled` | Bool | — | true |
| `dynamic` | Bool | — | true |
| `type` | Choice | "Low Shelf" / "Peak" / "High Shelf" | Band0=LowShelf, Band3=HighShelf, 中间=Peak |

> **注意**: `type` 参数在 `ParameterLayout` 中使用英文 `StringArray { "Low Shelf", "Peak", "High Shelf" }`，
> 但 UI 的 `ComboBox` 显示中文（通过 `juce::String::fromUTF8()`），两者通过 `ComboBoxAttachment` 索引绑定。

### 4.2 DynamicEQBand（单频段 DSP）

**文件**: `Source/DSP/DynamicEQBand.h`（纯头文件实现）

**类**: `DynamicEQBand`

**核心逻辑**:
1. 使用 `juce::dsp::ProcessorDuplicator<IIR::Filter, IIR::Coefficients>` 进行立体声 IIR 滤波
2. 静态模式（`dynamicOn = false`）：直接以 `gain` 应用滤波器
3. 动态模式（`dynamicOn = true`）：
   - 检测每 block 的峰值电平
   - 通过 `EnvelopeFollower` 平滑
   - 计算增益衰减 `reductionDB = excess - excess / ratio`
   - 以 `gain - reductionDB` 重新构造滤波系数并处理
4. 增益衰减量通过 `std::atomic<float> gainReductionDB` 暴露给 GUI 线程（无锁读取）

**辅助类**:
- `BandParams` — 频段参数结构体
- `EnvelopeFollower` — 攻击/释放包络跟随器

### 4.3 SpectrumAnalyzer（频谱分析）

**文件**: `Source/DSP/SpectrumAnalyzer.h`（纯头文件实现）

**类**: `SpectrumAnalyzer`

**核心逻辑**:
- FFT 阶数 = 12（4096 点），使用 Hann 窗
- `pushSamples()` 在音频线程调用，将采样写入环形 FIFO
- `processFFT()` 在 GUI 线程调用，执行 `performFrequencyOnlyForwardTransform` 并输出 0–1 归一化幅度数组
- 通过 `juce::Atomic<bool> newFFTDataAvailable` 实现线程间无锁同步

**辅助模板**: `AudioFifo<FifoSize>` — 基于 `juce::AbstractFifo` 的无锁音频缓冲区

### 4.4 SpectrumComponent（频谱 UI）

**文件**: `Source/UI/SpectrumComponent.h`（纯头文件实现）

**类**: `SpectrumComponent : public juce::Component, public juce::Timer`

**核心职责**:
- 60fps 定时器驱动重绘（`startTimerHz(60)`）
- 每帧：拉取 FFT 数据 → 平滑频谱 → 检查参数变化 → 按需重建曲线缓存 → `repaint()`
- 绘制层次（从底到顶）：背景 → 网格 → pre 频谱 → post 频谱 → EQ 总曲线 → 各频段曲线 → 节点

**曲线缓存机制**（性能关键）:
- 使用 `BandSnapshot` 记录上一帧各频段的关键参数值
- 仅当参数实际变化时，调用 `rebuildCurveCache()` 重新计算
- 采样 256 个频率点（对数分布），使用 `coeffs->getMagnitudeForFrequencyArray()` 批量计算幅度
- 缓存存储在 `cachedBandMagnitudes[band][point]` 和 `cachedTotalMagnitude[point]`

**交互功能**:
- 鼠标点击/拖拽：移动 EQ 节点（x → 频率, y → 增益）
- 鼠标滚轮：调整 Q 值
- 节点命中测试使用视觉位置（`gain - gainReduction`），拖拽时补偿增益衰减偏移

**坐标映射工具函数**（文件顶部的 `inline` 函数）:
- `freqToX(freq, width)` / `xToFreq(x, width)` — 对数频率 ↔ X 像素
- `dbToY(db, height)` / `yToDb(y, height)` — 分贝 ↔ Y 像素
- `getBandColour(bandIndex)` — 返回各频段颜色：红/黄/绿/蓝

### 4.5 PluginEditor（编辑器窗口）

**文件**: `Source/PluginEditor.h` + `Source/PluginEditor.cpp`

**类**:
- `DarkLookAndFeel : public juce::LookAndFeel_V4` — 暗色主题（定义在 .cpp 文件中，通过 `static` 单例访问）
- `BandControlStrip : public juce::Component` — 单频段控件条
- `DynamicEQAudioProcessorEditor : public juce::AudioProcessorEditor` — 主编辑器

**BandControlStrip 布局**:
```
┌──────────────────────────┐
│      频段 N (标题)         │ ← 20px，颜色条 3px
├──────────────────────────┤
│ [✓启用]  [峰值 ▼]  [✓动态] │ ← 26px 顶部行
├──────────────────────────┤
│  频率     增益     Q值     │ ← 16px 标签
│  [旋钮]  [旋钮]  [旋钮]    │ ← 58px 旋钮
├──────────────────────────┤
│    阈值       比率         │
│   [旋钮]     [旋钮]        │
├──────────────────────────┤
│    起音       释放         │
│   [旋钮]     [旋钮]        │
└──────────────────────────┘
```

**主编辑器布局**:
```
┌─────────────────────────────────────┐
│           "Dynamic EQ" (标题 36px)   │
├─────────────────────────────────────┤
│                                     │
│       SpectrumComponent             │ ← 填充剩余空间
│   (频谱 + EQ 曲线 + 节点)           │
│                                     │
├─────────────────────────────────────┤
│ Band1 │ Band2 │ Band3 │ Band4      │ ← 276px 控件区
│ Strip │ Strip │ Strip │ Strip      │
└─────────────────────────────────────┘
默认: 960×660, 可缩放: 800×550 ~ 1600×1000
```

---

## 5. 线程模型

```
音频线程 (Audio Thread)                    GUI 线程 (Message Thread)
─────────────────────                     ──────────────────────
processBlock()                            timerCallback() @ 60fps
  ├─ pushSamples() → pre spectrum          ├─ processFFT() ← 读取 FFT 数据
  ├─ updateBandParams() ← 读 APVTS        ├─ 平滑频谱
  ├─ band[i].process(buffer)               ├─ checkAndUpdateCurve()
  │   └─ gainReductionDB.store()           │   └─ rebuildCurveCache() 按需
  └─ pushSamples() → post spectrum         ├─ repaint()
                                           └─ 鼠标事件 → setValueNotifyingHost()
```

**线程安全机制**:
- APVTS 参数：音频线程通过 `getRawParameterValue()->load()` 原子读取
- 增益衰减：`std::atomic<float>` 写入（音频线程），无锁读取（GUI 线程）
- FFT 数据：`juce::Atomic<bool>` 标记新数据可用，GUI 线程 CAS 读取

---

## 6. JUCE 字符串使用注意事项

**关键约束**: JUCE 的 `juce::String(const char*)` 构造函数内部会 `jassert` 校验输入是否为纯 ASCII。
中文等非 ASCII 字符的 UTF-8 字节序列**无法**通过此校验，在 Debug 构建中会触发断言失败（程序崩溃）。

**正确做法**: 所有包含非 ASCII 字符的字符串必须使用 `juce::String::fromUTF8("中文文本")` 创建。

**错误示例**:
```cpp
// ❌ 断言失败：传入了非 ASCII 的 UTF-8 字节
juce::ToggleButton enableBtn { "启用" };
typeCombo.addItem ("低架", 1);
```

**正确示例**:
```cpp
// ✅ 通过 fromUTF8 安全创建
enableBtn.setButtonText (juce::String::fromUTF8 ("启用"));
typeCombo.addItem (juce::String::fromUTF8 ("低架"), 1);
```

> 这是本项目中已踩过的坑，后续添加中文文本时务必遵守。

---

## 7. 构建与验证流程

### 7.1 构建命令

```powershell
# 停止正在运行的实例（避免 LNK1168 文件锁定错误）
Stop-Process -Name "DynamicEQ" -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# 构建
& "D:/Application/VisualStudio/MSBuild/Current/Bin/amd64/MSBuild.exe" `
  "D:\C++repositories\JUCE\VSTPlugins\DynamicEQ\Builds\VisualStudio2026\DynamicEQ.sln" `
  /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
```

### 7.2 验证清单

每次代码修改后应执行以下验证：

1. **编译通过** — 运行 MSBuild，确保 exit code 0 且无 error
2. **无 warning** — 注意编译器警告（如变量遮蔽 `/W4`），应尽量消除
3. **运行不崩溃** — 特别注意：
   - `jassert` 断言（Debug 构建会中断）— 常见于非 ASCII 字符串
   - 空指针解引用（关注 `Coefficients::Ptr` 是否为 nullptr）
   - 除零错误（`sampleRate <= 0`、`width <= 0` 的保护）

### 7.3 添加新文件的步骤

如果需要添加新的源文件，必须同步修改以下三处：

1. **`DynamicEQ.jucer`** — 在 `<MAINGROUP>` 对应 `<GROUP>` 中添加 `<FILE>` 条目
2. **`DynamicEQ_SharedCode.vcxproj`** — 在 `<ItemGroup>` 中添加 `<ClCompile>` 或 `<ClInclude>`
3. **`DynamicEQ_SharedCode.vcxproj.filters`** — 在对应 `<Filter>` 中添加条目

> 否则 Projucer 或 Visual Studio 将找不到新文件。

---

## 8. 已知架构决策与约束

### 8.1 Header-Only DSP/UI

`DynamicEQBand.h`、`SpectrumAnalyzer.h`、`SpectrumComponent.h` 均为**纯头文件实现**。
这简化了构建配置（不需要额外添加 `.cpp` 到编译列表），但意味着：
- 修改这些文件会导致所有 include 它们的 `.cpp` 重新编译
- 类的实现细节全部暴露在头文件中

### 8.2 DarkLookAndFeel 单例

`DarkLookAndFeel` 类定义在 `PluginEditor.cpp` 中，通过：
```cpp
static DarkLookAndFeel& getDarkLookAndFeel()
{
    static DarkLookAndFeel lnf;
    return lnf;
}
```
作为文件级静态单例使用。`SpectrumComponent` 不需要设置 LookAndFeel（它完全自绘）。

### 8.3 getBandColour 全局函数

`getBandColour(int bandIndex)` 定义在 `SpectrumComponent.h` 中作为 `inline` 全局函数。
`BandControlStrip`（在 `PluginEditor.cpp` 中）也通过 include 链使用此函数。

### 8.4 参数内部 ID 与 UI 显示分离

APVTS 参数使用英文 ID（`band0_freq`）和英文显示名（`"Band 1 Freq"`），
而 GUI 控件的标签使用中文（`"频率"`）。两者通过 APVTS Attachment 机制绑定，互不干扰。

### 8.5 曲线缓存性能优化

`SpectrumComponent` 不会每帧重新计算 EQ 曲线。而是：
1. 每帧对比当前参数快照与上一帧的 `BandSnapshot`
2. 仅在参数变化（包括增益衰减变化 > 0.05 dB）时调用 `rebuildCurveCache()`
3. 缓存 256 个频率采样点的各频段幅度响应

---

## 9. 常见修改指南

### 9.1 增加新的 EQ 频段

1. 修改 `PluginProcessor.h` 中的 `static constexpr int numBands`
2. `createParameterLayout()` 会自动根据 `numBands` 生成参数
3. 在 `PluginEditor.cpp` 的 `DynamicEQAudioProcessorEditor` 构造函数中，band strip 循环也会自动适配
4. `SpectrumComponent.h` 中的 `cachedBandMagnitudes` 和 `lastSnapshots` 数组大小由 `numBands` 决定
5. 需要在 `getBandColour()` 中添加更多颜色

### 9.2 添加新的参数类型

1. 在 `BandParams` 结构体中添加字段
2. 在 `createParameterLayout()` 中添加参数定义
3. 在 `updateBandParams()` 中读取参数并传递给 `DynamicEQBand`
4. 在 `BandControlStrip` 中添加对应的控件 + Attachment
5. 如果影响滤波器行为，修改 `DynamicEQBand::updateFilterCoefficients()`

### 9.3 修改 UI 布局

- **控件区高度**: `PluginEditor.cpp` → `resized()` → `bounds.removeFromBottom(276)`
- **旋钮大小**: `BandControlStrip::resized()` → `knobSize = 58`
- **文本框大小**: `setupSlider()` → `setTextBoxStyle(..., 74, 16)`
- **窗口大小**: `DynamicEQAudioProcessorEditor` 构造函数 → `setSize(960, 660)` + `setResizeLimits(...)`

### 9.4 修改频谱显示

- **FFT 精度**: `SpectrumAnalyzer.h` → `fftOrder`（当前 12 = 4096 点）
- **平滑系数**: `SpectrumComponent.h` → `timerCallback()` → `smoothing = 0.75f`
- **刷新率**: `SpectrumComponent` 构造函数 → `startTimerHz(60)`
- **曲线采样数**: `curveNumPoints = 256`

---

## 10. JUCE 模块依赖

本项目使用以下 JUCE 模块（在 `.jucer` 文件中声明）：

| 模块 | 用途 |
|---|---|
| `juce_core` | 基础类型、字符串、线程 |
| `juce_audio_basics` | AudioBuffer、MIDI |
| `juce_audio_devices` | 音频设备 I/O |
| `juce_audio_formats` | 音频文件读写 |
| `juce_audio_processors` | AudioProcessor 基类、APVTS |
| `juce_audio_plugin_client` | VST3 / Standalone 插件入口 |
| `juce_audio_utils` | 音频工具组件 |
| `juce_dsp` | **核心**: FFT、IIR 滤波器、窗函数 |
| `juce_data_structures` | ValueTree |
| `juce_events` | Timer、消息循环 |
| `juce_graphics` | 2D 绘图、Path、ColourGradient |
| `juce_gui_basics` | Component、LookAndFeel、Slider |
| `juce_gui_extra` | 额外 GUI 组件 |
| `juce_opengl` | OpenGL 渲染（已引入但未主动使用） |

模块路径: `D:\C++repositories\JUCE\App\modules`（相对路径 `../../App/modules`）
