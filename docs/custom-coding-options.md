# Custom Coding Options

This plugin provides a text input field **"Custom Coding Options"** in the encoder settings panel. You can use it to manually override individual fields in the oneVPL extended coding option buffers.

---

## Format

Each line uses the following format:

```
Scope.FieldName=Value
```

- **Scope**: Which extended buffer the field belongs to — `CO`, `CO2`, `CO3`, or `CODDI`
- **FieldName**: The exact name of the struct field (case-sensitive)
- **Value**: `ON`, `OFF`, `UNKNOWN` (tri-state default), or a numeric value

Lines starting with `#` or `;` are treated as comments and ignored. Empty lines are skipped.

### Example

```
# CodingOption
CO.CAVLC=OFF

# CodingOption2
CO2.FixedFrameRate=ON
CO2.RepeatPPS=OFF

# CodingOption3
CO3.LowDelayBRC=ON

# CodingOptionDDI
CODDI.Hme=ON
CODDI.BRCPrecision=3
CODDI.DDI.IntraPredBlockSize=1
CODDI.DDI.InterPredBlockSize=64
```

---

## Buffers and Fields

### 1. `CO` — mfxExtCodingOption (First-generation coding options)

Encoding codec options for entropy coding, HRD conformance, SEI messages, and field output.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `MECostType` | — | — | **Reserved. Must be 0.** Use `CODDI.IntraPredCostType` instead for DDI-level control. |
| `MESearchType` | — | — | **Reserved. Must be 0.** Use `CODDI` fields for DDI-level search control. |
| `MVSearchWindow.x` | — | — | **Reserved. Must be (0, 0).** |
| `MVSearchWindow.y` | — | — | **Reserved. Must be (0, 0).** |
| `EndOfSequence` | tri-state | — | Deprecated. Insert End of Sequence NAL unit. |
| `FramePicture` | tri-state | — | Encode interlaced fields as interlaced frames. Does not affect progressive input. |
| `CAVLC` | tri-state | OFF | CAVLC entropy coding. OFF = CABAC (recommended). |
| `RecoveryPointSEI` | tri-state | (only when IntraRef ON) | Insert Recovery Point SEI at the beginning of every intra refresh cycle. Ignored if intra refresh is not enabled. |
| `ViewOutput` | tri-state | — | MVC encoder: output each view in a separate bitstream buffer. |
| `NalHrdConformance` | tri-state | (user setting) | ON forces AVC encoder to produce an HRD-conformant bitstream. OFF does not guarantee non-conformance. |
| `SingleSeiNalUnit` | tri-state | — | ON puts all SEI messages in a single NAL unit. OFF/UNKNOWN puts each SEI in its own NAL unit. |
| `VuiVclHrdParameters` | tri-state | (user setting) | Write VCL HRD parameters in VUI with values identical to NAL HRD parameters (VBR mode). |
| `RefPicListReordering` | — | — | **Reserved. Must be 0.** |
| `ResetRefList` | tri-state | ON | Reset reference list to **non-IDR I-frames** of a GOP sequence. |
| `RefPicMarkRep` | tri-state | ON | Write reference picture marking repetition SEI message into the output bitstream. |
| `FieldOutput` | tri-state | OFF (Lowpower off) | Output bitstream immediately after encoding a field in field-encoding mode. |
| `IntraPredBlockSize` | — | — | **Reserved. Must be 0.** Use `CODDI.DDI.IntraPredBlockSize` for DDI-level control. |
| `InterPredBlockSize` | — | — | **Reserved. Must be 0.** Use `CODDI.DDI.InterPredBlockSize` for DDI-level control. |
| `MVPrecision` | — | — | **Reserved. Must be 0.** |
| `MaxDecFrameBuffering` | numeric | NumRefFrame | Maximum number of frames buffered in the DPB. 0 = unspecified. |
| `AUDelimiter` | tri-state | (not set by default) | Insert Access Unit Delimiter NAL units. |
| `EndOfStream` | tri-state | — | Deprecated. Insert End of Stream NAL unit. |
| `PicTimingSEI` | tri-state | ON | Insert Picture Timing SEI with pic_struct syntax element. Default is ON. |

---

### 2. `CO2` — mfxExtCodingOption2 (Second-generation coding options)

Enhanced encoding options for BRC and quality controls.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `RepeatPPS` | tri-state | ON | Repeat PPS NAL unit with each frame. Default is ON. |
| `BRefType` | numeric | PYRAMID | B-frame reference type. 0=MFX_B_REF_UNKNOWN, 1=MFX_B_REF_OFF, 2=MFX_B_REF_PYRAMID. |
| `NumMbPerSlice` | numeric | 0 (use NumSlice) | Suggested slice size in macroblocks. Overrides mfxInfoMFX::NumSlice if non-zero. |
| `SkipFrame` | tri-state | — | Enable mfxEncodeCtrl::SkipFrame parameter. Use Query to check support. |
| `MinQPI` | numeric (U8) | (not set) | Minimum QP for I-frames. 0 = no limit (driver decides). |
| `MaxQPI` | numeric (U8) | (not set) | Maximum QP for I-frames. 0 = no limit. HEVC: adjusted to 51+6*(BitDepth-8). |
| `MinQPP` | numeric (U8) | (not set) | Minimum QP for P-frames. 0 = no limit (driver decides). |
| `MaxQPP` | numeric (U8) | (not set) | Maximum QP for P-frames. 0 = no limit. HEVC: adjusted to 51+6*(BitDepth-8). |
| `MinQPB` | numeric (U8) | (not set) | Minimum QP for B-frames. 0 = no limit (driver decides). |
| `MaxQPB` | numeric (U8) | (not set) | Maximum QP for B-frames. 0 = no limit. HEVC: adjusted to 51+6*(BitDepth-8). |
| `DisableDeblockingIdc` | numeric | 0 | Disable deblocking filter. 0=fully enabled. Use Query to check support. |
| `DisableVUI` | tri-state | — | Completely disable VUI in the output bitstream. Use Query to check support. |
| `EnableMAD` | tri-state | ON | Enable per-frame reporting of Mean Absolute Difference (luma). Not dead-zone quantization. |
| `BufferingPeriodSEI` | numeric | IFRAME | When to insert buffering period SEI. 0=MFX_BPSEI_DEFAULT (encoder decides), 1=MFX_BPSEI_IFRAME (every I-frame). |
| `FixedFrameRate` | tri-state | ON | Set fixed_frame_rate_flag in VUI. |

---

### 3. `CO3` — mfxExtCodingOption3 (Third-generation coding options)

Advanced encoding options for slices, frame-size limits, and codec-specific features.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `NumSliceI` | numeric | — | Number of slices for I-frames. Use Query to check support. |
| `NumSliceP` | numeric | — | Number of slices for P-frames. Use Query to check support. |
| `NumSliceB` | numeric | — | Number of slices for B-frames. Use Query to check support. |
| `EnableMBQP` | tri-state | ON (CQP mode) | Enable per-macroblock QP control. Rate control must be CQP. |
| `IntRefCycleDist` | numeric | 0 (when IntraRef ON) | Distance between beginnings of intra-refresh cycles in frames. 0 = no gap. |
| `MBDisableSkipMap` | tri-state | — | Enable mfxExtMBDisableSkipMap usage. |
| `AspectRatioInfoPresent` | tri-state | ON | Write aspect ratio information in VUI. |
| `OverscanInfoPresent` | tri-state | ON | Write overscan info in VUI. |
| `OverscanAppropriate` | tri-state | — | ON = cropped picture suitable for overscan display. OFF = entire region contains important info. |
| `TimingInfoPresent` | tri-state | ON | Write frame rate info in VUI. |
| `BitstreamRestriction` | tri-state | ON | Write bitstream restriction info in VUI. |
| `LowDelayHrd` | tri-state | (user setting) | AVC syntax element low_delay_hrd_flag (VUI). Required for low-latency streaming. |
| `MaxFrameSizeI` | numeric | — | Same as CO2.MaxFrameSize but for I-frames only. Must be set if MaxFrameSizeP is set. |
| `MaxFrameSizeP` | numeric | — | Same as CO2.MaxFrameSize but for P/B-frames only. 0 = same as MaxFrameSizeI. |
| `EnableQPOffset` | tri-state | ON | Enable QPOffset per pyramid layer QP control (CQP mode). |
| `TransformSkip` | tri-state | (user setting) | HEVC: ON sets transform_skip_enabled_flag=1 in PPS. Useful for screen content. |
| `TargetChromaFormatPlus1` | numeric | (chroma+1) | Target chroma format minus 1. 0=auto(source), 1=MONO, 2=YUV420, 3=YUV422, 4=YUV444. |
| `TargetBitDepthLuma` | numeric | 0 (auto) / 10 | Target luma bit depth. 0 = same as source. May differ from source. |
| `TargetBitDepthChroma` | numeric | 0 (auto) / 10 | Target chroma bit depth. 0 = same as source. May differ from source. |
| `BRCPanicMode` | tri-state | — | Control panic mode in AVC and MPEG2 encoders. |
| `LowDelayBRC` | tri-state | — | Strict frame size tolerance for VBR/QVBR/VCM. ON = strictly obey average frame size set by MaxKbps. |
| `EnableMBForceIntra` | tri-state | ON | Enable mfxExtMBForceIntra for AVC encoder. |
| `AdaptiveMaxFrameSize` | tri-state | — | ON allows BRC to use larger P/B-frame size than MaxFrameSizeP on scene change. Not supported with LowPower ON or MaxFrameSizeP=0. |
| `RepartitionCheckEnable` | tri-state | ON | AVC encoder partition prediction. ON favors quality, OFF favors performance. |
| `EncodedUnitsInfo` | tri-state | — | ON makes encoded units info available in mfxExtEncodedUnitsInfo. |
| `EnableNalUnitType` | tri-state | — | HEVC: ON enables application-provided NAL unit type via mfxEncodeCtrl::MfxNalUnitType. Use Query to check support. |
| `ExtBrcAdaptiveLTR` | tri-state | — | Adaptive Long Term Reference for ExtBRC. |

---

### 4. `CODDI` — mfxExtCodingOptionDDI (Device Driver Interface options)

Low-level driver tuning parameters. **"Magic beyond the control of mere mortals."** These directly control the hardware encoder.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `IntraPredCostType` | numeric | 8 | Intra prediction cost function. 1=SAD, 2=SSD, 4=SATD_HADAMARD, 8=SATD_HARR. |
| `MEInterpolationMethod` | numeric | 8 | Motion estimation interpolation method. 1=VME4TAP, 2=BILINEAR, 4=WMV4TAP, 8=AVC6TAP. |
| `MEFractionalSearchType` | numeric | 16 | Fractional-pixel ME search pattern. 1=FULL, 2=HALF, 4=SQUARE, 8=HQ, 16=DIAMOND. |
| `MaxMVs` | numeric | (driver default) | Maximum number of motion vectors per macroblock. |
| `SkipCheck` | tri-state | (driver default) | Enable skip macroblock checking. |
| `DirectCheck` | tri-state | ON | Enable direct prediction mode checking. |
| `BiDirSearch` | tri-state | ON | Enable bidirectional motion search. |
| `MBAFF` | tri-state | ON | Macroblock-Adaptive Frame/Field coding (interlaced content). |
| `FieldPrediction` | tri-state | ON | Enable field-level prediction for interlaced content. |
| `RefOppositeField` | tri-state | ON | Allow reference from opposite field in interlaced coding. |
| `ChromaInME` | tri-state | (driver default) | Include chroma planes in motion estimation. |
| `WeightedPrediction` | tri-state | ON | DDI-level weighted prediction for P-frames. |
| `MVPrediction` | tri-state | ON | DDI-level motion vector prediction control. |
| `BRCPrecision` | numeric | 3 (highest) | Bitrate control precision. 0=default, 1=lowest, 2=normal, 3=highest. |
| `RefRaw` | tri-state | (user setting) | Use raw frames as VME reference. ON = raw input, OFF = reconstructed. |
| `ConstQP` | numeric | (driver default) | Force constant QP mode, bypassing BRC. |
| `GlobalSearch` | numeric | 1 (long) | Global motion search scope. 0=default, 1=long, 2=medium, 3=short. |
| `LocalSearch` | numeric | 6 (exhaustive) | Local motion search pattern. Values 0-8. 6=exhaustive is the most thorough. |
| `EarlySkip` | numeric | 0 (auto) | Early skip decision control. 0=let driver choose, 1=enabled, 2=disabled. |
| `LaScaleFactor` | numeric | (driver default) | LookAhead scale factor. 0=auto, 1=1x, 2=2x, 4=4x. Deprecated for legacy H.264. |
| `IBC` | tri-state | ON | Intra Block Copy for screen content coding. |
| `Palette` | tri-state | ON | Palette mode for screen content coding. |
| `StrengthN` | numeric | (driver default) | Encoding strength level = StrengthN / 100.0. |
| `FractionalQP` | numeric | 1 (enabled) | Enable fractional QP values. 0=disabled, 1=enabled. |
| `NumActiveRefP` | numeric | (driver default) | Number of active references for P-frames. |
| `NumActiveRefBL0` | numeric | (driver default) | Number of active references for B-frames (L0). |
| `NumActiveRefBL1` | numeric | (driver default) | Number of active references for B-frames (L1). |
| `DisablePSubMBPartition` | tri-state | OFF | Disable sub-macroblock partitions for P-frames. OFF = allow all partitions (better quality). |
| `DisableBSubMBPartition` | tri-state | OFF | Disable sub-macroblock partitions for B-frames. OFF = allow all partitions (better quality). |
| `WeightedBiPredIdc` | numeric | 2 (implicit) | Weighted bi-prediction mode. 0=OFF, 1=explicit (unsupported), 2=implicit. |
| `DirectSpatialMvPredFlag` | tri-state | ON | Direct mode MV prediction type. ON=spatial, OFF=temporal. |
| `Transform8x8Mode` | tri-state | ON | Enable 8x8 transform mode (improves quality for HD content). |
| `LongStartCodes` | tri-state | (driver default) | Use long start codes for all NAL units. |
| `CabacInitIdcPlus1` | numeric | (driver default) | CABAC initialization table. 0=default, 1=cabac_init_idc=0, up to 3. |
| `QpUpdateRange` | numeric | (driver default) | QP adjustment range for BRC. |
| `RegressionWindow` | numeric | (driver default) | Regression analysis window size for BRC. |
| `LookAheadDependency` | numeric | (driver default) | Lookahead dependency depth (must be less than LookAhead depth). |
| `Hme` | tri-state | ON | Hierarchical Motion Estimation. Uses multi-level search for better accuracy. |
| `WriteIVFHeaders` | tri-state | OFF | Write IVF container headers. OFF = normal bitstream output. |
| `RefreshFrameContext` | tri-state | ON | Refresh encoder frame context for improved quality. |
| `ChangeFrameContextIdxForTS` | tri-state | ON | Change frame context index for temporal scalability. |
| `SuperFrameForTS` | tri-state | ON | Super-frame mode for temporal scalability. |
| `QpAdjust` | tri-state | ON | Enable SPS-level QP adjustment. |
| `TMVP` | tri-state | ON | Temporal Motion Vector Prediction. Improves compression efficiency. |
| `DDI.IntraPredBlockSize` | numeric | 1 (4x4) | DDI-level intra prediction block size. 1=4x4, 2=8x8, 4=16x16, 8=PCM. |
| `DDI.InterPredBlockSize` | numeric | 64 (4x4) | DDI-level inter prediction block size. 1=16x16, 2=16x8, 4=8x16, ... 64=4x4. |

---

## Value Types

| Type | Acceptable Values |
|------|-------------------|
| **tri-state** | `ON`, `OFF`, `UNKNOWN` (UNKNOWN = let driver choose default) |
| **numeric** | Any integer value within the field's valid range |
| **numeric (mask)** | Integer representing a bitmask; combine values by adding them |
| **numeric (U8)** | Integer in range 0-255 |

---

## Notes

- Custom options **override** the corresponding values set by the plugin's normal configuration GUI.
- Fields for a buffer that is not currently enabled (e.g., CODDI for AV1) will be silently ignored with a warning log.
- Incorrect values may cause encoder failures or degraded quality. Use with caution.
- The plugin logs all applied custom options at `info` level. Check the OBS log to verify your settings.
- CO fields marked **"Reserved. Must be 0."** exist for backward compatibility but should not be set. Use their CODDI equivalents instead.