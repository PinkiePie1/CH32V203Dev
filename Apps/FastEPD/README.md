# FastEPD for CH32V203

这是把 `V303FastEPD/303fastEPD` 的 **`epd_fast_net.c`** SSD1680 152x152
高刷驱动移植到 CH32V203（20 KB RAM，64 KB Flash）后的版本。

本移植保留 net 算法的核心行为：每像素维护一个有符号净驱动量，目标黑时向
正方向驱动，目标白时向负方向驱动，并用 `EPD_DC_BLACK/EPD_DC_WHITE` 和
`EPD_FAST_FRAMES` 限制每帧的驱动量。原来的 `epd_fast.c` 逻辑已被替换。

## 硬件连接

驱动中的引脚集中在 `epd_fast_net.c` 顶部的「移植区」。针对 CH32V203C8T6（LQFP48）的默认接线如下：

| EPD 引脚 | CH32V203 引脚 | 说明 |
|---|---|---|
| CS   | PA4 | 软件片选 |
| DC   | PA3 | 命令/数据 |
| SCK  | PA5 | SPI1 SCK |
| MOSI | PA7 | SPI1 MOSI |
| RST  | PA6 | 复位 |
| BUSY | PA0 | 输入下拉，EXTI0 |

SPI1 使用 144 MHz / 8 = 18 MHz，满足 SSD1680 小于 20 MHz 的要求。
如果实际开发板引脚不同，只需修改 `epd_fast_net.c` 顶部这一组宏。

## 编译与烧录

```bash
cd /home/anon/repos/v203/Apps/FastEPD
make -j
make flash        # 需要 minichlink 和已连接的开发板
```

构建产物在 `objs/FastEPD.bin` / `FastEPD.hex`。串口打印使用 USART1
（PA9，115200 baud），已在 Makefile 中定义 `DEBUG=1`。

## RAM 优化

原 `epd_fast_net.c` 需要的主要静态内存约为：

- `s_net`：11552 B（每像素 4 bit 有符号净驱动量）
- 两张整屏掩码：2 x 2888 B
- demo 帧：2888 B

合计约 20.2 KB，再加上栈和全局变量后超过 V203 的 20 KB RAM。
本移植做了这些优化：

1. 保留 net 算法需要的每像素 4 bit 有符号净驱动量。
2. 不再保存两张整屏掩码，而是在发送时根据 `s_net + next 帧` 实时生成。
3. 使用两块 512 B 掩码缓冲做 DMA 双缓冲流水：SPI/DMA 发送 A 块时
   CPU 生成 B 块，发送 B 块时 CPU 生成 A 块；块间只等待 DMA TC，
   不等待 SPI BSY。
4. 面板刷完后根据相同的目标/净驱动条件更新 `s_net`，不再需要掩码数组。
5. 保留一帧 demo 图像缓冲。

当前构建结果（包含 1024 B 栈）：

```
RAM: 16720 B / 20 KB (约 81.6%)
FLASH: 22708 B / 64 KB (约 34.6%)
```

静态 RAM 约占 15.3 KB，剩余约 3.7 KB 给栈和后续应用使用。

## 与旧驱动的接口差异

- 快刷算法来自 `epd_fast_net.c`，不再保存上一帧目标。
- `EPD_FastBegin(shown)` 仍然保留接口，但 net 算法会忽略 `shown`，只清空净驱动状态。
- `EPD_SetPulseBudget()` 的绝对值上限为 7，与 4 bit 有符号 nibble 的表示范围匹配。
- 其余公开 API、图片格式和刷新流程保持不变。
