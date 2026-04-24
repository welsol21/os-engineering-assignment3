#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/in.h>
#include <linux/if_link.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "xdp_lb.skel.h"

static volatile sig_atomic_t exiting = 0;
static int ifindex = 0;

struct service_config {
    __u32 vip;
    __u16 vport;
    __u8 proto;
    __u8 action;
};

struct backend {
    __u32 ip;
    __u16 port;
    __u8 valid;
    __u8 pad;
};

struct event {
    __u32 src_ip;
    __u32 dst_ip;
    __u16 src_port;
    __u16 dst_port;
    __u32 backend_ip;
    __u16 backend_port;
    __u8 proto;
    __u8 action;
    __u8 backend_idx;
};

static void on_sigint(int sig)
{
    (void)sig;
    exiting = 1;
}

static const char *proto_name(__u8 proto)
{
    if (proto == IPPROTO_TCP)
        return "tcp";
    if (proto == IPPROTO_UDP)
        return "udp";
    return "?";
}

static void ip_to_str(__u32 ip_be, char out[INET_ADDRSTRLEN])
{
    struct in_addr addr = { .s_addr = ip_be };
    inet_ntop(AF_INET, &addr, out, INET_ADDRSTRLEN);
}

static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct event *e = data;
    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN], be[INET_ADDRSTRLEN];
    (void)ctx;
    (void)data_sz;

    ip_to_str(e->src_ip, src);
    ip_to_str(e->dst_ip, dst);
    ip_to_str(e->backend_ip, be);

    printf("match proto=%s src=%s:%u dst=%s:%u action=%s backend_idx=%u backend=%s:%u\n",
           proto_name(e->proto),
           src, ntohs(e->src_port),
           dst, ntohs(e->dst_port),
           e->action ? "DROP" : "PASS",
           e->backend_idx,
           e->backend_ip ? be : "-",
           e->backend_port ? ntohs(e->backend_port) : 0);
    return 0;
}

static int parse_proto(const char *s, __u8 *proto)
{
    if (strcmp(s, "tcp") == 0) {
        *proto = IPPROTO_TCP;
        return 0;
    }
    if (strcmp(s, "udp") == 0) {
        *proto = IPPROTO_UDP;
        return 0;
    }
    return -1;
}

static int parse_action(const char *s, __u8 *action)
{
    if (strcmp(s, "pass") == 0) {
        *action = 0;
        return 0;
    }
    if (strcmp(s, "drop") == 0) {
        *action = 1;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    struct xdp_lb_bpf *skel = NULL;
    struct ring_buffer *rb = NULL;
    struct service_config cfg = {};
    struct backend be0 = {}, be1 = {};
    int cfg_fd, backends_fd;
    __u32 key0 = 0, key1 = 1;
    int prog_fd;
    __u8 proto, action;
    int err = 0;

    if (argc < 6) {
        fprintf(stderr,
                "Usage: %s <iface> <vip> <port> <tcp|udp> <pass|drop>\n"
                "Example: %s eth0 10.10.10.10 80 tcp pass\n",
                argv[0], argv[0]);
        return 1;
    }

    ifindex = if_nametoindex(argv[1]);
    if (!ifindex) {
        perror("if_nametoindex");
        return 1;
    }

    if (inet_pton(AF_INET, argv[2], &cfg.vip) != 1) {
        fprintf(stderr, "invalid VIP: %s\n", argv[2]);
        return 1;
    }

    cfg.vport = htons((unsigned short)atoi(argv[3]));

    if (parse_proto(argv[4], &proto) != 0) {
        fprintf(stderr, "invalid proto: %s\n", argv[4]);
        return 1;
    }

    if (parse_action(argv[5], &action) != 0) {
        fprintf(stderr, "invalid action: %s\n", argv[5]);
        return 1;
    }

    cfg.proto = proto;
    cfg.action = action;

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    skel = xdp_lb_bpf__open_and_load();
    if (!skel) {
        fprintf(stderr, "failed to open/load skeleton\n");
        return 1;
    }

    cfg_fd = bpf_map__fd(skel->maps.config_map);
    backends_fd = bpf_map__fd(skel->maps.backends);

    if (bpf_map_update_elem(cfg_fd, &key0, &cfg, BPF_ANY) != 0) {
        perror("bpf_map_update_elem(config_map)");
        xdp_lb_bpf__destroy(skel);
        return 1;
    }

    inet_pton(AF_INET, "10.0.0.21", &be0.ip);
    be0.port = htons(80);
    be0.valid = 1;

    inet_pton(AF_INET, "10.0.0.22", &be1.ip);
    be1.port = htons(80);
    be1.valid = 1;

    if (bpf_map_update_elem(backends_fd, &key0, &be0, BPF_ANY) != 0 ||
        bpf_map_update_elem(backends_fd, &key1, &be1, BPF_ANY) != 0) {
        perror("bpf_map_update_elem(backends)");
        xdp_lb_bpf__destroy(skel);
        return 1;
    }

    prog_fd = bpf_program__fd(skel->progs.xdp_lb);
    err = bpf_set_link_xdp_fd(ifindex, prog_fd, XDP_FLAGS_SKB_MODE);
    if (err) {
        fprintf(stderr, "bpf_set_link_xdp_fd attach failed: %d\n", err);
        xdp_lb_bpf__destroy(skel);
        return 1;
    }

    rb = ring_buffer__new(bpf_map__fd(skel->maps.events), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "failed to create ring buffer\n");
        bpf_set_link_xdp_fd(ifindex, -1, XDP_FLAGS_SKB_MODE);
        xdp_lb_bpf__destroy(skel);
        return 1;
    }

    printf("Attached XDP LB skeleton to %s (ifindex=%d)\n", argv[1], ifindex);
    printf("VIP=%s:%s proto=%s action=%s backends=[10.0.0.21:80, 10.0.0.22:80]\n",
           argv[2], argv[3], argv[4], argv[5]);
    printf("Waiting for matching packets... Press Ctrl-C to stop.\n");

    while (!exiting) {
        err = ring_buffer__poll(rb, 200);
        if (err < 0) {
            fprintf(stderr, "ring_buffer__poll failed: %d\n", err);
            break;
        }
    }

    ring_buffer__free(rb);
    bpf_set_link_xdp_fd(ifindex, -1, XDP_FLAGS_SKB_MODE);
    xdp_lb_bpf__destroy(skel);
    return err < 0 ? 1 : 0;
}