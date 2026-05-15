# obs-qsv-onevpl-boost

## Intel QSV encoder plugin for OBS Studio based on libVPL library

---

本项目复刻（Fork）自 [ma3uk/obs-qsv-onevpl](https://github.com/ma3uk/obs-qsv-onevpl)。

### 关于本项目

本项目的初衷是**个人使用**，在原始版本的基础上进行修改，而且修改更偏向于UHD 700因为我的显卡就只有这个，主要方向是：

- **更丰富的编码设置**：提供更多有用的编码参数和调节选项
- **更高的稳定性**：尽量减少录制和推流过程中遇到的错误
- 当然，任何人都可以自由使用、克隆和修改本仓库

### 主要修改

- 修复语言文件（locale）加载问题，并且添加中文翻译
- 添加更多编码参数可见项
- 工作流支持 Release / Debug 双版本编译
- 其他 bug 修复和优化

### obq-qsvonevpl 是什么

obq-qsvonevpl 是 OBS Studio（30 及以上版本）的一个插件。该插件基于 libVPL 库，为 Intel 显卡（UHD 600*试验中*、UHD 700、Arc Alchemist、Arc Battlemage）实现视频编码器功能，适用于网络直播和本地视频录制。与 OBS Studio 内置的标准插件相比，此插件提供了更高级的编码器设置，以获得更高的视频质量。

### 原始项目

原始项目地址：https://github.com/ma3uk/obs-qsv-onevpl

### 下载

请前往本仓库的 Actions 页面下载最新构建：https://github.com/HEIHUAa/obs-qsv-onevpl-boost/actions

### 安装方法

将下载下来的zip当中的data，obs-plugins文件夹解压到OBS Studio主目录下，也就是看得见bin，data，obs-plugins这三个文件的文件夹下

---

## English

### About

This project is a fork of [ma3uk/obs-qsv-onevpl](https://github.com/ma3uk/obs-qsv-onevpl).

It was created primarily for **personal use**, with modifications focused on UHD 700 (since that is the hardware I have). The main goals are:

- **More useful encoding settings**: Additional encoding parameters and adjustment options
- **Better stability**: Reducing errors during recording and streaming
- Everyone is welcome to use, clone, and modify this repository

### Key Changes

- Fixed locale loading issue and added Chinese translations
- Added more visible encoding parameters
- CI workflow now builds both Release and Debug configurations
- Other bug fixes and optimizations

### What is obs-qsvonevpl

obs-qsvonevpl is an OBS Studio (30 and above) plugin that implements a video encoder using the libVPL library for Intel graphics adapters (UHD 700, Arc Alchemist, Arc Battlemage). It is designed for live streaming and local video recording. Compared to the standard encoder built into OBS Studio, this plugin offers advanced encoder settings for higher video quality.

### Original Project

Original repository: https://github.com/ma3uk/obs-qsv-onevpl

### Download

Get the latest builds from the Actions page: https://github.com/HEIHUAa/obs-qsv-onevpl-boost/actions

### Installation

Extract the `data` and `obs-plugins` folders from the downloaded zip file into the OBS Studio main directory, where you can see the `bin`, `data`, and `obs-plugins` folders.
