# FastEPD for CH32V203

net 算法的核心行为：每像素维护一个有符号净驱动量，目标黑时向正方向驱动，目标白时向负方向驱动，并用 `EPD_DC_BLACK/EPD_DC_WHITE` 和
`EPD_FAST_FRAMES` 限制驱动量。

## 硬件连接

CH32V203C8T6的接线如下：

| EPD | CH32V203 | 
|---|---|
| CS   | PA4 | 
| DC   | PA3 | 
| SCK  | PA5 | 
| MOSI | PA7 | 
| RST  | PA6 | 
| BUSY | PA0 | 

