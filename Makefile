#
# Makefile for can-joystick
#
# This is a very simple project, so a more complex build system is avoided.
#

SRCS = main.cc \
  libusbcan/src/usbcan.c

BIN = canjoystick

CFLAGS = -Wall \
  -Wextra \
  -Wno-unused-parameter \
  -Wno-unknown-pragmas \
	-Ilibusbcan/include

LDFLAGS = -Llibusbcan/lib/linux_64bit \
  -lGinkgo_Driver

all:
	gcc $(CFLAGS) $(LDFLAGS) $(SRCS) -o $(BIN)
