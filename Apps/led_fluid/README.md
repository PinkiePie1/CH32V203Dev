# led矩阵+流体模拟

使用16*15的LED矩阵显示流体模拟的结果，CH32V203主控，LIS2/3传感加速度。

PB0-16接查理复用矩阵，加速度传感器接法见对应初始化函数

## 显示休眠与摇晃唤醒

设备静置不动约 **15 秒**（相邻 IMU 采样变化量 `|Δx|+|Δy|+|Δz|` 连续低于 `STILL_DELTA_THRESH`）会 **熄灭点阵**（`LED_DisplayStop` 把扫描缓冲清零，所有引脚转输入态；TIM1/DMA 扫描保持运行，仅关 TIM1 中断），MCU 与 LIS2DH 仍供电；**剧烈摇晃**（单次变化量 ≥ `SHAKE_DELTA_THRESH`）恢复显示并继续仿真。PA0 长按仍为 **整机** `shutdown()`（STOP + 复位），与显示休眠无关。

可调参数见 [`main.c`](main.c)：`IDLE_TIMEOUT_MS`、`STILL_DELTA_THRESH`、`SHAKE_DELTA_THRESH`、`SLEEP_POLL_MS`。标定时可通过串口（PA9，115200）观察 `delta=… idle_ms=…`（运行中每 20 帧一条）、`display sleep` / `wake` 日志；打印间隔见 `IMU_DELTA_LOG_EVERY`。

文件说明：
- `SandSim.c`:流体仿真主程序 
- `SandSim.h`:流体仿真参数与头文件
- `LIS2DH.c`，`LIS2DH.h`: 读取加速度计的驱动，和LIS3系列可通用
- `charlie.c`,`charlie.h`:查理复用驱动LED点阵的程序，可根据实际使用引脚数修改
- `genLUT.py`:生成n×n-1的查理复用点阵的LUT的脚本
