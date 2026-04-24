#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char LICENSE[] SEC("license") = "GPL";

#define MAX_BACKENDS 8

struct service_config {
    __u32 vip;      /* network byte order */
    __u16 vport;    /* network byte order */
    __u8 proto;     /* IPPROTO_TCP or IPPROTO_UDP */
    __u8 action;    /* 0 = PASS, 1 = DROP */
};

struct backend {
    __u32 ip;       /* network byte order */
    __u16 port;     /* network byte order */
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

enum {
    ACT_PASS = 0,
    ACT_DROP = 1,
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct service_config);
} config_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_BACKENDS);
    __type(key, __u32);
    __type(value, struct backend);
} backends SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 20);
} events SEC(".maps");

SEC("xdp")
int xdp_lb(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    struct iphdr *iph;
    __u32 zero = 0;
    struct service_config *cfg;
    __u16 dst_port = 0, src_port = 0;
    __u8 proto;
    __u32 backend_idx = 0;
    struct backend *be = 0;
    struct event *e;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    if (iph->ihl < 5)
        return XDP_PASS;

    if ((void *)iph + iph->ihl * 4 > data_end)
        return XDP_PASS;

    proto = iph->protocol;

    if (proto == IPPROTO_TCP) {
        struct tcphdr *tcph = (void *)iph + iph->ihl * 4;
        if ((void *)(tcph + 1) > data_end)
            return XDP_PASS;
        src_port = tcph->source;
        dst_port = tcph->dest;
    } else if (proto == IPPROTO_UDP) {
        struct udphdr *udph = (void *)iph + iph->ihl * 4;
        if ((void *)(udph + 1) > data_end)
            return XDP_PASS;
        src_port = udph->source;
        dst_port = udph->dest;
    } else {
        return XDP_PASS;
    }

    cfg = bpf_map_lookup_elem(&config_map, &zero);
    if (!cfg)
        return XDP_PASS;

    if (iph->daddr != cfg->vip)
        return XDP_PASS;
    if (dst_port != cfg->vport)
        return XDP_PASS;
    if (proto != cfg->proto)
        return XDP_PASS;

    {
        __u32 k0 = 0;
        __u32 k1 = 1;
        struct backend *b0 = bpf_map_lookup_elem(&backends, &k0);
        struct backend *b1 = bpf_map_lookup_elem(&backends, &k1);

        if (b0 && b0->valid) {
            backend_idx = 0;
            be = b0;
        } else if (b1 && b1->valid) {
            backend_idx = 1;
            be = b1;
        }
    }

    e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (e) {
        e->src_ip = iph->saddr;
        e->dst_ip = iph->daddr;
        e->src_port = src_port;
        e->dst_port = dst_port;
        e->proto = proto;
        e->action = cfg->action;
        e->backend_idx = be ? backend_idx : 255;
        e->backend_ip = be ? be->ip : 0;
        e->backend_port = be ? be->port : 0;
        bpf_ringbuf_submit(e, 0);
    }

    if (cfg->action == ACT_DROP)
        return XDP_DROP;

    return XDP_PASS;
}