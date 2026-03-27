# led矩阵+流体模拟

与上一个流体模拟一样，主要期望把硬件缩小。采用0402LED加x形排布。

加速度计采用HXY的替代版本，静止睡眠，晃动唤醒。但该加速度计有问题，零偏大的夸张，功能也无法完全替换，下一版不用这个了。

文件说明：
- `SandSim.c`:流体仿真主程序 
- `SandSim.h`:流体仿真参数与头文件
- `LIS2DWHXY.c`，`LIS2DWHXY.h`: 读取加速度计的驱动，适用于HXY版的LIS2DW，注意不可与原厂互换。
- `charlie.c`,`charlie.h`:查理复用驱动LED点阵的程序
- `scripts`文件夹：用于生成点阵所需要的LUT。将网表导出后对应部分粘贴到`netlist.txt`中并运行`genLUT.py`即可生成结果打印到命令行中，随后可将对应的数组粘贴到`charlie.c`。若接线有修改需要修改`genLUT.py`中的字典NET_TO_PB。
