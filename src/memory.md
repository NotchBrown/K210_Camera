# memory

- RTOS 已改为默认常开，无 APP_ENABLE_RTOS 开关。
- 显示刷新路径使用 ST7789 DMA 发送，且启用 LVGL 双缓冲。
- 目录重构目标：driver 放硬件抽象，lvgl 放显示/触摸/运行时，ui 放界面，fun 放业务功能。
- 串口调试统一使用 APP_LOG 宏，方便 release 时降低日志级别。
- 通过临时注入 PATH(`C:\Users\Lenovo\.platformio\penv\Scripts`)恢复了 platformio 命令，并完成编译/上传。
- 已参考 `D:\K210\kendryte-freertos-demo\lcd\lcd.c` 的 DMA 刷屏模式：继续使用 `tft_write_*` DMA 路径。
- 当前刷屏从“额外 DMA 缓冲拷贝”改为“LVGL flush 缓冲原地字节序处理 + DMA 发送”，降低内存带宽开销。
- 已增加屏幕调试叠层：显示 heap、flush/s、last flush us。

# require
1. 不要开关
2. 把所有裸机实现都改为RTOS
3. 请尝试DMA屏幕，屏幕很卡现在，另外也尝试显示LVGL Debug信息，渲染请用双缓冲，这个MCU内存很大
4. 你似乎没有真正地打开RTOS，因为我没找到#define APP_ENABLE_RTOS
5. 请你把所有的硬件相关的放在/src/driver,lvgl相关的如刷屏实现、触摸接口放在/src/lvgl，你要整理，还有/src/ui放置gui，还有/src/fun放置子功能
6. 目标：编译上传成功
7. 其余：串口调试信息请改成MACRO，不然不方便release
请你思考，然后完成，切不可偷懒，用/src/memory.md存取你的记忆，千万不要遗漏
之前你做的任务很好，但是你也偷懒了，偷懒是该死的