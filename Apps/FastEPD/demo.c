#include <string.h>

#include "demo.h"
#include "epd_fast.h"
#include "debug.h"
#include "ch32v20x_rtc.h"

static uint8_t frame[EPD_FRAME_BYTES] __attribute__((aligned(4)));

static inline void px(int x, int y, int ink)
{
    if ((x < 0) || (x >= EPD_W) || (y < 0) || (y >= EPD_H)) {
        return;
    }
    if (ink) {
        frame[(uint32_t)y * EPD_STRIDE + (uint32_t)(x >> 3)] &= (uint8_t)~(0x80u >> (x & 7));
    } else {
        frame[(uint32_t)y * EPD_STRIDE + (uint32_t)(x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
    }
}

static void fill(int x, int y, int w, int h, int ink)
{
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            px(x + i, y + j, ink);
        }
    }
}

static const uint8_t font8x16[128][16] = {
    [' '] = {0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00},
    ['0'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00},
    ['1'] = {0x00,0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00},
    ['2'] = {0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x30,0x60,0x60,0x66,0x7E,0x00,0x00,0x00},
    ['3'] = {0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x0C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00},
    ['4'] = {0x00,0x00,0x0C,0x1C,0x3C,0x6C,0x6C,0x6C,0x7E,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00},
    ['5'] = {0x00,0x00,0x7E,0x60,0x60,0x60,0x7C,0x66,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00},
    ['6'] = {0x00,0x00,0x1C,0x30,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00},
    ['7'] = {0x00,0x00,0x7E,0x66,0x06,0x06,0x0C,0x0C,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00},
    ['8'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00},
    ['9'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3E,0x06,0x06,0x06,0x0C,0x38,0x00,0x00,0x00},
    [':'] = {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
    ['F'] = {0x00,0x00,0x7E,0x60,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00},
    ['P'] = {0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x60,0x60,0x00,0x00,0x00},
    ['S'] = {0x00,0x00,0x3C,0x66,0x60,0x60,0x3C,0x06,0x06,0x06,0x66,0x66,0x3C,0x00,0x00,0x00},
};

static void draw_char(int x, int y, char c)
{
    if ((uint8_t)c >= 128u) {
        return;
    }
    for (int row = 0; row < 16; ++row) {
        const uint8_t bits = font8x16[(uint8_t)c][row];

        for (int col = 0; col < 8; ++col) {
            if ((bits & (uint8_t)(0x80u >> col)) != 0u) {
                px(x + col, y + row, 1);
            }
        }
    }
}

static void draw_str(int x, int y, const char *s)
{
    while (*s != '\0') {
        draw_char(x, y, *s);
        x += 8;
        ++s;
    }
}

static void draw_num(int x, int y, uint32_t num)
{
    char buf[4];
    int n = 0;

        buf[n++] = (char)('0' + (num / 100u) % 10u);
        buf[n++] = (char)('0' + (num / 10u) % 10u);
    buf[n++] = (char)('0' + num % 10u);
    buf[n] = '\0';

    draw_str(x, y, buf);
}
static void draw_fps(int x, int y, uint32_t fps)
{
    char buf[10];
    int n = 0;

    buf[n++] = 'F';
    buf[n++] = 'P';
    buf[n++] = 'S';
    buf[n++] = ':';
    if (fps >= 100u) {
        buf[n++] = (char)('0' + (fps / 100u) % 10u);
    }
    if (fps >= 10u) {
        buf[n++] = (char)('0' + (fps / 10u) % 10u);
    }
    buf[n++] = (char)('0' + fps % 10u);
    buf[n] = '\0';

    draw_str(x, y, buf);
}


void display_demo(){

    EPD_Hal_Init();
    
    uint8_t notbegin = 1;

    const int sq_size = 28;
    const int margin = 30;      
    const int min_x = margin;
    const int min_y = margin;
    const int max_x = EPD_W - margin - sq_size;
    const int max_y = EPD_H - margin - sq_size;
    int sq_x = min_x;
    int sq_y = min_y;
    int dir = 0;                                
    int pause = 0;                             
    int step = 0;

    uint32_t fps_val = 0;
    uint32_t fps_cnt = 0;
    uint32_t fps_sec = 0;


    while(1){


        memset(frame, 0xFF, sizeof(frame));
        fill(sq_x, sq_y, sq_size, sq_size, 1);

        if (pause > 0) {
            --pause;
        } else {
            switch (dir) {
            case 0:
                if (sq_x < max_x) {
                    sq_x+=1;
                } else {
                    dir = 1;
                    pause = 14;
                }
                break;
            case 1:
                if (sq_y < max_y) {
                    sq_y+=1;
                } else {
                    dir = 2;
                    pause = 14;
                }
                break;
            case 2:
                if (sq_x > min_x) {
                    sq_x-=1;
                } else {
                    dir = 3;
                    pause = 14;
                }
                break;
            default:
                if (sq_y > min_y) {
                    sq_y-=1;
                } else {
                    dir = 0;
                    pause = 14;
                }
                break;
            }
        }

        draw_fps(2, 2, fps_val);
        draw_num(2, 20, step);

        if(notbegin){
            notbegin = 0;
            EPD_SendDisplay(frame);
            EPD_FastBegin(frame);
            EPD_FastPrepare();
        }

        EPD_FastFrame(frame);
        EPD_FastSync();  //at about 67 (67!) FPS the ghosting is almost invisible (kept you waiting huh)
        Delay_Ms(15);     //commet out this delay and fast sync to get 140 FPS.
                     
        
        ++fps_cnt;
        {
            const uint32_t now = RTC_GetCounter();

            if ((int32_t)(now - fps_sec) >= 2) {
                fps_val = fps_cnt;
                fps_cnt = 0;
                fps_sec = now;
                printf("FPS:%u\r\n", (unsigned)fps_val);
                step = step++ > 999?0:step;
            }
        }
    }


}