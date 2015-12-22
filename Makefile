TARGET = libopusplay.a
OBJS = opusplay.o main.o

KOS_CFLAGS += -I$(KOS_PORTS)/include/opus -Wextra

include ${KOS_PORTS}/scripts/lib.mk
