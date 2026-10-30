TARGET := FastEPD

# ch32v203.mk already adds ./*.c, ../../SRC/Core, Debug and Peripheral sources.
C_INCLUDES := -I"./"

NEEDS_UNBRICK = 1
include ../../ch32v203.mk

# Must be after the include: ch32v203.mk assigns CFLAGS itself.
# Select USART1/PA9 in the WCH debug helper and enable printf.
#CFLAGS += -DDEBUG=1
