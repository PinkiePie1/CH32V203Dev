/* ============================================================================
 * SSD1680 152x152 电子纸高刷驱动（epd_fast_net.c 算法）—— CH32V203
 *
 * 功能逻辑来自 V303 工程的 epd_fast_net.c；V203 移植部分保留本工程的
 * 20 KB RAM 优化、DMA 双缓冲和引脚/时钟改动。
 *
 * 换单片机时只需要改两处：
 *   1. 下面的 #include（CH32V203 SDK 头文件）
 *   2. 紧跟其后的「移植区」：引脚宏、SPI/DMA 参数、时基、收发原语、EPD_Hal_Init()
 * 移植区之后是纯驱动逻辑，和具体单片机无关。
 * ==========================================================================*/

#include "ch32v20x_conf.h"
#include "debug.h"
#include "epd_fast.h"
#include "print.h"

#include <string.h>

/* 调试打印走 USART1(PA9)，需要先调用 USART_Printf_Init()。 */


/* ============================================================================
 * ============================ 移植区（开始） ================================
 *
 * 换一颗单片机要重写的就是这一段：
 *   - 引脚宏 CS/DC/RST/BUSY
 *   - SPI 时钟分频、DMA 通道与传输完成标志
 *   - 纯软件延时：EPD_DelayUs / EPD_DelayMs
 *   - 收发原语：spi_byte / EPD_Cmd / EPD_Dat / epd_dma_send / epd_dat_buf
 *   - EPD_Hal_Init()：时钟、GPIO、SPI、DMA 的初始化
 * ==========================================================================*/

/* ---------------------------------------------------------------- 引脚
 *
 * CH32V203C8T6（LQFP48）没有 PC4，BUSY 改到 PA0：
 *
 *   PA0  BUSY（输入下拉，EXTI0 用来唤醒 WFI）
 *   PA3  DC        PA4  CS         PA5  SPI1 SCK（默认复用）
 *   PA6  RST       PA7  SPI1 MOSI（默认复用）
 *
 * 这样 EPD 的 7 根线占 PA0、PA3..PA7，全部位于 LQFP48 的相邻引脚上。
 */
#define EPD_CS_PORT     GPIOA
#define EPD_CS_PIN      GPIO_Pin_4
#define EPD_DC_PORT     GPIOA
#define EPD_DC_PIN      GPIO_Pin_3
#define EPD_RST_PORT    GPIOA
#define EPD_RST_PIN     GPIO_Pin_6
#define EPD_BUSY_PORT   GPIOA
#define EPD_BUSY_PIN    GPIO_Pin_0

/* BUSY 引脚接到哪根 EXTI / 哪个中断号（WFI 靠它唤醒） */
#define EPD_BUSY_EXTI_LINE  EXTI_Line0
#define EPD_BUSY_IRQn       EXTI0_IRQn      // CH32V203 的 EXTI 每根线一个中断
#define EPD_BUSY_EXTI_PORT  GPIO_PortSourceGPIOA
#define EPD_BUSY_EXTI_PIN   GPIO_PinSource0

#define CS_H()   GPIO_SetBits(EPD_CS_PORT, EPD_CS_PIN)
#define CS_L()   GPIO_ResetBits(EPD_CS_PORT, EPD_CS_PIN)
#define DC_H()   GPIO_SetBits(EPD_DC_PORT, EPD_DC_PIN)
#define DC_L()   GPIO_ResetBits(EPD_DC_PORT, EPD_DC_PIN)
#define RST_H()  GPIO_SetBits(EPD_RST_PORT, EPD_RST_PIN)
#define RST_L()  GPIO_ResetBits(EPD_RST_PORT, EPD_RST_PIN)
#define BUSY()   ((GPIO_ReadInputDataBit(EPD_BUSY_PORT, EPD_BUSY_PIN) != 0) ? 1u : 0u)

/* --------------------------------------------------------- SPI / DMA 参数 */

#define EPD_SPI_PRESCALER   SPI_BaudRatePrescaler_4
#define EPD_GPIO_SPEED      GPIO_Speed_50MHz

// DMA设置。bulk传输走dma最快，比轮询fifo更快。
// 移植到其他平台的话这些宏可以删掉，重新实现epd_dma_send即可。
// 不想实现直接在该函数里循环EPD_Dat()也行。
#define EPD_DMA_CHANNEL     DMA1_Channel3                     // SPI1_TX 固定走 DMA1 通道3
#define EPD_DMA_TC_FLAG     DMA1_FLAG_TC3
#define EPD_DMA_TC_READY()  (DMA_GetFlagStatus(EPD_DMA_TC_FLAG) != RESET)
#define EPD_DMA_TC_CLEAR()  DMA_ClearFlag(EPD_DMA_TC_FLAG)
#define EPD_DMA_MIN_LEN     256                               // 超过这个长度才走 DMA

/* 软件延时：144 MHz 下约 27 个空循环 / us。 */
#define EPD_DELAY_LOOPS_PER_MS  27000u
#define EPD_DELAY_LOOPS_PER_US  (EPD_DELAY_LOOPS_PER_MS / 1000u)

void EPD_DelayUs(uint32_t us)
{
    uint32_t loops = us * EPD_DELAY_LOOPS_PER_US;

    while (loops != 0u) {
        --loops;
        __asm__ volatile ("nop");
    }
}

void EPD_DelayMs(uint32_t ms)
{
    uint32_t loops = ms * EPD_DELAY_LOOPS_PER_MS;

    while (loops != 0u) {
        --loops;
        __asm__ volatile ("nop");
    }
}

/* ----------------------------SPI收发--------------------------------  */

/* 单字节发送 */
static void spi_byte(uint8_t v)
{
    uint32_t guard = 0;

    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
        if (++guard > 200000u) {
            return;
        }
    }
    SPI_I2S_SendData(SPI1, v);

    guard = 0;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) {
        if (++guard > 200000u) {
            return;
        }
    }
    (void)SPI_I2S_ReceiveData(SPI1);
}

/* 命令 */
void EPD_Cmd(uint8_t cmd)
{
    DC_L();
    CS_L();
    spi_byte(cmd);
    CS_H();
}

/* 数据 */
void EPD_Dat(uint8_t dat)
{
    DC_H();
    CS_L();
    spi_byte(dat);
    CS_H();
}

/* 启动一次 SPI TX DMA，不等待完成。用于平面数据的双缓冲流水发送。 */
static void epd_dma_start(const uint8_t *buf, uint32_t len)
{
    DMA_Cmd(EPD_DMA_CHANNEL, DISABLE);
    EPD_DMA_TC_CLEAR();
    EPD_DMA_CHANNEL->MADDR = (uint32_t)buf;
    DMA_SetCurrDataCounter(EPD_DMA_CHANNEL, (uint16_t)len);
    /* 保证 CPU 对缓冲区的写入在 DMA 使能前已经完成。 */
    __asm__ volatile ("" ::: "memory");
    DMA_Cmd(EPD_DMA_CHANNEL, ENABLE);
}

/* 等当前 DMA 块传完。这里只等 DMA TC，不等 SPI BSY：立刻启动下一块，
 * 让 SPI 的移位寄存器始终保持有数据可发。 */
static void epd_dma_wait_tc(const uint8_t *buf, uint32_t len)
{
    uint32_t guard = 2000000u;

    while (!EPD_DMA_TC_READY()) {
        if (--guard == 0u) {
            /* DMA 没跑起来就退回轮询，把没发完的部分补上 */
            const uint32_t left = DMA_GetCurrDataCounter(EPD_DMA_CHANNEL);
            const uint32_t done = (left <= len) ? (len - left) : len;

            DMA_Cmd(EPD_DMA_CHANNEL, DISABLE);
            for (uint32_t i = done; i < len; ++i) {
                spi_byte(buf[i]);
            }
            PRINT("[EPD] dma timeout at %u/%u, fell back to polling\r\n",
                  (unsigned)done, (unsigned)len);
            return;
        }
    }
}

/* 一整张平面发完后，才等待移位寄存器清空并清理 DMA/SPI 状态。 */
static void epd_spi_wait_idle(void)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) {
    }
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET) {
    }

    DMA_Cmd(EPD_DMA_CHANNEL, DISABLE);
    EPD_DMA_TC_CLEAR();

    /* SPI传完要读一下接收 */
    (void)SPI1->DATAR;
    (void)SPI1->STATR;
}

/* 阻塞式 DMA 发送，供非流水数据（LUT、普通 EPD_FastScan、整帧全刷）使用。 */
static void epd_dma_send(const uint8_t *buf, uint32_t len)
{
    epd_dma_start(buf, len);
    epd_dma_wait_tc(buf, len);
    epd_spi_wait_idle();
}

/* 数据阶段的原始发送：不碰 DC/CS，供单块和多块连续传输共用。 */
static void epd_send_data_raw(const uint8_t *buf, uint32_t len)
{
    if (len >= EPD_DMA_MIN_LEN) {
        epd_dma_send(buf, len);
    } else {
        for (uint32_t i = 0; i < len; ++i) {
            spi_byte(buf[i]);
        }
    }
}

/* 数据阶段（CS 保持低电平）：长块走 DMA，短块逐字节 */
static void epd_dat_buf(const uint8_t *buf, uint32_t len)
{
    DC_H();
    CS_L();
    epd_send_data_raw(buf, len);
    CS_H();
}

/* ------------------------------------------------------------ 硬件初始化 */

static void epd_build_mask_lut(void);

void EPD_Hal_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SPI_InitTypeDef spi = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);

    /* CS / DC / RST 输出 */
    gpio.GPIO_Pin = EPD_CS_PIN | EPD_DC_PIN | EPD_RST_PIN;
    gpio.GPIO_Speed = EPD_GPIO_SPEED;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(EPD_CS_PORT, &gpio);

    /* BUSY 输入下拉 */
    gpio.GPIO_Pin = EPD_BUSY_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_Init(EPD_BUSY_PORT, &gpio);

    /* BUSY中断配置*/
    GPIO_EXTILineConfig(EPD_BUSY_EXTI_PORT, EPD_BUSY_EXTI_PIN);
    {
        EXTI_InitTypeDef exti = {0};

        exti.EXTI_Line = EPD_BUSY_EXTI_LINE;
        exti.EXTI_Mode = EXTI_Mode_Interrupt;
        exti.EXTI_Trigger = EXTI_Trigger_Falling;
        exti.EXTI_LineCmd = ENABLE;
        EXTI_Init(&exti);
    }
    NVIC_SetPriority(EPD_BUSY_IRQn, 1);
    NVIC_EnableIRQ(EPD_BUSY_IRQn);

    /* SPI1 的 SCK/MOSI 在 PA5/PA7，复位后默认就映射在这里，确认没开重映射 */
    GPIO_PinRemapConfig(GPIO_Remap_SPI1, DISABLE);

    gpio.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    CS_H();
    DC_H();
    RST_H();

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = EPD_SPI_PRESCALER;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &spi);
    SPI_Cmd(SPI1, ENABLE);

    /* DMA：DMA1 通道3 固定映射到 SPI1_TX，无需 DMAMUX */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    {
        DMA_InitTypeDef dma = {0};

        DMA_DeInit(EPD_DMA_CHANNEL);
        dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DATAR;
        dma.DMA_MemoryBaseAddr = 0;
        dma.DMA_DIR = DMA_DIR_PeripheralDST;
        dma.DMA_BufferSize = 0;
        dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
        dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
        dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
        dma.DMA_Mode = DMA_Mode_Normal;
        dma.DMA_Priority = DMA_Priority_VeryHigh;
        dma.DMA_M2M = DMA_M2M_Disable;
        DMA_Init(EPD_DMA_CHANNEL, &dma);
    }
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

    /* 上电时按当前预算生成掩码/驱动判定查表。 */
    epd_build_mask_lut();
}

/* BUSY 引脚的 EXTI 中断：只清标志，作用是让 epd_wait() 里的 WFI 醒过来。
 * 真正的判断还在 epd_wait() 里读引脚，所以这里不做任何业务逻辑。 */
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EPD_BUSY_EXTI_LINE) != RESET) {
        EXTI_ClearITPendingBit(EPD_BUSY_EXTI_LINE);
    }
}

/* ============================================================================
 * ============================= 移植区（结束） ===============================
 * ==========================================================================*/


/* ============================================================================
 * 墨水屏驱动参数：
 * ==========================================================================*/

/* 局部窗口开关：
 *   1 = 只把有变化的行推给面板（省传输，默认）
 *   0 = 每次都写整屏 152 行 */
#define EPD_PARTIAL_WINDOW  0

/* 每像素净驱动帧数限制：黑方向不超过 +7，白方向不超过 -7。 */
#define EPD_DC_BLACK        7
#define EPD_DC_WHITE        7
#define EPD_FAST_FRAMES     1      // 每次快刷扫描用几个控制器帧
#define EPD_RESET_HIGH_MS   50     // 保持 RST 高电平的时间 (ms)
#define EPD_RESET_LOW_MS    20     // 拉低 RST 的时间 (ms)
#define EPD_CLEAN_INTERVAL  10000   // 每多少次快刷做一次全刷清洁；0 = 关闭
#define EPD_HOLD_ENDING     1      // 1 = 最后一帧驱动完后补一次全0电平扫描放电。

#define EPD_PIXELS          (EPD_W * EPD_H)   /* 23104 */

/* ------------------------------------------------------------ waveforms */

/* Full refresh waveform taken from the user's CH572 driver, with the source
 * voltage fields flipped back to the polarity this driver uses. The flip is a
 * swap of the two bit fields 01 (VSH1) and 10 (VSL) in every VS field of the
 * four level rows, see Table 6-6 of the SSD1680 datasheet:
 *
 *   b = ((b & 0x55) << 1) | ((b & 0xAA) >> 1)
 *
 * 00 (VSS, no drive) and 11 (VSH2) are left alone, so untouched phases stay
 * untouched. Its stronger VSH1/VCOM (0x4A / 0x36) is what makes it fast.
 * Layout: 153 byte LUT, then 0x3F/0x03/0x04/0x2C take bytes 153..158. */
 /*
static const uint8_t lut_full[159] = {
    0x80,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,
    0x40,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,
    0x90,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,
    0x40,0x99,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0b,0x01,0x01,0x00,0x00,0x00,0x00,0x05,0x05,0x00,0x03,0x03,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0b,0x01,0x01,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x33,0x44,0x44,0x45,0x43,0x44,0x00,0x00,0x00,0x22,0x17,0x4a,
    0x00,0x38,0x36,
};
*/
static const uint8_t lut_full[159] =
{
0x80,	0x48,	0x40,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x40,	0x48,	0x80,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x80,	0x48,	0x40,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x40,	0x48,	0x80,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,
0x9,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x5,	0x1,	0x0,	0x5,	0x1,	0x0,	0x3,					
0x9,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x0,	0x0,	0x0,	0x0,	0x0,	0x0,	0x0,					
0x22,	0x22,	0x22,	0x22,	0x22,	0x22,	0x0,	0x0,	0x0,			
0x22,	0x17,	0x41,	0x0,	0x32,	0x20                          //EOPT VGH VSH1 VSH2 VSL VCOM
};

/* Fast waveform: the vendor partial table with the two HOLD rows silenced.
 * Level rows are  00 = hold, 01 = drive white, 10 = drive black, 11 = hold,
 * so a pixel that is in neither mask keeps its current colour. */
static const uint8_t lut_fast_base[159] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 00  LUT0  */
    0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 01 white LUT1 */
    0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 10 black LUT2 */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 11 hold  LUT3 */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //LUT4
    0x00,0x00,0x00,0x00,0x00,0x00,0x00, 
    0x01,0x00,0x00,0x00,0x00,0x00,0x00, //1A 1B SRAB 1C 1D SRCD RP
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x77,0x77,0x77,0x77,0x77,0x77,0x00,0x00,0x00,
    0x02,0x17,0x4B,0xb0,0x3A,0x28,//EOPT VGH VSH1 VSH2 VSL VCOM
};

/* --------------------------------------------------------------- state */


static uint8_t analog_on;          /* the booster is up and kept up */
static uint8_t s_dc_black = EPD_DC_BLACK;
static uint8_t s_dc_white = EPD_DC_WHITE;
static uint8_t s_fast_frames = EPD_FAST_FRAMES;

/* 每像素净驱动帧数：正数 = 黑方向，负数 = 白方向，限制在 [-7,+7]。
 * 4 bit 有符号补码（-8..+7），两个像素共用 1 字节。 */
#define EPD_NET_BYTES    (EPD_PIXELS / 2u)
#define EPD_MAX_PULSE    7u

/* 后面会按 32-bit 读取 4 个 net 字节，保证 4 字节对齐。 */
static uint8_t s_net[EPD_NET_BYTES] __attribute__((aligned(4)));

/* 掩码双缓冲改为 2 x 64 字节。 */
#define EPD_PLANE_CHUNK   64u
static uint8_t s_plane_buf[2][EPD_PLANE_CHUNK] __attribute__((aligned(4)));

/* 上电时生成：[plane][net_pair_byte] -> 两个像素是否达到本方向驱动阈值。
 * 返回值的 bit0/bit1 分别对应 net 字节里的低/高 nibble。
 * plane 0 = 黑方向，plane 1 = 白方向。 */
static uint8_t s_mask_lut[2][256];

/* 检测“本帧是否存在需要驱动的像素”时使用的循环探测位置（帧字节下标）。 */
static uint32_t s_probe_pos;

static uint32_t s_scan_count;   /* fast scans since the last full refresh */
static uint8_t  s_pending;      /* 已经激活、面板还在刷 */
static uint8_t  s_row0, s_row1; /* 本帧要发的行范围（局部刷新用） */
static uint8_t  s_col0, s_col1; /* 本帧要发的列范围（字节列 0..18） */
static uint8_t s_hold_pending;  /* a scan drove something and still needs its HOLD ending */

/* 4 bit 有符号 nibble 转 int（查表和掩码计算共用）。 */
static inline int epd_signed_nibble(uint8_t v)
{
    v &= 0x0Fu;
    return (v & 0x08u) ? ((int)v - 16) : (int)v;
}

/* 上电/预算变化时重建查表。 */
static void epd_build_mask_lut(void)
{
    const int hi_black = (int)s_dc_black - (int)s_fast_frames;
    const int lo_white = -(int)s_dc_white + (int)s_fast_frames;

    for (uint16_t v = 0; v < 256u; ++v) {
        const int n0 = epd_signed_nibble((uint8_t)v);
        const int n1 = epd_signed_nibble((uint8_t)(v >> 4));
        uint8_t black_ok = 0;
        uint8_t white_ok = 0;

        if (n0 <= hi_black) {
            black_ok |= 0x01u;
        }
        if (n1 <= hi_black) {
            black_ok |= 0x02u;
        }
        if (n0 >= lo_white) {
            white_ok |= 0x01u;
        }
        if (n1 >= lo_white) {
            white_ok |= 0x02u;
        }

        s_mask_lut[0][v] = black_ok;
        s_mask_lut[1][v] = white_ok;
    }
}

/* 输入 4 个 net 字节（共 8 像素），输出 8 bit 驱动条件掩码。
 * 结果还要再与目标颜色按位与，才会得到真正的黑/白平面。 */
static inline uint8_t epd_drive_mask_byte(const uint8_t *cell, uint8_t plane)
{
    const uint8_t p0 = s_mask_lut[plane][cell[0]];
    const uint8_t p1 = s_mask_lut[plane][cell[1]];
    const uint8_t p2 = s_mask_lut[plane][cell[2]];
    const uint8_t p3 = s_mask_lut[plane][cell[3]];

    return (uint8_t)(((p0 & 0x01u) << 7) | (((p0 >> 1) & 0x01u) << 6) |
                     ((p1 & 0x01u) << 5) | (((p1 >> 1) & 0x01u) << 4) |
                     ((p2 & 0x01u) << 3) | (((p2 >> 1) & 0x01u) << 2) |
                     ((p3 & 0x01u) << 1) |  ((p3 >> 1) & 0x01u));
}

/* 快速判断一个帧字节（8 像素）里是否有像素需要驱动。
 * 32-bit 全饱和跳过：
 *   - 全黑目标 0x00 + 8 个 net nibble 全为 +7 -> 不驱动
 *   - 全白目标 0xFF + 8 个 net nibble 全为 -7 -> 不驱动
 * 注意全白目标配合 +7（0x77777777）仍需要向白驱动，不能跳过。 */
static inline int epd_frame_byte_needs_drive(const uint8_t *next, uint32_t frame_index)
{
    const uint8_t px = next[frame_index];
    const uint8_t *const cell = &s_net[frame_index * 4u];
    uint32_t net_word;

    memcpy(&net_word, cell, sizeof(net_word));
    if ((px == 0x00u) && (net_word == 0x77777777u)) {
        return 0;
    }
    if ((px == 0xFFu) && (net_word == 0x99999999u)) {
        return 0;
    }

    {
        const uint8_t black = (uint8_t)(epd_drive_mask_byte(cell, 0u) & (uint8_t)~px);
        const uint8_t white = (uint8_t)(epd_drive_mask_byte(cell, 1u) & px);

        return (uint8_t)(black | white) != 0u;
    }
}

/* 清零净驱动量，并复位探测游标。 */
static void epd_net_clear(void)
{
    for (uint32_t i = 0; i < EPD_NET_BYTES; ++i) {
        s_net[i] = 0;
    }
    s_probe_pos = 0;
}

/* 全刷已经把整帧真正显示到面板上。此时把 net 直接设到对应方向的饱和值：
 *   - 目标黑：net = +dc_black
 *   - 目标白：net = -dc_white
 * 这样下一帧如果目标没有变化，就不会再向同方向重复驱动。 */
static void epd_net_set_from_image(const uint8_t *image)
{
    const uint8_t black_nibble = (uint8_t)((int)s_dc_black & 0x0F);
    const uint8_t white_nibble = (uint8_t)((-(int)s_dc_white) & 0x0F);

    for (uint32_t i = 0; i < EPD_FRAME_BYTES; ++i) {
        const uint8_t v = image[i];
        uint8_t *const cell = &s_net[i * 4u];

        for (uint8_t b = 0; b < 4u; ++b) {
            const uint8_t m0 = (uint8_t)(0x80u >> (b * 2u));
            const uint8_t m1 = (uint8_t)(0x40u >> (b * 2u));
            const uint8_t n0 = ((v & m0) != 0u) ? white_nibble : black_nibble;
            const uint8_t n1 = ((v & m1) != 0u) ? white_nibble : black_nibble;

            cell[b] = (uint8_t)(n0 | (uint8_t)(n1 << 4));
        }
    }

    s_probe_pos = 0;
}

/* ------------------------------------------------------------ 等待 BUSY
 *
 * 时基用 LSI + RTC（40 kHz 分频到 1 Hz），完全不占用 SysTick，SysTick 留给应用层。
 *
 * 等待 BUSY 时直接进 STOP 模式：
 *   - BUSY 下降沿（EXTI0）唤醒，这是正常情况
 *   - LSI 驱动的 RTC 闹钟（EXTI17）唤醒，作为面板卡死的超时保护
 *   - 唤醒后先 SystemInit() 把系统时钟恢复到 144 MHz，再重新判断 BUSY
 */

//BUSY 超时 单位：秒
#define EPD_BUSY_POLLS_LONG     4u   /* 复位 / LUT / 升压 / 全刷 */
#define EPD_BUSY_POLLS_SCAN     1u   /* 一次快刷扫描 */

/* LSI 标称 40 kHz，RTC 预分频到 2 Hz（0.5 s 一个 tick） */
#define EPD_LSI_HZ              40000u
#define EPD_RTC_HZ              2u
#define EPD_RTC_PRESCALER       ((EPD_LSI_HZ / EPD_RTC_HZ) - 1u)

/* 最近一次等待是否超时：面板卡住时上层可以做一次硬复位恢复 */
static uint8_t s_busy_timeout;

/* LSI + RTC 时基，只初始化一次 */
static void epd_rtc_ensure(void)
{
    static uint8_t ready;

    if (ready != 0u) {
        return;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);

    /* 备份域复位一次，保证 RTC 时钟源可以重新选 */
    RCC_BackupResetCmd(ENABLE);
    RCC_BackupResetCmd(DISABLE);

    RCC_LSICmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
    }

    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
    RCC_RTCCLKCmd(ENABLE);
    RTC_WaitForSynchro();
    RTC_WaitForLastTask();

    RTC_SetPrescaler(EPD_RTC_PRESCALER);
    RTC_WaitForLastTask();
    RTC_SetCounter(0);
    RTC_WaitForLastTask();

    /* RTC 闹钟经 EXTI17 唤醒 STOP */
    {
        EXTI_InitTypeDef exti = {0};

        exti.EXTI_Line = EXTI_Line17;
        exti.EXTI_Mode = EXTI_Mode_Interrupt;
        exti.EXTI_Trigger = EXTI_Trigger_Rising;
        exti.EXTI_LineCmd = ENABLE;
        EXTI_Init(&exti);
    }
    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    NVIC_SetPriority(RTCAlarm_IRQn, 1);
    NVIC_EnableIRQ(RTCAlarm_IRQn);

    ready = 1;
}

/* RTC闹钟中断函数 */
void RTCAlarm_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void RTCAlarm_IRQHandler(void)
{
    EXTI_ClearITPendingBit(EXTI_Line17);
    RTC_ClearITPendingBit(RTC_IT_ALR);
}

/* STOP 唤醒后系统时钟回到 HSI，必须重新配回 144 MHz */
static void epd_restore_clock(void)
{
    PWR->CTLR |= PWR_CTLR_CWUF;                  /* 清唤醒标志 */
    SystemInit();
    SystemCoreClockUpdate();
}

/* 低功耗等待：时基改用 LSI/RTC 后 SysTick 空闲，这里保持软件延时。 */
void EPD_SleepUs(uint32_t us)
{
    EPD_DelayUs(us);
}

/* 等 BUSY 落下：进 STOP 睡眠，BUSY 下降沿或 RTC 闹钟唤醒。
 * timeout_s 单位：秒；RTC 每 0.5 s 计一个 tick，闹钟直接用计数器值比较。 */
static void epd_wait(uint32_t timeout_s)
{
    uint32_t deadline;

    epd_rtc_ensure();

    if (!BUSY()) {
        return;
    }

    /* uint32 加法自然回绕：计数器接近 0xFFFFFFFF 时，deadline 会变成
     * 溢出后的小值，等 CNT 回绕到 0 再递增即可命中。 */
    deadline = RTC_GetCounter() + timeout_s * EPD_RTC_HZ;

    while (BUSY()) {
        const uint32_t now = RTC_GetCounter();

        /* 有符号差比较，回绕时同样正确。 */
        if ((int32_t)(now - deadline) >= 0) {
            s_busy_timeout = 1;
            PRINT("[EPD] busy timeout\r\n");
            RTC_SetAlarm(0xFFFFFFFFu);
            RTC_WaitForLastTask();
            return;
        }

        /* 闹钟直接写 deadline；只要 now < deadline 就至少还有 1 个 tick
         * (0.5 s) 的余量，不会因为设置耗时错过。 */
        RTC_SetAlarm(deadline);
        RTC_WaitForLastTask();
        RTC_ClearITPendingBit(RTC_IT_ALR);
        EXTI_ClearITPendingBit(EXTI_Line17);
        EXTI_ClearITPendingBit(EPD_BUSY_EXTI_LINE);
        NVIC_ClearPendingIRQ(RTCAlarm_IRQn);
        NVIC_ClearPendingIRQ(EPD_BUSY_IRQn);

        /* 清挂起位到进 STOP 之间 BUSY 可能已经落下，再确认一次 */
        if (!BUSY()) {
            break;
        }

        PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
        epd_restore_clock();
    }

    /* 正常结束后把闹钟移到很远，避免后续误触发 */
    RTC_SetAlarm(0xFFFFFFFFu);
    RTC_WaitForLastTask();
}

/* -------------------------------------------------------------- helpers */

static void epd_analog_off(void)
{
    if (!analog_on) {
        return;
    }
    EPD_Cmd(0x22);
    EPD_Dat(0x03);
    EPD_Cmd(0x20);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (!BUSY()) {
        analog_on = 0;
    }
}

static void epd_analog_on(void)
{
    EPD_Cmd(0x22);
    EPD_Dat(0xC0);
    EPD_Cmd(0x20);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (!BUSY()) {
        analog_on = 1;
    }
}

static void epd_reset(void)
{
    epd_analog_off();
    RST_H();
    EPD_DelayMs(EPD_RESET_HIGH_MS);
    RST_L();
    EPD_DelayMs(EPD_RESET_LOW_MS);
    RST_H();
    EPD_DelayMs(EPD_RESET_HIGH_MS);
    analog_on = 0;
    s_pending = 0;
    epd_wait(EPD_BUSY_POLLS_LONG);
}

static void epd_load_lut(const uint8_t *lut)
{
    EPD_Cmd(0x32);
    epd_dat_buf(lut, 153);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x3F);
    EPD_Dat(lut[153]);
    EPD_Cmd(0x03);
    EPD_Dat(lut[154]);
    EPD_Cmd(0x04);
    EPD_Dat(lut[155]);
    EPD_Dat(lut[156]);
    EPD_Dat(lut[157]);
    EPD_Cmd(0x2C);
    EPD_Dat(lut[158]);
}

/* 设置 RAM 窗口。行优先缓存按 X、Y 递减的顺序直接写入，
 * 使逻辑像素映射与 SHT_152 已验证的列优先驱动一致。
 */
static void epd_window(uint8_t row0, uint8_t row1, uint8_t col0, uint8_t col1)
{
    const uint8_t x0 = (uint8_t)(EPD_STRIDE - 1 - col0);
    const uint8_t x1 = (uint8_t)(EPD_STRIDE - 1 - col1);
    const uint8_t y0 = (uint8_t)(EPD_H - 1 - row0);
    const uint8_t y1 = (uint8_t)(EPD_H - 1 - row1);

    EPD_Cmd(0x44);              /* X 起止（字节列），递减 */
    EPD_Dat(x0);
    EPD_Dat(x1);
    EPD_Cmd(0x45);              /* Y 起止，递减 */
    EPD_Dat(y0);
    EPD_Dat(0x00);
    EPD_Dat(y1);
    EPD_Dat(0x00);
    EPD_Cmd(0x4E);              /* 光标 X = 起始列 */
    EPD_Dat(x0);
    EPD_Cmd(0x4F);              /* 光标 Y = 起始行 */
    EPD_Dat(y0);
    EPD_Dat(0x00);
}

static void epd_window_full(void)
{
    epd_window(0, (uint8_t)(EPD_H - 1), 0, (uint8_t)(EPD_STRIDE - 1));
}

/* --------------------------------------------------------------- public */

void EPD_Init(void)
{
    epd_reset();
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x12);
    EPD_DelayMs(10);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x01);
    EPD_Dat(0x97);
    EPD_Dat(0x00);
    EPD_Dat(0x00);

    EPD_Cmd(0x11);
    EPD_Dat(0x00);

    epd_window_full();

    EPD_Cmd(0x3C);
    EPD_Dat(0x01);

    EPD_Cmd(0x18);
    EPD_Dat(0x80);
}

void EPD_SendDisplay(const uint8_t *image)
{
    EPD_FastSync();
    if (s_pending) {
        return;
    }

    epd_reset();
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x12);
    EPD_DelayMs(10);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x01);
    EPD_Dat(0x97);
    EPD_Dat(0x00);
    EPD_Dat(0x00);
    EPD_Cmd(0x11);
    EPD_Dat(0x00);
    epd_window_full();
    EPD_Cmd(0x3C);
    EPD_Dat(0x01);
    EPD_Cmd(0x18);
    EPD_Dat(0x80);

    EPD_Cmd(0x22);          /* load temperature */
    EPD_Dat(0xB1);
    EPD_Cmd(0x20);
    epd_wait(EPD_BUSY_POLLS_LONG);
    if (BUSY()) {
        return;
    }

    epd_load_lut(lut_full);
    if (BUSY()) {
        return;
    }

    epd_window_full();
    EPD_Cmd(0x24);
    epd_dat_buf(image, EPD_FRAME_BYTES);

    epd_window_full();
    EPD_Cmd(0x26);
    epd_dat_buf(image, EPD_FRAME_BYTES);

    EPD_Cmd(0x22);
    EPD_Dat(0xC7);
    EPD_Cmd(0x20);
    epd_wait(EPD_BUSY_POLLS_LONG);      /* 全刷一次约 1.3 秒 */
    if (!BUSY()) {
        analog_on = 0;      /* the full sequence powers down by itself */
        if (image != 0) {
            epd_net_set_from_image(image);
        } else {
            epd_net_clear();
        }
        s_hold_pending = 0;
    }
}

void EPD_FastPrepareFrames(uint8_t frames)
{
    static uint8_t scratch[159];

    s_fast_frames = (frames != 0u) ? frames : 1u;
    epd_build_mask_lut();
    for (uint16_t i = 0; i < 159u; ++i) {
        scratch[i] = lut_fast_base[i];
    }
    if (frames <= 1u) {
        scratch[60] = 0;
        scratch[68] = 0;
    } else if (frames == 2u) {
        scratch[60] = 0;
        scratch[68] = 1;
    } else {
        scratch[60] = (uint8_t)(frames - 2u);
        scratch[68] = 1;
    }

    epd_reset();            /* hardware reset only: the partial path skips 0x12 */
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x01);
    EPD_Dat(0x97);
    EPD_Dat(0x00);
    EPD_Dat(0x00);
    EPD_Cmd(0x11);
    EPD_Dat(0x00);
    EPD_Cmd(0x18);
    EPD_Dat(0x80);

    epd_load_lut(scratch);
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x37);          /* no automatic RAM ping-pong */
    for (uint8_t i = 0; i < 10u; ++i) {
        EPD_Dat(0x00);
    }

    EPD_Cmd(0x3C);
    EPD_Dat(0x80);

    epd_analog_on();        /* power up once and keep it up */
}

void EPD_FastPrepare(void)
{
    EPD_FastPrepareFrames(EPD_FAST_FRAMES);
}

/* 与 budget 驱动共用接口；这里表示黑/白方向一次变化后的最大预驱动帧数。 */
void EPD_SetPulseBudget(uint8_t black, uint8_t white)
{
    /* 3 bit 计数最大为 7；0 仍按 1 处理。 */
    if (black == 0u) {
        black = 1u;
    } else if (black > EPD_MAX_PULSE) {
        black = EPD_MAX_PULSE;
    }
    if (white == 0u) {
        white = 1u;
    } else if (white > EPD_MAX_PULSE) {
        white = EPD_MAX_PULSE;
    }
    s_dc_black = black;
    s_dc_white = white;
    epd_build_mask_lut();
}

// 传入一帧并刷新
// black和white指向对应的数据数组
void EPD_FastScan(const uint8_t *black, const uint8_t *white)
{
    EPD_FastSync();
    if (s_pending) {
        return;
    }

    epd_window_full();
    EPD_Cmd(0x26);
    epd_dat_buf(black, EPD_FRAME_BYTES);

    EPD_Cmd(0x24);
    epd_dat_buf(white, EPD_FRAME_BYTES);

    EPD_Cmd(0x22);
    EPD_Dat(0x0C);
    EPD_Cmd(0x20);
    epd_wait(EPD_BUSY_POLLS_SCAN);
}

#if EPD_PARTIAL_WINDOW
/* 全帧扫描：只在 EPD_PARTIAL_WINDOW=1 时用于得到准确的起止行。
 * 普通全屏发送使用下面的 epd_probe_for_drive() 提前退出。 */
static int epd_plan_frame(const uint8_t *next, uint8_t *r0, uint8_t *r1)
{
    uint8_t first = EPD_H;
    uint8_t last = 0;
    int driven = 0;

    for (uint32_t row = 0; row < EPD_H; ++row) {
        int any = 0;

        for (uint32_t k = 0; k < EPD_STRIDE; ++k) {
            if (epd_frame_byte_needs_drive(next, row * EPD_STRIDE + k)) {
                any = 1;
                break;
            }
        }

        if (any != 0) {
            if (row < first) {
                first = (uint8_t)row;
            }
            last = (uint8_t)row;
            driven = 1;
        }
    }

    *r0 = first;
    *r1 = last;
    return driven;
}
#endif

#if !EPD_PARTIAL_WINDOW
/* 全屏模式下只需要知道“本帧是否有像素要驱动”。从上次找到的位置开始，
 * 找到第一个要驱动的像素就立即退出；若到帧尾没有找到则绕回帧首继续。 */
static int epd_probe_for_drive(const uint8_t *next, uint8_t *r0, uint8_t *r1)
{
    uint32_t start = s_probe_pos;
    uint32_t found = EPD_FRAME_BYTES;

    if (start >= EPD_FRAME_BYTES) {
        start = 0;
    }

    for (uint32_t p = start; p < EPD_FRAME_BYTES; ++p) {
        if (epd_frame_byte_needs_drive(next, p)) {
            found = p;
            break;
        }
    }

    if (found == EPD_FRAME_BYTES) {
        for (uint32_t p = 0; p < start; ++p) {
            if (epd_frame_byte_needs_drive(next, p)) {
                found = p;
                break;
            }
        }
    }

    if (found == EPD_FRAME_BYTES) {
        /* 本次没有任何驱动；保留游标，等下一次继续从这里探测。 */
        *r0 = EPD_H;
        *r1 = 0;
        return 0;
    }

    /* 下次仍然从这个像素位置附近开始探测。 */
    s_probe_pos = found;
    *r0 = 0;
    *r1 = (uint8_t)(EPD_H - 1);
    return 1;
}
#endif

/* 面板刷完后，按实际驱动方向更新净驱动帧数。
 * 先用查表得到黑/白平面，再只更新掩码中实际置位的像素。 */
static void epd_apply_charge(const uint8_t *next)
{
    const int frames = (int)s_fast_frames;

    for (uint32_t i = 0; i < EPD_FRAME_BYTES; ++i) {
        const uint8_t px = next[i];
        uint8_t *const cell = &s_net[i * 4u];
        const uint8_t black = (uint8_t)(epd_drive_mask_byte(cell, 0u) & (uint8_t)~px);
        const uint8_t white = (uint8_t)(epd_drive_mask_byte(cell, 1u) & px);
        const uint8_t active = (uint8_t)(black | white);

        if (active == 0u) {
            continue;
        }

        for (uint8_t b = 0; b < 4u; ++b) {
            const uint8_t m0 = (uint8_t)(0x80u >> (b * 2u));
            const uint8_t m1 = (uint8_t)(0x40u >> (b * 2u));

            if ((active & (m0 | m1)) == 0u) {
                continue;
            }

            {
                const uint8_t nb = cell[b];
                int n0 = epd_signed_nibble(nb);
                int n1 = epd_signed_nibble((uint8_t)(nb >> 4));

                if ((black & m0) != 0u) {
                    n0 += frames;
                } else if ((white & m0) != 0u) {
                    n0 -= frames;
                }

                if ((black & m1) != 0u) {
                    n1 += frames;
                } else if ((white & m1) != 0u) {
                    n1 -= frames;
                }

                cell[b] = (uint8_t)(((uint8_t)n0 & 0x0Fu) |
                                    (uint8_t)(((uint8_t)n1 & 0x0Fu) << 4));
            }
        }
    }
}

/* 生成一小块平面数据。plane=0 发黑平面，plane=1 发白平面；
 * all_zero 用于 HOLD 收尾的全 0 掩码。 */
static void epd_build_plane_chunk(uint8_t *dst, uint32_t frame_offset, uint32_t len,
                                  const uint8_t *next, uint8_t plane, uint8_t all_zero)
{
    if (all_zero != 0u) {
        for (uint32_t i = 0; i < len; ++i) {
            dst[i] = 0x00;
        }
        return;
    }

    for (uint32_t i = 0; i < len; ++i) {
        const uint32_t frame_index = frame_offset + i;
        const uint8_t px = next[frame_index];
        const uint8_t *const cell = &s_net[frame_index * 4u];
        uint32_t net_word;

        /* 该平面没有目标像素时直接清零。 */
        if (((plane == 0u) && (px == 0xFFu)) ||
            ((plane != 0u) && (px == 0x00u))) {
            dst[i] = 0x00;
            continue;
        }

        memcpy(&net_word, cell, sizeof(net_word));

        /* 32-bit 全饱和跳过。 */
        if ((plane == 0u) && (net_word == 0x77777777u)) {
            dst[i] = 0x00;      /* 8 个 net 全为 +7，黑方向已经饱和 */
            continue;
        }
        if ((plane != 0u) && (net_word == 0x99999999u)) {
            dst[i] = 0x00;      /* 8 个 net 全为 -7，白方向已经饱和 */
            continue;
        }

        {
            const uint8_t drive = epd_drive_mask_byte(cell, plane);
            const uint8_t target = (plane == 0u) ? (uint8_t)~px : px;

            dst[i] = (uint8_t)(drive & target);
        }
    }
}

/* 按窗口把一张由状态表实时生成的平面送给面板。
 *
 * 双缓冲流水：
 *   1. 生成长度为 L 的 A 块并启动 DMA；
 *   2. DMA 发 A 时，CPU 生成下一块 B；
 *   3. 等 A 的 DMA TC 后立刻启动 B；
 *   4. DMA 发 B 时，CPU 生成下一块 A，如此循环。
 *
 * 块之间不等待 SPI BSY，只等 DMA TC，所以计算时间被隐藏在传输期间。 */
static void epd_send_generated_plane(uint8_t cmd, uint8_t plane,
                                     const uint8_t *next,
                                     uint8_t row0, uint8_t row1,
                                     uint8_t col0, uint8_t col1,
                                     uint8_t all_zero)
{
    uint32_t first;
    uint32_t total;
    uint32_t sent;
    uint32_t cur_len;
    uint8_t cur;

    if (row1 < row0) {
        return;
    }

    first = (uint32_t)row0 * EPD_STRIDE;
    total = (uint32_t)(row1 - row0 + 1u) * EPD_STRIDE;

    epd_window(row0, row1, col0, col1);
    EPD_Cmd(cmd);
    DC_H();
    CS_L();

    cur_len = (total > EPD_PLANE_CHUNK) ? EPD_PLANE_CHUNK : total;
    cur = 0;
    epd_build_plane_chunk(s_plane_buf[cur], first, cur_len,
                          next, plane, all_zero);
    epd_dma_start(s_plane_buf[cur], cur_len);
    sent = cur_len;

    while (sent < total) {
        const uint32_t left = total - sent;
        const uint32_t next_len = (left > EPD_PLANE_CHUNK) ? EPD_PLANE_CHUNK : left;
        const uint8_t next_buf = (uint8_t)(cur ^ 1u);

        /* 当前块正在 DMA 发送，这里先算下一块。 */
        epd_build_plane_chunk(s_plane_buf[next_buf], first + sent, next_len,
                              next, plane, all_zero);

        epd_dma_wait_tc(s_plane_buf[cur], cur_len);
        epd_dma_start(s_plane_buf[next_buf], next_len);

        cur = next_buf;
        cur_len = next_len;
        sent += next_len;
    }

    epd_dma_wait_tc(s_plane_buf[cur], cur_len);
    epd_spi_wait_idle();
    CS_H();
}

static void epd_send_planes(const uint8_t *next, uint8_t row0, uint8_t row1,
                            uint8_t col0, uint8_t col1)
{
    epd_send_generated_plane(0x26, 0u, next, row0, row1, col0, col1, 0u);
    epd_send_generated_plane(0x24, 1u, next, row0, row1, col0, col1, 0u);
}

/* 等面板把已经激活的那一帧刷完。
   如果是单核运行的话，激活之后
   CPU可以先算下一帧。一边算面板一边刷新
   刷完了再回来。 */
void EPD_FastSync(void)
{
    if (!s_pending) {
        return;
    }
    epd_wait(EPD_BUSY_POLLS_SCAN); //有超时的等待busy
    if (BUSY()) {
        //等待完了如果还是忙说明卡死，epd_wait会把超时标志置位，这里返回就行了
        return;
    }
    s_pending = 0;
}

// 激活刷新，0x0C会让epd的升压电路不关闭，这样才能做到很快地刷新 
static void epd_activate(void)
{
    EPD_Cmd(0x22);
    EPD_Dat(0x0C);          /* display, one controller frame, keep the booster */
    EPD_Cmd(0x20);
    s_pending = 1;
}

void EPD_FastBegin(const uint8_t *shown)
{
    /* shown 是当前面板上已经显示的一帧。按它初始化饱和 net，
     * 使下一帧相同像素不再被重复驱动。 */
    if (shown != 0) {
        epd_net_set_from_image(shown);
    } else {
        epd_net_clear();
    }
    s_scan_count = 0;
    s_hold_pending = 0;
    s_pending = 0;
    s_row0 = EPD_H;
    s_row1 = 0;
}

/* 取走"上一次 FastFrame 是重来否遇到 BUSY 超时"的标志（读后清零）。
 * 上层看到 1 就说明面板卡住了，应该 EPD_Init() 一遍。 */
uint8_t EPD_TakeTimeoutFlag(void)
{
    const uint8_t t = s_busy_timeout;

    s_busy_timeout = 0;
    return t;
}

uint32_t EPD_FastScanCount(void)
{
    return s_scan_count;
}

//主函数，外部不断调用这里。
int EPD_FastFrame(const uint8_t *next)
{
    uint8_t row0, row1;
    int driven = 0;

    // 阶段清零，每隔一段时间全刷 
    if ((EPD_CLEAN_INTERVAL != 0) && (s_scan_count >= (uint32_t)EPD_CLEAN_INTERVAL)) {
        PRINT("[EPD] clean refresh after %u fast scans\r\n", (unsigned)s_scan_count);
        /* 只看本次全刷是否超时：历史超时标志若一直留着，这里每次都会判失败，
         * s_scan_count 就永远清不掉，于是每帧都来一次全刷。 */
        s_busy_timeout = 0;
        EPD_SendDisplay(next);
        if (s_busy_timeout) {
            s_scan_count = 0;    /* 本次失败也退出 clean，避免每帧重试全刷 */
            return -1;
        }
        EPD_FastBegin(next);
        EPD_FastPrepare();
        return s_busy_timeout ? -1 : 1;
    }

    /* 全屏模式只需找到第一个需要驱动的像素即可提前退出；
     * 局部窗口模式需要完整扫描才能得到准确的行范围。 */
#if EPD_PARTIAL_WINDOW
    driven = epd_plan_frame(next, &row0, &row1);
#else
    driven = epd_probe_for_drive(next, &row0, &row1);
#endif

    /* 列范围固定整行（0..18）。列也缩小范围AI没搞出来。
    不过之前light ink做到过，所以肯定是有办法的
    不过这个只影响写入RAM的速度，作用不算很大，不浪费时间了 */
    s_col0 = 0;
    s_col1 = (uint8_t)(EPD_STRIDE - 1);

    if (driven) {
        /* busy高的时候不能写RAM，所以还是得等刷完了再写进去 */
        EPD_FastSync();      // 等上一帧刷完                                    
        if (s_pending) {
            return -1;
        }
        s_row0 = row0;
        s_row1 = row1;

        //把待刷新的数据写入EPD中
#if EPD_PARTIAL_WINDOW
        epd_send_planes(next, s_row0, s_row1, s_col0, s_col1);    /* 只发变化的行 */
#else
        epd_send_planes(next, 0, (uint8_t)(EPD_H - 1), 0, (uint8_t)(EPD_STRIDE - 1));
#endif
        //刷新动作就是这个函数。触发刷新之后对像素预算进行处理就很快返回了。如果单核的话此时主循环就可以去算下一帧。
        epd_activate();

        ++s_scan_count;
        s_hold_pending = 1;

        /* 这一帧已经交给面板了，更新每像素净驱动帧数。 */
        epd_apply_charge(next);
    } else if (EPD_HOLD_ENDING && s_hold_pending) {
        /* HOLD 收尾：沿用上一次真正发过的行范围，两块全 0 掩码，所有像素都不驱动，
         * 让上一次的驱动把波形走完而不是停在半途。 */
        if (s_row1 >= s_row0) {
            EPD_FastSync();          /* 等上一帧刷完 */
            if (s_pending) {
                return -1;
            }
            epd_send_generated_plane(0x26, 0u, next, s_row0, s_row1, s_col0, s_col1, 1u);
            epd_send_generated_plane(0x24, 1u, next, s_row0, s_row1, s_col0, s_col1, 1u);
            epd_activate();          /* 全 HOLD 的一帧，同样不等它刷 */

            ++s_scan_count;
        }
        s_hold_pending = 0;
    }

    return driven;
}

void EPD_Sleep(void)
{
    EPD_FastSync();
    if (s_pending) {
        return;
    }
    epd_analog_off();
    if (BUSY()) {
        return;
    }

    EPD_Cmd(0x10);
    EPD_Dat(0x01);
    EPD_DelayMs(100);
}

