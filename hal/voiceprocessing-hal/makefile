#Git version
GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

# Set the compiler here
CC = ../../toolchain/bin/arm-linux-gnueabihf-gcc

# Compiler flags
CFLAGS = -g -Wall -fPIC -o0 -D_GNU_SOURCE -DCONFIG_MFD_IA8X01 -DVERSION=\"$(GIT_VERSION)\"

# Include directories
INCLUDES = -I./ -I../../adnc/include/linux/mfd/adnc/

# Library paths
LDFLAGS = -shared

# ----------------- ODSP LIB -------------------
ODSP_LIB_SRCS = iaxxx_odsp_hw.c
ODSP_LIB_OBJS = $(ODSP_LIB_SRCS:.c=.o)
ODSP_LIB = libodsp.so
#-----------------------------------------------

# ----------------- TUNNEL LIB -----------------
TUNNEL_SRCS = tunnel.c
TUNNEL_OBJS = $(TUNNEL_SRCS:.c=.o)
TUNNEL = libtunnel.so

# -------------- build -------------------------
all: $(ODSP_LIB) $(TUNNEL)

$(TUNNEL): $(TUNNEL_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TUNNEL) $(TUNNEL_OBJS) $(LDFLAGS)

$(ODSP_LIB): $(ODSP_LIB_OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(ODSP_LIB) $(ODSP_LIB_OBJS) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $<  -o $@

clean:
	$(RM) *.o *.so
