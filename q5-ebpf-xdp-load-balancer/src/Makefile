BPF_CLANG ?= clang
CC ?= gcc

CFLAGS += -O2 -g -Wall
BPF_CFLAGS += -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu

LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null)

all: xdp_lb

xdp_lb.bpf.o: xdp_lb.bpf.c
	$(BPF_CLANG) $(BPF_CFLAGS) -c $< -o $@

xdp_lb.skel.h: xdp_lb.bpf.o
	bpftool gen skeleton $< > $@

xdp_lb: xdp_lb.c xdp_lb.skel.h
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS) $< -o $@ $(LIBBPF_LIBS) -lelf -lz

clean:
	rm -f xdp_lb xdp_lb.bpf.o xdp_lb.skel.h