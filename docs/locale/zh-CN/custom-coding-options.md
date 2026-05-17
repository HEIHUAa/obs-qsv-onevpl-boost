# 自定义编码选项

本插件在编码器设置面板中提供了一个文本输入框 **"自定义编码选项"**，你可以用它手动覆盖 oneVPL 扩展编码选项缓冲区中的单个字段值。

---

## 格式

每行使用以下格式：

```
作用域.字段名=值
```

- **作用域**：字段属于哪个扩展缓冲区 — `CO`、`CO2`、`CO3` 或 `CODDI`
- **字段名**：结构体中字段的精确名称（区分大小写）
- **值**：`ON`、`OFF`、`UNKNOWN`（三态默认值），或数字值

以 `#` 或 `;` 开头的行视为注释，将被忽略。空行也会被跳过。

### 示例

```
# CodingOption
CO.RateDistortionOpt=ON
CO.CAVLC=OFF

# CodingOption2
CO2.FixedFrameRate=ON
CO2.RepeatPPS=OFF

# CodingOption3
CO3.FadeDetection=ON

# CodingOptionDDI
CODDI.Hme=ON
CODDI.BRCPrecision=3
CODDI.DDI.IntraPredBlockSize=1
CODDI.DDI.InterPredBlockSize=64
```

---

## 缓冲区与字段

### 1. `CO` — mfxExtCodingOption（第一代编码选项）

编码相关选项：熵编码、HRD 一致性、SEI 消息、场输出等。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `RateDistortionOpt` | 三态 | （用户设置） | 启用码率-失真优化。通过评估多种编码模式来提升编码质量。 |
| `MECostType` | — | — | **保留字段，必须为 0。** 如需 DDI 级别控制请用 `CODDI.IntraPredCostType`。 |
| `MESearchType` | — | — | **保留字段，必须为 0。** 如需 DDI 级别控制请用 `CODDI` 相关字段。 |
| `MVSearchWindow.x` | — | — | **保留字段，必须为 (0, 0)。** |
| `MVSearchWindow.y` | — | — | **保留字段，必须为 (0, 0)。** |
| `EndOfSequence` | 三态 | — | 已废弃。插入序列结束 NAL 单元。 |
| `FramePicture` | 三态 | — | 将隔行场编码为隔行帧。不影响逐行输入。 |
| `CAVLC` | 三态 | OFF | CAVLC 熵编码。OFF = 使用 CABAC（推荐）。 |
| `RecoveryPointSEI` | 三态 | （仅 IntraRef 开启时） | 在每个帧内刷新周期开头插入恢复点 SEI。帧内刷新未启用时忽略。 |
| `ViewOutput` | 三态 | — | MVC 编码器：将每个视图输出到单独的码流缓冲区。 |
| `NalHrdConformance` | 三态 | （用户设置） | ON 强制 AVC 编码器生成 HRD 合规码流。OFF 不保证不合规。 |
| `SingleSeiNalUnit` | 三态 | — | ON 将所有 SEI 消息放入同一个 NAL 单元。OFF/UNKNOWN 则将每个 SEI 放入独立的 NAL 单元。 |
| `VuiVclHrdParameters` | 三态 | （用户设置） | 在 VUI 中写入 VCL HRD 参数，值与 NAL HRD 参数相同（VBR 模式下）。 |
| `RefPicListReordering` | — | — | **保留字段，必须为 0。** |
| `ResetRefList` | 三态 | ON | 在 GOP 序列的 **非 IDR I 帧**上重置参考列表。 |
| `RefPicMarkRep` | 三态 | ON | 在输出码流中写入参考图像标记重复 SEI 消息。 |
| `FieldOutput` | 三态 | OFF（低功耗模式关闭时） | 在场编码模式下编码完一个场后立即输出码流。 |
| `IntraPredBlockSize` | — | — | **保留字段，必须为 0。** 如需 DDI 级别控制请用 `CODDI.DDI.IntraPredBlockSize`。 |
| `InterPredBlockSize` | — | — | **保留字段，必须为 0。** 如需 DDI 级别控制请用 `CODDI.DDI.InterPredBlockSize`。 |
| `MVPrecision` | — | — | **保留字段，必须为 0。** |
| `MaxDecFrameBuffering` | 数值 | NumRefFrame | DPB 中缓存的最大帧数。0 = 不指定。 |
| `AUDelimiter` | 三态 | （默认不设置） | 插入访问单元分隔符 NAL 单元。 |
| `EndOfStream` | 三态 | — | 已废弃。插入流结束 NAL 单元。 |
| `PicTimingSEI` | 三态 | ON | 插入带有 pic_struct 语法元素的图像时序 SEI。默认值为 ON。 |
| `VuiNalHrdParameters` | 三态 | （用户设置） | 在 VUI 头中插入 NAL HRD 参数。 |

---

### 2. `CO2` — mfxExtCodingOption2（第二代编码选项）

增强编码选项，涵盖码率控制、前瞻、自适应特性和质量控制。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `IntRefType` | 数值 | 2（IntraRef 开启时） | 帧内刷新类型。0=MFX_REFRESH_NO, 1=MFX_REFRESH_VERTICAL, 2=MFX_REFRESH_HORIZONTAL, 3=MFX_REFRESH_SLICE。 |
| `IntRefCycleSize` | 数值 | （根据 GOP 计算） | 刷新周期内的图像数量（2 或以上）。0 和 1 为无效值。 |
| `IntRefQPDelta` | mfxI16 | （用户设置，IntraRef 开启时） | 插入帧内 MB 的 QP 差值。范围：-51 到 51。仅在 IntraRefEncoding 启用时生效。 |
| `MaxFrameSize` | 数值 | （来自自适应最大帧大小） | 最大编码帧大小，单位**字节**。仅 VBR 系列码控模式生效。I 帧建议 5-10 倍目标帧大小。 |
| `MaxSliceSize` | 数值 | — | 最大 slice 大小（字节）。指定后覆盖其他 slice 数量设置。使用 Query 检查支持。 |
| `BitrateLimit` | 三态 | ON（API 2.9 已弃用） | 将码率限制在编码器范围内。非 CQP 模式下若 TargetKbps 超出范围则自动调整。仅 AVC 有效。**API 2.9 已弃用。** |
| `MBBRC` | 三态 | （用户/目标用途） | 宏块级码率控制。提升主观画质，但有一定性能开销。默认值取决于目标用途。 |
| `ExtBRC` | 三态 | （用户设置） | 启用外部码率控制。使用 Query API 检查支持。 |
| `LookAheadDepth` | 数值 | （启用前瞻时） | 前瞻深度。范围：10 到 100 帧。0 = 编码器默认值。值越大画质越好但延迟越高。 |
| `Trellis` | 数值（掩码） | （用户设置） | AVC Trellis 量化。位掩码：MFX_TRELLIS_OFF=1, MFX_TRELLIS_I=2, MFX_TRELLIS_P=4, MFX_TRELLIS_B=8。通过位或组合。 |
| `RepeatPPS` | 三态 | ON | 每帧重复 PPS NAL 单元。默认值为 ON。 |
| `BRefType` | 数值 | PYRAMID | B 帧参考类型。0=MFX_B_REF_UNKNOWN, 1=MFX_B_REF_OFF, 2=MFX_B_REF_PYRAMID。 |
| `AdaptiveI` | 三态 | （用户设置） | 允许将 P/B 帧类型改为 I 帧。GopOptFlag=MFX_GOP_STRICT 时忽略。 |
| `AdaptiveB` | 三态 | （用户设置） | 允许将 B 帧类型改为 P 帧。GopOptFlag=MFX_GOP_STRICT 时忽略。 |
| `LookAheadDS` | 数值 | OFF | 前瞻下采样。0=MFX_LOOKAHEAD_DS_UNKNOWN, 1=MFX_LOOKAHEAD_DS_OFF, 2=MFX_LOOKAHEAD_DS_2x, 3=MFX_LOOKAHEAD_DS_4x。 |
| `NumMbPerSlice` | 数值 | 0（使用 NumSlice） | 建议的 slice 大小（宏块数）。非零时覆盖 mfxInfoMFX::NumSlice。 |
| `SkipFrame` | 三态 | — | 启用 mfxEncodeCtrl::SkipFrame 参数。使用 Query 检查支持。 |
| `MinQPI` | 数值 (U8) | （未设置） | I 帧最小 QP。0 = 无限制（由驱动决定）。 |
| `MaxQPI` | 数值 (U8) | （未设置） | I 帧最大 QP。0 = 无限制。HEVC 下调整为 51+6*(BitDepth-8)。 |
| `MinQPP` | 数值 (U8) | （未设置） | P 帧最小 QP。0 = 无限制（由驱动决定）。 |
| `MaxQPP` | 数值 (U8) | （未设置） | P 帧最大 QP。0 = 无限制。HEVC 下调整为 51+6*(BitDepth-8)。 |
| `MinQPB` | 数值 (U8) | （未设置） | B 帧最小 QP。0 = 无限制（由驱动决定）。 |
| `MaxQPB` | 数值 (U8) | （未设置） | B 帧最大 QP。0 = 无限制。HEVC 下调整为 51+6*(BitDepth-8)。 |
| `FixedFrameRate` | 三态 | ON | 设置 VUI 中的 fixed_frame_rate_flag。 |
| `DisableDeblockingIdc` | 数值 | 0 | 禁用去块滤波器。0=完全启用。使用 Query 检查支持。 |
| `DisableVUI` | 三态 | — | 完全禁用输出码流中的 VUI。使用 Query 检查支持。 |
| `BufferingPeriodSEI` | 数值 | IFRAME | 何时插入缓冲周期 SEI。0=MFX_BPSEI_DEFAULT（编码器决定）, 1=MFX_BPSEI_IFRAME（每个 I 帧）。 |
| `EnableMAD` | 三态 | ON | 启用每帧 MAD（平均绝对差）上报（亮度分量），**非**死区量化。 |
| `UseRawRef` | 三态 | （用户设置） | 使用原始（输入）帧作为参考帧，而非重建帧。初始化时设为 ON 才能在运行时更改。 |

---

### 3. `CO3` — mfxExtCodingOption3（第三代编码选项）

高级编码选项：slice 控制、BRC 滑动窗口、加权预测、帧大小限制、编解码器特定功能。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `NumSliceI` | 数值 | — | I 帧的 slice 数量。使用 Query 检查支持。 |
| `NumSliceP` | 数值 | — | P 帧的 slice 数量。使用 Query 检查支持。 |
| `NumSliceB` | 数值 | — | B 帧的 slice 数量。使用 Query 检查支持。 |
| `WinBRCMaxAvgKbps` | 数值 | — | 在 WinBRCSize 滑动窗口内的最大平均码率。两者均设为 0 可禁用。 |
| `WinBRCSize` | 数值 | — | WinBRCMaxAvgKbps 的滑动窗口大小（帧数）。 |
| `QVBRQuality` | 数值 | （用户设置，仅 QVBR） | QVBR 码控模式的质量因子。范围：1-51，其中 1 = 最高质量。仅在 QVBR 码控激活时生效。 |
| `EnableMBQP` | 三态 | ON（CQP 模式） | 启用逐宏块 QP 控制。码控方法必须为 CQP。 |
| `IntRefCycleDist` | 数值 | 0（IntraRef 开启时） | 帧内刷新周期间距（帧数）。0 = 无间隔。 |
| `DirectBiasAdjustment` | 三态 | — | 降低 B Direct/Skip 模式的选择偏向。仅对 B 帧生效。 |
| `GlobalMotionBiasAdjustment` | 三态 | — | 启用全局运动偏向。 |
| `MVCostScalingFactor` | 数值 | — | 缩放 MV 代价。0=MV代价=0, 1=默认的1/2, 2=默认的1/4, 3=默认的1/8。 |
| `MBDisableSkipMap` | 三态 | — | 启用 mfxExtMBDisableSkipMap 的使用。 |
| `WeightedPred` | 三态 | DEFAULT / （用户） | 加权预测模式。见 WeightedPred 枚举：0=UNKNOWN, 1=DEFAULT, 2=EXPLICIT, 3=IMPLICIT。 |
| `WeightedBiPred` | 三态 | DEFAULT / （用户） | 加权双向预测模式。见 WeightedPred 枚举。 |
| `AspectRatioInfoPresent` | 三态 | ON | 在 VUI 中写入宽高比信息。 |
| `OverscanInfoPresent` | 三态 | ON | 在 VUI 中写入过扫描信息。 |
| `OverscanAppropriate` | 三态 | — | ON = 裁剪后画面适合过扫描显示。OFF = 整个区域包含重要视觉信息。 |
| `TimingInfoPresent` | 三态 | ON | 在 VUI 中写入帧率信息。 |
| `BitstreamRestriction` | 三态 | ON | 在 VUI 中写入码流限制信息。 |
| `LowDelayHrd` | 三态 | （用户设置） | AVC 语法元素 low_delay_hrd_flag（VUI）。低延迟直播场景必需。 |
| `MotionVectorsOverPicBoundaries` | 三态 | — | OFF = 帧间预测不使用图像边界外的采样点。 |
| `ScenarioInfo` | 数值 | （用户设置） | 使用场景提示。0=UNKNOWN, 1=DISPLAY_REMOTING, 2=VIDEO_CONFERENCE, 3=ARCHIVE, 4=LIVE_STREAMING, 5=CAMERA_CAPTURE, 6=VIDEO_SURVEILLANCE, 7=GAME_STREAMING, 8=REMOTE_GAMING。 |
| `ContentInfo` | 数值 | （用户设置） | 内容类型提示。0=UNKNOWN, 1=FULL_SCREEN_VIDEO, 2=NON_VIDEO_SCREEN。 |
| `PRefType` | 数值 | SIMPLE / PYRAMID | P 帧参考类型。0=MFX_P_REF_DEFAULT, 1=MFX_P_REF_SIMPLE, 2=MFX_P_REF_PYRAMID。 |
| `FadeDetection` | 三态 | （用户设置） | 启用内部渐变检测算法来计算 pred_weight_table 的值。 |
| `GPB` | 三态 | （用户设置，仅 HEVC） | OFF 使 HEVC 编码器使用常规 P 帧而非广义 P/B 帧。 |
| `MaxFrameSizeI` | 数值 | — | 与 CO2.MaxFrameSize 相同，但仅对 I 帧生效。MaxFrameSizeP 设置时必须同时设置此项。 |
| `MaxFrameSizeP` | 数值 | — | 与 CO2.MaxFrameSize 相同，但仅对 P/B 帧生效。0 = 与 MaxFrameSizeI 相同。 |
| `EnableQPOffset` | 三态 | ON | 启用基于金字塔层的 QPOffset QP 控制（CQP 模式下）。 |
| `QPOffset[8]` | — | — | **不支持通过文本解析器设置。** 每金字塔层的 QP 偏移数组。需在 C++ 代码中配置。 |
| `NumRefActiveP[8]` | — | — | **不支持通过文本解析器设置。** P 帧每层的活动参考帧数。需在 C++ 代码中配置。 |
| `NumRefActiveBL0[8]` | — | — | **不支持通过文本解析器设置。** B 帧（L0）每层的活动参考帧数。需在 C++ 代码中配置。 |
| `NumRefActiveBL1[8]` | — | — | **不支持通过文本解析器设置。** B 帧（L1）每层的活动参考帧数。需在 C++ 代码中配置。 |
| `TransformSkip` | 三态 | （用户设置） | HEVC: ON 在 PPS 中设置 transform_skip_enabled_flag=1。对屏幕内容编码有用。 |
| `TargetChromaFormatPlus1` | 数值 | （色度+1） | 目标色度格式减 1。0=自动（与源相同）, 1=单色, 2=YUV420, 3=YUV422, 4=YUV444。 |
| `TargetBitDepthLuma` | 数值 | 0（自动）/ 10 | 目标亮度位深。0 = 与源相同。可与源不同。 |
| `TargetBitDepthChroma` | 数值 | 0（自动）/ 10 | 目标色度位深。0 = 与源相同。可与源不同。 |
| `BRCPanicMode` | 三态 | — | 控制 AVC 和 MPEG2 编码器的 panic 模式。 |
| `LowDelayBRC` | 三态 | — | 严格帧大小容差（VBR/QVBR/VCM 模式）。ON = 严格遵守 MaxKbps 设置的平均帧大小。 |
| `EnableMBForceIntra` | 三态 | ON | 启用 AVC 编码器的 mfxExtMBForceIntra。 |
| `AdaptiveMaxFrameSize` | 三态 | — | ON 允许 BRC 在场景变化时使用比 MaxFrameSizeP 更大的 P/B 帧。不支持 LowPower ON 或 MaxFrameSizeP=0。 |
| `RepartitionCheckEnable` | 三态 | ON | AVC 编码器分区预测。ON 偏向质量，OFF 偏向性能。 |
| `EncodedUnitsInfo` | 三态 | — | ON 使编码单元信息在 mfxExtEncodedUnitsInfo 中可用。 |
| `EnableNalUnitType` | 三态 | — | HEVC: ON 启用应用程序通过 mfxEncodeCtrl::MfxNalUnitType 提供 NAL 单元类型。使用 Query 检查支持。 |
| `ExtBrcAdaptiveLTR` | 三态 | — | ExtBRC 的自适应长期参考帧。 |

---

### 4. `CODDI` — mfxExtCodingOptionDDI（设备驱动接口选项）

底层驱动调优参数。**"凡人无法理解的魔法。"** 这些参数直接控制硬件编码器。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `IntraPredCostType` | 数值 | 8 | 帧内预测代价函数。1=SAD, 2=SSD, 4=SATD_HADAMARD, 8=SATD_HARR。 |
| `MEInterpolationMethod` | 数值 | 8 | 运动估计插值方法。1=VME4TAP, 2=BILINEAR, 4=WMV4TAP, 8=AVC6TAP。 |
| `MEFractionalSearchType` | 数值 | 16 | 亚像素运动估计搜索模式。1=FULL, 2=HALF, 4=SQUARE, 8=HQ, 16=DIAMOND。 |
| `MaxMVs` | 数值 | （驱动默认） | 每个宏块的最大运动矢量数。 |
| `SkipCheck` | 三态 | （驱动默认） | 启用跳过宏块检测。 |
| `DirectCheck` | 三态 | ON | 启用直接预测模式检测。 |
| `BiDirSearch` | 三态 | ON | 启用双向运动搜索。 |
| `MBAFF` | 三态 | ON | 宏块自适应帧/场编码（隔行内容）。 |
| `FieldPrediction` | 三态 | ON | 启用隔行内容的场级预测。 |
| `RefOppositeField` | 三态 | ON | 允许隔行编码中从对向场参考。 |
| `ChromaInME` | 三态 | （驱动默认） | 在运动估计中包括色度平面。 |
| `WeightedPrediction` | 三态 | ON | DDI 级 P 帧加权预测。 |
| `MVPrediction` | 三态 | ON | DDI 级运动矢量预测控制。 |
| `BRCPrecision` | 数值 | 3（最高） | 码率控制精度。0=默认, 1=最低, 2=正常, 3=最高。 |
| `RefRaw` | 三态 | （用户设置） | 使用原始帧作为 VME 参考。ON = 原始输入, OFF = 重建帧。 |
| `ConstQP` | 数值 | （驱动默认） | 强制恒定 QP 模式，绕过码率控制。 |
| `GlobalSearch` | 数值 | 1（长搜索） | 全局运动搜索范围。0=默认, 1=长, 2=中, 3=短。 |
| `LocalSearch` | 数值 | 6（穷举） | 局部运动搜索模式。值 0-8。6=穷举是最彻底的。 |
| `EarlySkip` | 数值 | 0（自动） | 早期跳过决策控制。0=让驱动选择, 1=启用, 2=禁用。 |
| `LaScaleFactor` | 数值 | （驱动默认） | 前瞻缩放因子。0=自动, 1=1倍, 2=2倍, 4=4倍。旧版 H.264 已弃用。 |
| `IBC` | 三态 | ON | 帧内块复制（屏幕内容编码）。 |
| `Palette` | 三态 | ON | 调色板模式（屏幕内容编码）。 |
| `StrengthN` | 数值 | （驱动默认） | 编码强度级别 = StrengthN / 100.0。 |
| `FractionalQP` | 数值 | 1（启用） | 启用分数 QP。0=禁用, 1=启用。 |
| `NumActiveRefP` | 数值 | （驱动默认） | P 帧的活跃参考帧数。 |
| `NumActiveRefBL0` | 数值 | （驱动默认） | B 帧 L0 方向的活跃参考帧数。 |
| `NumActiveRefBL1` | 数值 | （驱动默认） | B 帧 L1 方向的活跃参考帧数。 |
| `DisablePSubMBPartition` | 三态 | OFF | 禁用 P 帧子宏块分割。OFF = 允许所有分割（更好的画质）。 |
| `DisableBSubMBPartition` | 三态 | OFF | 禁用 B 帧子宏块分割。OFF = 允许所有分割（更好的画质）。 |
| `WeightedBiPredIdc` | 数值 | 2（隐式） | 加权双向预测模式。0=OFF, 1=显式（不支持）, 2=隐式。 |
| `DirectSpatialMvPredFlag` | 三态 | ON | 直接模式 MV 预测类型。ON=空间, OFF=时间。 |
| `Transform8x8Mode` | 三态 | ON | 启用 8x8 变换模式（改善高清内容的画质）。 |
| `LongStartCodes` | 三态 | （驱动默认） | 对所有 NAL 单元使用长起始码。 |
| `CabacInitIdcPlus1` | 数值 | （驱动默认） | CABAC 初始化表。0=默认, 1=cabac_init_idc=0，最高为3。 |
| `QpUpdateRange` | 数值 | （驱动默认） | BRC 的 QP 调整范围。 |
| `RegressionWindow` | 数值 | （驱动默认） | BRC 的回归分析窗口大小。 |
| `LookAheadDependency` | 数值 | （驱动默认） | 前瞻依赖深度（必须小于前瞻深度）。 |
| `Hme` | 三态 | ON | 分层运动估计。使用多级搜索获得更好的精度。 |
| `WriteIVFHeaders` | 三态 | OFF | 写入 IVF 容器头。OFF = 正常码流输出。 |
| `RefreshFrameContext` | 三态 | ON | 刷新编码器帧上下文以改善画质。 |
| `ChangeFrameContextIdxForTS` | 三态 | ON | 为时间可扩展性改变帧上下文索引。 |
| `SuperFrameForTS` | 三态 | ON | 用于时间可扩展性的超级帧模式。 |
| `QpAdjust` | 三态 | ON | 启用 SPS 级 QP 调整。 |
| `TMVP` | 三态 | ON | 时间运动矢量预测。提高压缩效率。 |
| `DDI.IntraPredBlockSize` | 数值 | 1 (4x4) | DDI 级帧内预测块大小。1=4x4, 2=8x8, 4=16x16, 8=PCM。 |
| `DDI.InterPredBlockSize` | 数值 | 64 (4x4) | DDI 级帧间预测块大小。1=16x16, 2=16x8, 4=8x16, ... 64=4x4。 |

---

## 值类型

| 类型 | 可接受的值 |
|------|-----------|
| **三态** | `ON`、`OFF`、`UNKNOWN`（UNKNOWN = 让驱动选择默认值） |
| **数值** | 字段有效范围内的任意整数值 |
| **数值（掩码）** | 表示位掩码的整数；通过相加来组合多个值 |
| **数值 (U8)** | 0-255 范围内的整数 |

---

## 注意事项

- 自定义选项会**覆盖**插件正常配置界面设置的对应值。
- 对于当前未启用的缓冲区（例如 AV1 编码时的 CODDI），其字段将被静默忽略并记录警告日志。
- 设置不正确可能导致编码器失败或画质下降，请谨慎使用。
- 插件会在 `info` 级别日志中记录所有已应用的自定义选项。请在 OBS 日志中查看以验证你的设置。
- CO3 中的数组字段（`QPOffset[8]`、`NumRefActiveP[8]`、`NumRefActiveBL0[8]`、`NumRefActiveBL1[8]`）**不支持**通过此文本解析器设置。如需使用请在 C++ 代码中配置。
- CO 中标有 **"保留字段，必须为 0"** 的字段仅为向后兼容而存在，不应设置。请改用对应的 CODDI 版本。