# obs-qsv-onevpl-boost

## Intel QSV encoder plugin for OBS Studio based on libVPL library

***

## 中文

本项目复刻（Fork）自 [ma3uk/obs-qsv-onevpl](https://github.com/ma3uk/obs-qsv-onevpl)。

### 关于本项目

本项目的初衷可能更偏向于UHD 700系列和UHD600系列因为我的Intel显卡就只有UHD730，还有朋友的UHD620，主要方向是：

- **更丰富的编码设置**：提供更多有用的编码参数和调节选项
- 更多有效的功能：让某些选项真正生效
- **更高的稳定性**：尽量减少录制和推流过程中遇到的错误
- 当然，任何人都可以自由使用、克隆和修改本仓库

### 主要修改

- 修复语言文件（locale）加载问题，并且添加中文翻译
- 添加更多编码参数可见项
- 更少的bug
- 更好的录制启动速度和录制运行占用

### obq-qsvonevpl-boost 是什么

obq-qsvonevpl-boost 是 obq-qsvonevpl 增强版本 —— OBS Studio（30 及以上版本）的一个插件。该插件基于 libVPL 库，为 Intel 显卡（UHD 600*试验中*、UHD 700、Arc Alchemist、Arc Battlemage）实现视频编码器功能，适用于网络直播和本地视频录制。与 OBS Studio 内置的标准插件相比，此插件提供了更高级的编码器设置，以获得更高的视频质量。

### 原始项目

原始项目地址：<https://github.com/ma3uk/obs-qsv-onevpl>

### 下载

前往本仓库的 Releases 最新稳定发布版：<https://github.com/HEIHUAa/obs-qsv-onevpl-boost/releases>
或者前往本仓库的 Actions 页面下载最新构建：<https://github.com/HEIHUAa/obs-qsv-onevpl-boost/actions>

### 安装方法

将下载下来的zip当中的`data`，`obs-plugins`文件夹解压到OBS Studio主目录下，也就是看得见`bin`，`data`，`obs-plugins`这三个文件的文件夹下

### 本地编译（Windows）

无需额外安装软件（需要机器上已装有 Visual Studio Build Tools 2026 与 Windows SDK 10.0.26100，CMake 会从 VS 自带目录自动定位）。

1. **准备环境**（仅首次，约下载 2–3GB，全部存放在项目内 `build-env\` 文件夹）：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Setup-Local-Build.ps1
   ```

   脚本会克隆 OBS Studio 32.2.0、oneVPL v2.14.0、MediaSDK 头文件和 FFmpeg 头文件（release/8.1 分支，与 OBS 预编译依赖版本一致）到 `build-env\`，并用目录联接（junction）把本仓库接入 OBS 源码树，构建方式与 `.github/workflows/build-uhd700.yml` 一致。

2. **编译并打包**：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1          # 标准版 (UHD700+)
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1 -UHD600  # 同时构建 UHD600 变体
   ```

3. **产物**位于 `release\uhd700\`（或 `release\uhd600\`），目录结构与插件包一致，把 `obs-plugins`、`data` 拷入 OBS 主目录即可使用。

> 首次编译时 CMake 会自动下载 OBS 预编译依赖（obs-deps / Qt6）到 `build-env\obs-studio\.deps\`。日常修改代码后只需重复第 2 步；脚本可安全重复执行。

### 多显卡用户注意事项

如果你有多张显卡（例如核显+独显），**强烈建议将 OBS 运行在你需要截取画面的那张显卡上**。例如，你要录制独显上的游戏画面，就将 OBS 的运行显卡也设置为独显。

否则会导致显卡 3D 占用大幅增加。

**设置方法**：Windows 设置 → 系统 → 屏幕 → 显示卡 → 找到 `obs64.exe` → 选项 → 选择你要截取画面的显卡 → 保存。

***

## English

This project is a fork of [ma3uk/obs-qsv-onevpl](https://github.com/ma3uk/obs-qsv-onevpl).

### About This Project

The original intent of this project is more focused on the UHD 700 series and UHD 600 series, since my own Intel GPU is only UHD 730, and a friend's UHD 620. The main directions are:

- **Richer encoding settings**: Provide more useful encoding parameters and adjustment options.
- **More effective functionality**: Make certain options actually take effect.
- **Higher stability**: Minimize errors encountered during recording and streaming.
- Of course, anyone is free to use, clone, and modify this repository.

### Key Changes

- Fixed locale loading issues and added Chinese translations.
- Added more visible encoding parameters.
- Fewer bugs.
- Better recording startup speed and lower runtime resource usage.

### What is obq-qsvonevpl-boost

obq-qsvonevpl-boost is an enhanced version of obq-qsvonevpl – a plugin for OBS Studio (version 30 and above). This plugin implements a video encoder using the libVPL library for Intel graphics adapters (UHD 600 *experimental*, UHD 700, Arc Alchemist, Arc Battlemage), designed for live streaming and local video recording. Compared to the standard encoder built into OBS Studio, this plugin offers advanced encoder settings for higher video quality.

### Original Project

Original repository: <https://github.com/ma3uk/obs-qsv-onevpl>

### Download

Go to the Releases page of this repository for the latest stable release: <https://github.com/HEIHUAa/obs-qsv-onevpl-boost/releases>
Or go to the Actions page of this repository to download the latest build: <https://github.com/HEIHUAa/obs-qsv-onevpl-boost/actions>

### Installation

Extract the `data` and `obs-plugins` folders from the downloaded zip file into the OBS Studio main directory, i.e., the folder where you can see the `bin`, `data`, and `obs-plugins` folders.

### Local Build (Windows)

No extra software installation is required (Visual Studio Build Tools 2026 and Windows SDK 10.0.26100 must be present; CMake is located automatically from the VS installation).

1. **Set up the environment** (first time only, downloads ~2–3 GB into the in-repo `build-env\` folder):

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Setup-Local-Build.ps1
   ```

   The script clones OBS Studio 32.2.0, oneVPL v2.14.0, the MediaSDK headers and the FFmpeg headers (release/8.1 branch, matching the version shipped in OBS's prebuilt deps) into `build-env\`, then links this repo into the OBS source tree with NTFS junctions, mirroring `.github/workflows/build-uhd700.yml`.

2. **Build and package**:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1          # standard (UHD700+)
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1 -UHD600  # also build the UHD600 variant
   ```

3. **Artifacts** land in `release\uhd700\` (or `release\uhd600\`) with the same layout as the plugin zip; copy `obs-plugins` and `data` into your OBS installation directory.

> The first build automatically downloads the OBS prebuilt dependencies (obs-deps / Qt6) into `build-env\obs-studio\.deps\`. After editing the code, just repeat step 2. Both scripts are safe to re-run.

### Multi-GPU Users Notice

If you have multiple GPUs (e.g., integrated + discrete), **it is strongly recommended to run OBS on the same GPU as the content you are capturing**. For example, if you want to record a game running on your discrete GPU, set OBS to run on the discrete GPU as well.

Otherwise it will cause significantly higher GPU 3D usage.

**How to configure**: Windows Settings → System → Display → Graphics → Find `obs64.exe` → Options → Select the GPU where your captured content runs → Save.