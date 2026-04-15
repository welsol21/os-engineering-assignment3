1. install the following on Ubuntu 

sudo apt install -y \
    build-essential \
    clang \
    llvm \
    libelf-dev \
    zlib1g-dev \
    pkg-config \
    git

sudo apt install -y \
    libbpf-dev \
    linux-headers-$(uname -r) \
    linux-tools-$(uname -r) \
    linux-tools-common \
    bpftool

sudo apt install -y \
    bpfcc-tools \
    python3-bpfcc \
    trace-cmd \
    strace


2. clone repo
git clone --recurse-submodules https://github.com/iovisor/bcc.git
cd bcc/libbpf-tools

3. build with make
make

4. create a new bpf program 
# tcptop is a good networking example
cp tcpconnect.bpf.c  myprobe.bpf.c    
cp tcpconnect.c      myprobe.c 

5.
# In libbpf-tools/Makefile, add to APPS list:
APPS = ... myprobe

6. build with make
make


