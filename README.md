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

### Multi-GPU Users Notice

If you have multiple GPUs (e.g., integrated + discrete), **it is strongly recommended to run OBS on the same GPU as the content you are capturing**. For example, if you want to record a game running on your discrete GPU, set OBS to run on the discrete GPU as well.

Otherwise it will cause significantly higher GPU 3D usage.

**How to configure**: Windows Settings → System → Display → Graphics → Find `obs64.exe` → Options → Select the GPU where your captured content runs → Save.