#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct event {
    __u32 pid;
    char comm[16];
    __u32 len;
    char data[64];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

SEC("kprobe/tcp_recvmsg")
int handle_tcp_recvmsg(struct pt_regs *ctx)
{
    struct event *e;
    __u64 pid_tgid;
    unsigned long len;

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e)
        return 0;

    pid_tgid = bpf_get_current_pid_tgid();
    e->pid = pid_tgid >> 32;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));

    if (!(e->comm[0] == 'c' &&
      e->comm[1] == 'u' &&
      e->comm[2] == 'r' &&
      e->comm[3] == 'l' &&
      e->comm[4] == '\0')) {
    bpf_ringbuf_discard(e, 0);
    return 0;
    }
    __builtin_memset(e->data, 0, sizeof(e->data));

    len = PT_REGS_PARM3(ctx);
    e->len = (__u32)len;

    bpf_ringbuf_submit(e, 0);
    return 0;
}