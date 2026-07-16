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
CO.CAVLC=OFF

# CodingOption2
CO2.FixedFrameRate=ON
CO2.RepeatPPS=OFF

# CodingOption3
CO3.LowDelayBRC=ON

# CodingOptionDDI
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
| `RateDistortionOpt` | 三态 | （用户设置） | 启用率失真优化。 |
| `CAVLC` | 三态 | OFF | 熵编码。OFF = 使用 CABAC（推荐），ON = 使用 CAVLC。 |
| `MECostType` | 数值 | 8 | 运动估计代价类型。1=SAD, 2=SSD, 4=SATD_HADAMARD, 8=SATD_HARR。也可通过 `CODDI.IntraPredCostType` 调整。 |
| `MESearchType` | 数值 | 16 | 运动估计算法。1=FULL, 2=HALF, 4=SQUARE, 8=HQ, 16=DIAMOND。 |
| `MVSearchWindow.x` | 数值 | AVC=16 / HEVC=32 | 运动估计搜索窗口宽度（像素）。 |
| `MVSearchWindow.y` | 数值 | AVC=16 / HEVC=32 | 运动估计搜索窗口高度（像素）。 |
| `EndOfSequence` | 三态 | — | 已废弃。插入序列结束 NAL 单元。 |
| `FramePicture` | 三态 | UNKNOWN | 将隔行场编码为隔行帧。不影响逐行输入。 |
| `RecoveryPointSEI` | 三态 | ON（IntraRef 开启时） | 在每个帧内刷新周期开头插入恢复点 SEI。帧内刷新未启用时忽略。 |
| `ViewOutput` | 三态 | UNKNOWN | MVC 编码器：将每个视图输出到单独的码流缓冲区。 |
| `NalHrdConformance` | 三态 | （HRD 设置） | ON 强制 AVC 编码器生成 HRD 合规码流。OFF 不保证不合规。 |
| `SingleSeiNalUnit` | 三态 | UNKNOWN | ON 将所有 SEI 消息放入同一个 NAL 单元。OFF/UNKNOWN 则将每个 SEI 放入独立的 NAL 单元。 |
| `VuiVclHrdParameters` | 三态 | （HRD 设置） | 在 VUI 中写入 VCL HRD 参数，值与 NAL HRD 参数相同（VBR 模式下）。 |
| `VuiNalHrdParameters` | 三态 | （HRD 设置） | 在 VUI 头中插入 NAL HRD 参数。 |
| `RefPicListReordering` | 三态 | ON | 保留字段，但插件实际设为 ON。 |
| `ResetRefList` | 三态 | ON | 在 GOP 序列的 **非 IDR I 帧**上重置参考列表。 |
| `RefPicMarkRep` | 三态 | ON | 在输出码流中写入参考图像标记重复 SEI 消息。 |
| `FieldOutput` | 三态 | LP=ON / 无LP=OFF | 在场编码模式下编码完一个场后立即输出码流。低功耗模式下为 ON。 |
| `IntraPredBlockSize` | 数值 | 3 (MIN_4X4) | 最小帧内预测块大小。0=UNKNOWN, 1=16x16, 2=8x8, 3=4x4。DDI 级别控制请用 `CODDI.DDI.IntraPredBlockSize`。 |
| `InterPredBlockSize` | 数值 | 3 (MIN_4X4) | 最小帧间预测块大小。0=UNKNOWN, 1=16x16, 2=8x8, 3=4x4。DDI 级别控制请用 `CODDI.DDI.InterPredBlockSize`。 |
| `MVPrecision` | 数值 | 4 (QUARTERPEL) | 运动估计精度。0=UNKNOWN, 1=INTEGER, 2=HALFPEL, 4=QUARTERPEL。 |
| `MaxDecFrameBuffering` | 数值 | NumRefFrame | DPB 中缓存的最大帧数。0 = 不指定。 |
| `AUDelimiter` | 三态 | UNKNOWN | 插入访问单元分隔符 NAL 单元。 |
| `EndOfStream` | 三态 | — | 已废弃。插入流结束 NAL 单元。 |
| `PicTimingSEI` | 三态 | ON | 插入带有 pic_struct 语法元素的图像时序 SEI。API 规范默认值为 ON。 |

---

### 2. `CO2` — mfxExtCodingOption2（第二代编码选项）

增强编码选项，涵盖码率控制和质量控制。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `IntRefType` | 数值 | 0（关闭） | 帧内刷新类型。0=MFX_REFRESH_NO, 1=垂直, 2=水平, 3=切片。 |
| `IntRefCycleSize` | 数值 | GopRefDist 或 2 | 帧内刷新周期内图片数（≥2）。0/1 无效。 |
| `IntRefQPDelta` | 数值 (S16) | 0 | 插入的帧内 MB 的 QP 差异。范围 -51..51。 |
| `MaxFrameSize` | 数值 (U32) | （用户设置） | 最大编码帧大小（字节，仅 VBR 系列）。 |
| `MaxSliceSize` | 数值 (U32) | 0 | 最大 slice 大小（字节）。会覆盖其他 slice 数量控制。使用 Query 检查支持。 |
| `BitrateLimit` | 三态 | — | **已废弃。** 调整码率以适应编码器限制范围。仅 AVC。 |
| `MBBRC` | 三态 | （用户设置） | 宏块级码率控制。提升主观质量，可能影响客观指标。 |
| `LookAheadDepth` | 数值 | 0（驱动默认） | 前瞻深度（10-100）。0 = 由编码器决定。仅在启用 Lookahead 时生效。 |
| `Trellis` | 数值（掩码） | （用户设置） | Trellis 量化控制（AVC）。位掩码：1=I, 2=P, 4=B。0=OFF, 7=IPB。 |
| `RepeatPPS` | 三态 | ON | 每帧重复 PPS NAL 单元。默认值为 ON。 |
| `BRefType` | 数值 | PYRAMID | B 帧参考类型。0=MFX_B_REF_UNKNOWN, 1=MFX_B_REF_OFF, 2=MFX_B_REF_PYRAMID。 |
| `AdaptiveI` | 三态 | （用户设置） | 允许 P/B 帧转换为 I 帧。GopOptFlag=MFX_GOP_STRICT 时忽略。 |
| `AdaptiveB` | 三态 | （用户设置） | 允许 B 帧转换为 P 帧。GopOptFlag=MFX_GOP_STRICT 时忽略。 |
| `LookAheadDS` | 数值 | OFF | 前瞻下采样。0=OFF, 1=2x, 2=4x。仅 CBR/VBR/AVBR/ICQ/QVBR 模式。 |
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
| `UseRawRef` | 三态 | （用户设置） | 使用原始帧作参考，而非重建帧。 |

---

### 3. `CO3` — mfxExtCodingOption3（第三代编码选项）

高级编码选项：slice 控制、帧大小限制、编解码器特定功能。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `NumSliceI` | 数值 | — | I 帧的 slice 数量。使用 Query 检查支持。 |
| `NumSliceP` | 数值 | — | P 帧的 slice 数量。使用 Query 检查支持。 |
| `NumSliceB` | 数值 | — | B 帧的 slice 数量。使用 Query 检查支持。 |
| `WinBRCMaxAvgKbps` | 数值 | 0 | 滑动窗口内最大平均码率（CBR/VBR/LA/LA_HRD/QVBR）。0 = 禁用滑动窗口。 |
| `WinBRCSize` | 数值 | 0 | 滑动窗口大小（帧数）。两者都设为 0 才能禁用。 |
| `QVBRQuality` | 数值 | 0 | QVBR 码控的质量因子（1-51，1 = 最好）。 |
| `EnableMBQP` | 三态 | ON（CQP 模式） | 启用逐宏块 QP 控制。码控方法必须为 CQP。 |
| `IntRefCycleDist` | 数值 | 0（IntraRef 开启时） | 帧内刷新周期间距（帧数）。0 = 无间隔。 |
| `DirectBiasAdjustment` | 三态 | （用户设置） | 让模式决策偏向更少的 B Direct/Skip 类型（仅 B 帧）。 |
| `GlobalMotionBiasAdjustment` | 三态 | （用户设置） | 启用全局运动偏置。 |
| `MVCostScalingFactor` | 数值 | （用户设置） | MV 代价缩放。0=零代价, 1=1/2, 2=1/4, 3=1/8。仅当 GlobalMotionBiasAdjustment=ON 时生效。 |
| `MBDisableSkipMap` | 三态 | — | 启用 mfxExtMBDisableSkipMap 的使用。 |
| `WeightedPred` | 数值 | （用户设置） | P 帧加权预测模式。0=UNKNOWN, 1=DEFAULT, 2=EXPLICIT, 3=IMPLICIT。 |
| `WeightedBiPred` | 数值 | （用户设置） | B 帧加权预测模式。取值与 WeightedPred 相同。 |
| `AspectRatioInfoPresent` | 三态 | ON | 在 VUI 中写入宽高比信息。 |
| `OverscanInfoPresent` | 三态 | ON | 在 VUI 中写入过扫描信息。 |
| `OverscanAppropriate` | 三态 | — | ON = 裁剪后画面适合过扫描显示。OFF = 整个区域包含重要视觉信息。 |
| `TimingInfoPresent` | 三态 | ON | 在 VUI 中写入帧率信息。 |
| `BitstreamRestriction` | 三态 | ON | 在 VUI 中写入码流限制信息。 |
| `LowDelayHrd` | 三态 | （用户设置） | AVC 语法元素 low_delay_hrd_flag（VUI）。低延迟直播场景必需。 |
| `MotionVectorsOverPicBoundaries` | 三态 | （用户设置） | 允许 MV 引用画面边界外的样本。 |
| `ScenarioInfo` | 数值 | （用户设置） | 编码场景提示。见 ScenarioInfo 枚举。 |
| `ContentInfo` | 数值 | （用户设置） | 内容提示。见 ContentInfo 枚举。 |
| `PRefType` | 数值 | SIMPLE/PYRAMID | 当 GopRefDist=1 时指定参考列表构造模式。0=DEFAULT, 1=SIMPLE, 2=PYRAMID。 |
| `FadeDetection` | 三态 | （用户设置） | 启用内部淡入淡出检测，用于 pred_weight_table 计算。 |
| `GPB` | 三态 | （用户设置） | 仅 HEVC。OFF = 使用常规 P 帧而非 GPB。 |
| `MaxFrameSizeI` | 数值 (U32) | — | 与 CO2.MaxFrameSize 相同，但仅对 I 帧生效。MaxFrameSizeP 设置时必须同时设置此项。 |
| `MaxFrameSizeP` | 数值 (U32) | — | 与 CO2.MaxFrameSize 相同，但仅对 P/B 帧生效。0 = 与 MaxFrameSizeI 相同。 |
| `EnableQPOffset` | 三态 | ON | 启用基于金字塔层的 QPOffset QP 控制（CQP 模式下）。 |
| `QPOffset[0..7]` | 数值 (S16) | （驱动默认） | 每个金字塔层的 QP 偏移。FrameQP = QPX + QPOffset[layer]。 |
| `NumRefActiveP[0..7]` | 数值 | （驱动默认） | 每个金字塔层 P 帧的最大活跃参考数。 |
| `NumRefActiveBL0[0..7]` | 数值 | （驱动默认） | 每个金字塔层 B 帧 L0 方向的最大活跃参考数。 |
| `NumRefActiveBL1[0..7]` | 数值 | （驱动默认） | 每个金字塔层 B 帧 L1 方向的最大活跃参考数。 |
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
| `AdaptiveLTR` | 三态 | （用户设置） | 编码器自适应标记/修改/移除长期参考帧。（已废弃别名 `ExtBrcAdaptiveLTR` 会被静默重映射。） |
| `AdaptiveCQM` | 三态 | （用户设置） | 每帧自适应选择量化矩阵。 |
| `AdaptiveRef` | 三态 | （用户设置） | 自适应参考帧列表选择以提升质量。可能增加延迟。 |

---

### 4. `CODDI` — mfxExtCodingOptionDDI（设备驱动接口选项）

底层驱动调优参数。**"凡人无法理解的魔法。"** 这些参数直接控制硬件编码器。

| 字段 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `RefRaw` | 三态 | （用户设置） | 使用原始帧作为 VME 参考。ON = 原始输入, OFF = 重建帧。 |
| `StrengthN` | 数值 | （驱动默认） | 编码强度级别 = StrengthN / 100.0。 |
| `NumActiveRefP` | 数值 | （驱动默认） | P 帧的活跃参考帧数。 |
| `NumActiveRefBL0` | 数值 | （驱动默认） | B 帧 L0 方向的活跃参考帧数。 |
| `NumActiveRefBL1` | 数值 | （驱动默认） | B 帧 L1 方向的活跃参考帧数。 |
| `DisablePSubMBPartition` | 三态 | OFF | 禁用 P 帧子宏块分割。OFF = 允许所有分割（更好的画质）。 |
| `WeightedBiPredIdc` | 数值 | 2（隐式） | 加权双向预测模式。0=OFF, 1=显式（不支持）, 2=隐式。 |
| `DirectSpatialMvPredFlag` | 三态 | ON | 直接模式 MV 预测类型。ON=空间, OFF=时间。 |
| `Transform8x8Mode` | 三态 | ON | 启用 8x8 变换模式（改善高清内容的画质）。 |
| `LongStartCodes` | 三态 | （驱动默认） | 对所有 NAL 单元使用长起始码。 |
| `CabacInitIdcPlus1` | 数值 | （驱动默认） | CABAC 初始化表。0=默认, 1=cabac_init_idc=0，最高为3。 |
| `QpUpdateRange` | 数值 | （驱动默认） | BRC 的 QP 调整范围。 |
| `RegressionWindow` | 数值 | （驱动默认） | BRC 的回归分析窗口大小。 |
| `LookAheadDependency` | 数值 | （驱动默认） | 前瞻依赖深度（必须小于前瞻深度）。 |
| `RefreshFrameContext` | 三态 | ON | 刷新编码器帧上下文以改善画质。 |
| `QpAdjust` | 三态 | ON | 启用 SPS 级 QP 调整。 |
| `TMVP` | 三态 | ON | 时间运动矢量预测。提高压缩效率。 |

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
- CO 中标有 **"保留字段，必须为 0"** 的字段仅为向后兼容而存在，不应设置。请改用对应的 CODDI 版本。