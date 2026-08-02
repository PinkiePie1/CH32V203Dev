TARGET := LED_Fluid
#C_SOURCES := $(wildcard ./*.c) 
#C_INCLUDES := -I"./" 
#C_SOURCES += $(wildcard ./CharlieDriver/*.c) 
#C_INCLUDES += -I"./CharlieDriver/" 


include ../../ch32v203.mk

# 打开 PRINT 宏(debug.h 中 DEBUG 未定义时 PRINT 展开为空,没有任何串口输出)
CFLAGS += -DDEBUG=1
