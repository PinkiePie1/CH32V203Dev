# led矩阵+流体模拟

和其他流体模拟一样，PCB线缆排布有些区别。

文件说明：
- `SandSim.c`:流体仿真主程序 
- `SandSim.h`:流体仿真参数与头文件
- `LIS2DH.c`，`LIS2DH.h`: 读取加速度计的驱动
- `charlie.c`,`charlie.h`:查理复用驱动LED点阵的程序
- `scripts`文件夹：用于生成点阵所需要的LUT。将网表导出后对应部分粘贴到`netlist.txt`中并运行`genLUT.py`即可生成结果打印到命令行中，随后可将对应的数组粘贴到`charlie.c`。若接线有修改需要修改`genLUT.py`中的字典NET_TO_PB。

