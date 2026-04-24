#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <bpf/libbpf.h>
#include "tcpdump_portion.skel.h"

static volatile sig_atomic_t exiting = 0;

struct event {
    __u32 pid;
    char comm[16];
    __u32 len;
    char data[64];
};

static void handle_sigint(int sig)
{
    (void)sig;
    exiting = 1;
}

// static void print_payload(const char *data, __u32 max_len)
// {
//     __u32 i;

//     putchar('"');
//     for (i = 0; i < max_len; i++) {
//         unsigned char c = (unsigned char)data[i];
//         if (c == '\0')
//             break;

//         if (isprint(c))
//             putchar(c);
//         else
//             printf("\\x%02x", c);
//     }
//     putchar('"');
// }

static void print_payload_hex(const char *data, __u32 max_len)
{
    __u32 i;

    for (i = 0; i < max_len; i++) {
        printf("%02x ", (unsigned char)data[i]);
    }
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct event *e = data;
    __u32 shown = e->len < sizeof(e->data) ? e->len : sizeof(e->data);

    (void)ctx;
    (void)data_sz;

    // printf("pid=%u comm=%s len=%u data=", e->pid, e->comm, e->len);
    // print_payload(e->data, shown);
    printf("pid=%u comm=%s len=%u data_hex=", e->pid, e->comm, e->len);
    print_payload_hex(e->data, shown);
    putchar('\n');
    return 0;
}

int main(void)
{
    struct tcpdump_portion_bpf *skel = NULL;
    struct ring_buffer *rb = NULL;
    int err = 0;

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    skel = tcpdump_portion_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "failed to open/load BPF skeleton\n");
        return 1;
    }

    err = tcpdump_portion_bpf__attach(skel);
    if (err) {
        fprintf(stderr, "failed to attach BPF skeleton: %d\n", err);
        tcpdump_portion_bpf__destroy(skel);
        return 1;
    }

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "failed to create ring buffer\n");
        tcpdump_portion_bpf__destroy(skel);
        return 1;
    }

    printf("Tracing recvfrom payload... Press Ctrl-C to stop.\n");

    while (!exiting) {
        err = ring_buffer__poll(rb, 100);
        if (err < 0) {
            fprintf(stderr, "ring_buffer__poll failed: %d\n", err);
            break;
        }
    }

    ring_buffer__free(rb);
    tcpdump_portion_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}