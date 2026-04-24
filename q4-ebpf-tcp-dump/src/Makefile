BPF_CLANG ?= clang
CC ?= gcc

CFLAGS += -O2 -g -Wall
BPF_CFLAGS += -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu

LIBBPF_CFLAGS := $(shell pkg-config --cflags libbpf 2>/dev/null)
LIBBPF_LIBS := $(shell pkg-config --libs libbpf 2>/dev/null)

all: tcpdump_portion

tcpdump_portion.bpf.o: tcpdump_portion.bpf.c
	$(BPF_CLANG) $(BPF_CFLAGS) -c $< -o $@

tcpdump_portion.skel.h: tcpdump_portion.bpf.o
	bpftool gen skeleton $< > $@

tcpdump_portion: tcpdump_portion.c tcpdump_portion.skel.h
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS) $< -o $@ $(LIBBPF_LIBS) -lelf -lz

clean:
	rm -f tcpdump_portion tcpdump_portion.bpf.o tcpdump_portion.skel.h