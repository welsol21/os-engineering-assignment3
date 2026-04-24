#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

struct event {
    __u32 pid;
    char comm[16];
    __u32 len;
    char data[64];
};

struct recv_args {
    const char *buf;
    __u64 req_len;
};

/* sys_enter/sys_exit tracepoint contexts */
struct trace_event_raw_sys_enter {
    __u64 unused;
    long id;
    unsigned long args[6];
};

struct trace_event_raw_sys_exit {
    __u64 unused;
    long id;
    long ret;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u64);
    __type(value, struct recv_args);
} inflight SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 24);
} events SEC(".maps");

static __always_inline int is_curl(const char comm[16])
{
    return comm[0] == 'c' &&
           comm[1] == 'u' &&
           comm[2] == 'r' &&
           comm[3] == 'l' &&
           comm[4] == '\0';
}

/*
 * recvfrom(fd, ubuf, size, flags, addr, addr_len)
 * args[0] = fd
 * args[1] = ubuf
 * args[2] = size
 */
SEC("tracepoint/syscalls/sys_enter_recvfrom")
int handle_recvfrom_enter(struct trace_event_raw_sys_enter *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    struct recv_args args = {};
    char comm[16];

    bpf_get_current_comm(&comm, sizeof(comm));
    if (!is_curl(comm))
        return 0;

    args.buf = (const char *)ctx->args[1];
    args.req_len = (__u64)ctx->args[2];

    bpf_map_update_elem(&inflight, &pid_tgid, &args, BPF_ANY);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvfrom")
int handle_recvfrom_exit(struct trace_event_raw_sys_exit *ctx)
{
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    struct recv_args *args;
    struct event *e;
    long ret;
    __u32 copy_len = 0;

    args = bpf_map_lookup_elem(&inflight, &pid_tgid);
    if (!args)
        return 0;

    ret = ctx->ret;
    if (ret <= 0) {
        bpf_map_delete_elem(&inflight, &pid_tgid);
        return 0;
    }

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        bpf_map_delete_elem(&inflight, &pid_tgid);
        return 0;
    }

    e->pid = pid_tgid >> 32;
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    e->len = (__u32)ret;
    __builtin_memset(e->data, 0, sizeof(e->data));

    copy_len = ret < (long)sizeof(e->data) ? (__u32)ret : (__u32)sizeof(e->data);
    if (copy_len > args->req_len)
        copy_len = (__u32)args->req_len;

    if (copy_len > 0)
        bpf_probe_read_user(e->data, copy_len, args->buf);

    bpf_ringbuf_submit(e, 0);
    bpf_map_delete_elem(&inflight, &pid_tgid);
    return 0;
}