# Assignment 3 Project

## Environment used

This project was developed and tested on the following environment:

- **OS:** Ubuntu 22.04.5 LTS (Jammy)
- **Kernel:** Linux `6.8.0-110-generic`
- **Architecture:** `x86_64`
- **GCC:** `11.4.0`
- **Clang/LLVM:** `14.0.0`
- **GNU ld (binutils):** `2.38`
- **GNU Make:** `4.3`
- **libbpf (pkg-config):** `0.5.0`
- **bpftool:** `v7.4.0`
- **Compiler paths:**
  - `gcc`: `/usr/bin/gcc`
  - `clang`: `/usr/bin/clang`
  - `bpftool`: `/usr/sbin/bpftool`

Full captured environment summary:

```text
=== OS ===
Distributor ID: Ubuntu
Description: Ubuntu 22.04.5 LTS
Release: 22.04
Codename: jammy

=== Kernel ===
Linux ML-desktop 6.8.0-110-generic #110~22.04.1-Ubuntu SMP PREEMPT_DYNAMIC Fri Mar 27 12:43:08 UTC x86_64 x86_64 x86_64 GNU/Linux

=== GCC ===
gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0

=== Clang ===
Ubuntu clang version 14.0.0-1ubuntu1.1

=== LD ===
GNU ld (GNU Binutils for Ubuntu) 2.38

=== Make ===
GNU Make 4.3

=== libbpf ===
0.5.0

=== LLVM ===
14.0.0

=== bpftool ===
bpftool v7.4.0
using libbpf v1.4
features:

=== Paths ===
/usr/bin/gcc
/usr/bin/clang
/usr/sbin/bpftool
```

## Notes on the environment

- **Q1** is a normal Linux userspace C program and was built with GCC on Ubuntu 22.04.
- **Q4** and **Q5** require a Linux host with eBPF/XDP support and **root privileges** to load BPF programs.
- The practical eBPF/XDP work was tested on the Ubuntu host kernel shown above.
- Interface names and local IP addresses used in Q5 are machine-specific and may need to be changed on another system.

## Suggested packages

On Ubuntu 22.04, the useful packages are:

```bash
sudo apt update
sudo apt install -y build-essential clang llvm libbpf-dev libelf-dev pkg-config
```

`bpftool` availability depends on the installed kernel packages. On the system used for testing it was already available and working.

## Project structure

- `ASSIGNMENT3_REPORT.md` - main report
- `q1-concurrent-web-server/` - Question 1
- `q4-ebpf-tcp-dump/` - Question 4
- `q5-ebpf-xdp-load-balancer/` - Question 5
- `q5_hello.png`, `q5_index.png` - screenshots used in the Q5 report section
- `references/` - assignment brief, notes, and supporting reading

## Question 1 - Concurrent web server

Location:

```text
q1-concurrent-web-server/
```

Important files:

- `src/server.c`
- `src/server.h`
- `www/index.html`
- `www/hello.txt`

Build:

```bash
cd q1-concurrent-web-server
gcc -Wall -Wextra -O2 -o server src/server.c -lpthread
```

Run:

```bash
./server
```

Test:

```bash
curl -i http://localhost:3000/
curl -i http://localhost:3000/hello.txt
curl -i http://localhost:3000/nope
```

Expected behaviour:

- `/` returns `200 OK`
- `/hello.txt` returns `200 OK`
- `/nope` returns `404 Not Found`

## Question 4 - eBPF TCP payload tracing tool

Location:

```text
q4-ebpf-tcp-dump/
```

Important files:

- `src/tcpdump_portion.bpf.c`
- `src/tcpdump_portion.c`
- `src/Makefile`

Build:

```bash
cd q4-ebpf-tcp-dump/src
make
```

Run:

```bash
sudo ./tcpdump_portion
```

In another terminal, generate traffic against the Q1 server:

```bash
curl http://localhost:3000/hello.txt
```

Typical output:

```text
Tracing recvfrom payload... Press Ctrl-C to stop.
pid=... comm=curl len=... data_hex=...
```

Notes:

- root privileges are required
- this tool traces the user-space receive path and prints the first bytes of received data
- it was tested against the Q1 server on port 3000

## Question 5 - XDP/eBPF load-balancer skeleton

Location:

```text
q5-ebpf-xdp-load-balancer/
```

Important files:

- `src/xdp_lb.bpf.c`
- `src/xdp_lb.c`
- `src/Makefile`

Build:

```bash
cd q5-ebpf-xdp-load-balancer/src
make
```

Run example used in testing:

```bash
sudo ./xdp_lb enp3s0 192.168.50.2 3000 tcp pass
```

What it does:

- attaches an XDP program to a real interface
- parses Ethernet, IPv4 and TCP/UDP
- matches a configured VIP and port
- selects a backend from a simple BPF map
- emits events to userspace
- currently returns `XDP_PASS`

Typical output:

```text
Attached XDP LB skeleton to enp3s0 (ifindex=2)
VIP=192.168.50.2:3000 proto=tcp action=pass backends=[10.0.0.21:80, 10.0.0.22:80]
Waiting for matching packets... Press Ctrl-C to stop.
match proto=tcp src=192.168.50.1:53019 dst=192.168.50.2:3000 action=PASS backend_idx=0 backend=10.0.0.21:80
```

Notes:

- interface name must be changed to match the target machine
- root privileges are required
- this is an educational skeleton, not a full production load balancer

## Report

The main write-up is in:

```text
ASSIGNMENT3_REPORT.md
```

The report covers:
- Q1 implementation
- Q2 theory
- Q3 design discussion
- Q4 practical eBPF tracing tool
- Q5 practical XDP skeleton and explanation