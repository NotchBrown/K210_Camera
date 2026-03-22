# Camera settings (available APIs)

下面列出 Sipeed OV2640（库：Sipeed_OV2640 / Camera 基类）中可用的设置项、对应函数、参数类型与范围说明。表格以工程中可调用的公有方法为主，并补充了内部/私有函数（可考虑对外暴露）的参数范围说明。

- 参考：Sipeed_OV2640 公有方法实现（库实现中有详细寄存器操作与取值约束）

| 设置项 (中文) | 接口/函数 (`C++`) | 参数类型 | 参数值 / 范围 | 备注 |
|---|---:|---:|---:|---|
| 像素格式 | `setPixFormat(pixformat_t pixFormat)` | `pixformat_t` (enum) | 常量：`PIXFORMAT_BAYER`, `PIXFORMAT_RGB565`, `PIXFORMAT_YUV422`, `PIXFORMAT_GRAYSCALE`, `PIXFORMAT_JPEG`。<br>但 `Sipeed_OV2640` 构造/初始化在 K210 平台上断言只支持 `PIXFORMAT_RGB565` 或 `PIXFORMAT_YUV422`。 | 公有，调用映射到 `ov2640_set_pixformat`，并设置 DVP 格式。 |
| 分辨率 / 捕获尺寸 | `setFrameSize(framesize_t frameSize)` | `framesize_t` (enum) | 枚举值大量：`FRAMESIZE_QQVGA(160x120)`, `FRAMESIZE_QVGA(320x240)`, `FRAMESIZE_VGA(640x480)`, `FRAMESIZE_QQQVGA`, `FRAMESIZE_HQVGA` 等（详见 Camera.h 的 `framesize_t` 列表）。 | 公有，映射到 `ov2640_set_framesize`，会写寄存器并调用 `dvp_set_image_size`。 |
| 预览帧率（请求） | `setFrameRate(framerate_t framerate)` | `framerate_t` (enum) | `FRAMERATE_2FPS`, `FRAMERATE_8FPS`, `FRAMERATE_15FPS`, `FRAMERATE_30FPS`, `FRAMERATE_60FPS`。 | 库内 `ov2640_set_framerate` 在当前实现中为空（返回0），所以对真实硬件效果可能为 no-op；但接口存在。 |
| 增益上限（AGC ceiling） | `setGainCeiling(gainceiling_t gainceiling)` | `gainceiling_t` (enum) | `GAINCEILING_2X`, `4X`, `8X`, `16X`, `32X`, `64X`, `128X` | 公有，映射到 `ov2640_set_gainceiling`，写 COM9 寄存器。 |
| 自动增益 (AGC) | `setAutoGain(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_auto_gain(enable, 0.0f, 0.0f)`。私有更高级接口可设置静态增益或增益上限（gain_db, gain_db_ceiling）。 |
| 自动曝光 (AEC) | `setAutoExposure(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_auto_exposure(enable, 0)`。私有接口允许在 `enable==0` 时指定 `exposure_us`（微秒）。 |
| 曝光时间（私有/可扩展） | `ov2640_set_auto_exposure(int enable, int exposure_us)` (private) | `int enable`, `int exposure_us` | `exposure_us` 整数，库内部会 clamp 到 0..0xFFFF 的曝光计数并按像素时钟换算为微秒 | 若要固定曝光，需要调用或对外封装此私有函数。 |
| 自动白平衡 (AWB) | `setAutoWhiteBalance(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_auto_whitebal(enable, 0,0,0)`。私有函数可在 `enable==0` 时指定 `r/g/b` 增益（dB）。 |
| AWB 红绿蓝增益 (私有) | `ov2640_set_auto_whitebal(int enable, float r_gain_db, float g_gain_db, float b_gain_db)` | `float` 三通道 (dB) | 任意浮点（实现检查 NaN/Inf），用于在关闭 AWB 时指定通道增益 | 私有，库实现写寄存器但对自动/手动切换逻辑有限制。 |
| 亮度 (Brightness) | `setBrightnessLevel(int level)` | `int` | 有效值：`-2` .. `+2`（函数会 clamp 到此范围） | 公有，映射到 `ov2640_set_brightness`，写 DSP 的 brightness 寄存器组。 |
| 对比度 (Contrast) | `ov2640_set_contrast(int level)` (private) | `int` | 有效值库中 `-2` .. `+2`（实现通过数组索引） | 私有，可考虑对外封装。 |
| 饱和度 (Saturation) | `ov2640_set_saturation(int level)` (private) | `int` | 有效值 `-2` .. `+2` | 私有。 |
| 色条测试模式 (Color bar) | `setColorBar(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_colorbar`，修改 COM7 的 COLOR_BAR 位。 |
| 镜像水平 (H mirror) | `setHMirror(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_hmirror`（写 REG04 的 HFLIP 位）。 |
| 翻转垂直 (V flip) | `setVFlip(bool enable)` | `bool` | `true` / `false` | 公有，映射到 `ov2640_set_vflip`（写 REG04 的 VFLIP 与 VREF）。 |
| 旋转 (rotation) | `setRotaion(uint8_t rotation)` | `uint8_t` | 未实现（函数体为 FIXME） | 公有接口存在但无实际实现。 |
| 反相 / invert | `setInvert(bool invert)` | `bool` | `true` / `false` | 在实现中被映射为 `ov2640_set_hmirror(!invert)`（语义奇怪：invert -> 取反并调用水平镜像），需要修正或明确文档。 |
| JPEG / 图像质量 (私有) | `ov2640_set_quality(int qs)` (private) | `int` | 未在头文件中定义具体范围，QS 寄存器通常 0..255 | 私有。 |
| AGC/Gain 查询 | `ov2640_get_gain_db(float* gain_db)` (private) | `float*` 输出 | 返回以 dB 表示的当前总体增益 | 私有，只读接口。 |
| 曝光查询 | `ov2640_get_exposure_us(int* exposure_us)` (private) | `int*` 输出（微秒） | 读取并计算当前曝光（us） | 私有。 |

## 额外说明与建议（简短）

- UI 设计应区分“公有且立即生效”的设置与“私有/可选暴露”的设置。优先在 UI 中展示并操作上表中带有"公有"标记的接口（例如：分辨率、AGC、AEC、AWB、亮度、镜像、色条）。
- 若需要更精细控制（例如手动曝光时间、手动 RGB 增益、对比/饱和度微调），可以考虑把对应的私有函数封装为 app 层 API（例如在 `app_manager` 中增加 `app_manager_set_contrast()` 等）。
- 注意库中部分函数是部分实现或 no-op（例如 `ov2640_set_framerate` 目前为空，`setRotaion` 未实现，`setInvert` 的映射语义需要修正），在 UI 上应屏蔽或以灰显/提示方式标注“未实现/无效”。

如果你确认需要，我可以：
- 把 `cam_set_ava.md` 中的私有函数项进一步展开（包含实现细节与寄存器说明），或者
- 直接在 `app_manager` 中添加小型封装函数（如 `app_manager_set_contrast`）并把 UI 中对应开关/滚轮绑定到这些封装。 

请选择下一步（例如“只生成文档”或“生成并提交小补丁暴露 contrast/brightness”）。