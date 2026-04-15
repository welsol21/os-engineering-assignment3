

----------

Possible workflow: 
1. Run the server program from question 1

2. note the process id

3.
cp tcptop.bpf.c  mytcptop.bpf.c    
cp tcptop.c      mytcptop.c 
cp tcptop.h mytcptop.h
modify the new c files to include mytcptop.h 
tcptop provides flags as follows

4. assume process id of 6098
sudo ./mytcptop -p 6098 -S

5.
Get rid of the screen clearing code in mytcptop.c
when you now connect to the server you should see output associated with the received TCP packets.

6.
provide a hook into
SEC("kprobe/tcp_v4_do_rcv")

7.

the skb is available in this function.
use code like:
You will need to add an array into the traffic_t struct
__u8 *skb_data = NULL;
struct traffic_t zero;
	
	
bpf_probe_read_kernel(&skb_data, sizeof(skb_data), &skb->data);


bpf_probe_read_kernel(zero.data, 32, skb_data);
bpf_map_update_elem(&ip_map, &ip_key, &zero, BPF_NOEXIST);

6.
See below for more details
---------


# TCP Socket Layer & eBPF Tracepoints

## The Full Stack — Where Each Hook Lives

```
User Space
│
│   recv() / read() / recvmsg()
│        │
─────────┼──────────────────────────────────────────── syscall boundary
│        │
│   sock_recvmsg()
│        │
│   tcp_recvmsg()          ◄─── kprobe/tcp_recvmsg
│        │                       • sk, msg, len, flags
│        │                       • receive queue HAS data here
│        │                       • called in context of reading process
│        │
│   tcp_recv_skb()         ◄─── good for per-skb inspection
│        │                       • skb passed directly as arg
│        │
│   __skb_unlink()              skb removed from sk_receive_queue
│        │
│   tcp_cleanup_rbuf()     ◄─── what tcptop uses
│        │                       • copied bytes available
│        │                       • queue already drained
│        │
Kernel Space - Receive Path (softirq context)
│
│   netif_receive_skb()         NIC → kernel
│        │
│   ip_rcv()                    IP layer
│        │
│   ip_local_deliver()
│        │
│   tcp_v4_rcv()                TCP entry point
│        │
│   tcp_v4_do_rcv()        ◄─── kprobe/tcp_v4_do_rcv
│        │                       • sk, skb args
│        │                       • skb->data points to TCP header
│        │                       • fires in softirq, not process context
│        │                       • fires for ALL sockets incl. SYN etc
│        │
│        ├─[ESTABLISHED]──────────────────────────────────────
│        │                       
│   tcp_rcv_established()  ◄─── tracepoint/tcp/tcp_probe fires here
│        │                       • sequence, ack, snd_wnd, rcv_wnd
│        │                       • srtt, lost, retrans metrics too
│        │
│   tcp_queue_rcv()        ◄─── kprobe/tcp_queue_rcv
│        │                       • sk, skb, fragstolen args
│        │                       • skb being added to sk_receive_queue
│        │                       • payload is intact and accessible
│        │
│   sk_data_ready()             wakes up blocked recv() in user space
```

---





### `kprobe/tcp_v4_do_rcv` — All TCP traffic, headers visible
```
Signature: int tcp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)

Fires:   for ALL TCP packets including SYN/FIN/ACK
Context: softirq
skb:     data pointer at TCP header — you must skip it to get payload

  skb->data
       │
       ▼
  [TCP HEADER 20-60 bytes][PAYLOAD]
       │                   │
       └── doff field      └── what you want
           tells you
           header size
```

```c
SEC("kprobe/tcp_v4_do_rcv")
int BPF_KPROBE(tcp_v4_do_rcv, struct sock *sk, struct sk_buff *skb)
{
    __u8 *data = NULL;
    __u8  doff = 0;
    __u32 hdr_len = 0;
    __u8  buf[64] = {};

    bpf_probe_read_kernel(&data, sizeof(data), &skb->data);
    bpf_probe_read_kernel(&doff, sizeof(doff), data + 12); // data offset byte
    hdr_len = ((doff >> 4) & 0xF) * 4;

    bpf_probe_read_kernel(&buf, sizeof(buf), data + hdr_len); // skip to payload
}
```
