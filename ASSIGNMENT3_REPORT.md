## Question 1 - Concurrent Web Server in Linux

### Requirement
Q1 required a simple concurrent web server in Linux based on the supplied `server.c`.
The server had to process HTTP GET requests, compile with:

`gcc -o server server.c -lpthread`

and accept connections from telnet or a web browser on port 3000.

### My solution
I implemented a thread-per-connection web server in C.

The server:
- creates a TCP listening socket on port 3000,
- accepts incoming client connections,
- creates a new POSIX thread for each client,
- reads the HTTP request,
- parses the request line,
- accepts only the `GET` method,
- maps the requested URI to a file in the `www` directory,
- sends either:
  - `200 OK` with the file contents, or
  - `404 Not Found` if the file does not exist.

### Design
The main thread creates the listening socket using `socket()`, enables `SO_REUSEADDR`, binds to port 3000 with `bind()`, and waits for connections using `listen()` and `accept()`.

When a client connects, the server allocates heap memory for a copy of the client socket descriptor and passes it to a new thread using `pthread_create()`.

Each worker thread:
1. copies the socket descriptor,
2. frees the heap argument,
3. receives the request using `recv()`,
4. terminates the request buffer with `\0`,
5. checks for the end of the HTTP header (`\r\n\r\n`),
6. parses the request into method, URI, and version,
7. opens the requested file,
8. sends a response,
9. closes the socket.

### Request parsing
The parser reads:
- method
- URI
- HTTP version

It uses width-limited `sscanf()` fields so fixed-size buffers are not overflowed.
It also rejects any method other than `GET`.

### File handling
The server maps:
- `/` to `www/index.html`
- `/hello.txt` to `www/hello.txt`

If the requested file exists, it is opened and returned to the client.
If it does not exist, the server returns a `404 Not Found` response.

The file response function:
- finds the file size using `fseek()` and `ftell()`,
- allocates a buffer,
- reads the file into memory,
- sends the response,
- frees the buffer,
- closes the file.

### HTTP response
The response function sends:
- the status line,
- the `Content-Length` header,
- a blank line,
- the body.

The server supports:
- `200 OK`
- `404 Not Found`
- `500 Internal Server Error`

### Memory and resource handling
To avoid leaks:
- the heap memory used to pass the socket descriptor to the thread is freed in the connection handler,
- each client socket is closed with `close(sock)`,
- opened files are closed with `fclose()`,
- allocated response buffers are released with `free()`,
- worker threads are detached with `pthread_detach()`.

### Build and run
The server was compiled with:

```bash
gcc -Wall -Wextra -O2 -o server src/server.c -lpthread
```

It was run with:

```bash
./server
```

### Testing
I tested the server with:

```bash
curl -i http://localhost:3000/
curl -i http://localhost:3000/hello.txt
curl -i http://localhost:3000/nope
```

Observed behaviour:
- `/` returned `200 OK` and `index.html`
- `/hello.txt` returned `200 OK` and `hello.txt`
- `/nope` returned `404 Not Found`

### Conclusion
The implementation satisfies the main Q1 requirements:
- it is a concurrent Linux web server,
- it handles HTTP GET requests,
- it accepts browser and telnet-style connections on port 3000,
- it serves files from the local `www` directory,
- and it returns valid minimal HTTP responses.

## Question 2 - Asynchronous I/O

### Requirement
Question 2 asked for:
- an explanation of the `select` and `epoll` I/O interfaces to the Linux kernel,
- a brief overview of `libuv` and how it provides an eventing model for Node.js using Linux kernel services,
- an explanation of `io_uring`, its benefits, and whether it presents a security risk.

### a) What are `select` and `epoll`?

`select` and `epoll` are Linux interfaces used to wait for I/O readiness on file descriptors such as sockets, pipes, and some device files. They do not perform the I/O themselves. Instead, they tell a process when a file descriptor is ready for operations such as `read()` or `write()`. This is the basis of event-driven servers and runtimes.

`select` is the older interface. A program passes sets of file descriptors to the kernel and asks which ones are ready for reading, writing, or exceptional conditions. The kernel blocks until one or more descriptors become ready or a timeout expires. The main disadvantages of `select` are that it must rebuild and rescan the descriptor sets on every call, and it is limited by `FD_SETSIZE`, so it does not scale well to large numbers of connections.

`epoll` is a newer Linux event notification mechanism designed for scalability. A program first creates an epoll instance, which itself is represented by a file descriptor. It then registers the file descriptors it wants to monitor using `epoll_ctl()`, and waits for events using `epoll_wait()`. `epoll` supports both level-triggered and edge-triggered modes. It is much more efficient than `select` when the server has to manage many concurrent connections.

In practice, `select` is useful for small and simple programs, while `epoll` is the standard Linux solution for scalable event-driven servers such as web servers and proxies.

### b) Overview of `libuv` and how it uses `epoll`

`libuv` is a cross-platform asynchronous I/O library originally developed for Node.js. It provides the event loop and the callback-driven model used by Node.js to offer non-blocking behaviour. It supports networking, timers, child processes, signals, asynchronous DNS, and background work.

From a Node.js programmer’s point of view, the code appears to be asynchronous because operations return quickly and the completion work is handled later by callbacks or promises. Underneath, `libuv` is responsible for connecting this model to the operating system.

On Linux, `libuv` uses non-blocking sockets together with `epoll` for network I/O. The event loop registers sockets with an epoll instance, waits for readiness events, and then dispatches the corresponding callback when the kernel reports that a socket is ready to read or write. This is the kernel-facing mechanism that supports the Node.js event-driven model for networking.

Not all asynchronous operations in Node.js are handled directly through kernel readiness notification. File system operations and some DNS-related work are handled by the `libuv` thread pool because these operations are not uniformly available as non-blocking kernel events across all supported platforms. As a result, `libuv` combines two main strategies:
- `epoll` for socket readiness and other event-loop driven I/O on Linux,
- a worker thread pool for operations that would otherwise block.

Therefore, for Linux networking the path is:

Node.js application → Node runtime → libuv event loop → epoll → callback execution in the event loop.

This design allows Node.js to remain highly responsive even though JavaScript application code usually runs in a single main thread.

### c) What is `io_uring`? What are its benefits? Is it a security risk?

`io_uring` is a Linux asynchronous I/O interface based on shared ring buffers between user space and kernel space. It uses:
- a submission queue (SQ) for requests,
- a completion queue (CQ) for results.

Instead of relying only on repeated system calls for each operation, user space and the kernel share queue structures. This reduces overhead and allows applications to submit and complete many operations efficiently.

The main benefits of `io_uring` are:
- lower syscall overhead,
- efficient batching of operations,
- improved throughput and latency,
- support for a wide range of I/O-related operations,
- a more flexible and modern interface than older Linux asynchronous I/O facilities.

These features make `io_uring` attractive for high-performance storage and network applications.

However, `io_uring` has also raised important security concerns. Because it is a relatively new and complex kernel subsystem, it has been associated with multiple serious vulnerabilities. It increases kernel attack surface, and some organisations have restricted or disabled it in environments that run untrusted code. For this reason, `io_uring` is often described as powerful and high-performance, but also security-sensitive.

So the balanced answer is:
- `io_uring` provides major performance and flexibility benefits,
- but it has also been considered a security risk because of repeated vulnerabilities and the size and complexity of the kernel interface it exposes.

### Conclusion
`select`, `epoll`, `libuv`, and `io_uring` all relate to asynchronous or event-driven I/O, but they operate at different layers.

- `select` and `epoll` are Linux kernel interfaces for waiting on I/O readiness.
- `epoll` is much better suited to large-scale servers.
- `libuv` uses mechanisms such as `epoll` on Linux to provide the event loop and eventing model used by Node.js.
- `io_uring` is a newer Linux asynchronous I/O interface that offers strong performance benefits, but it must be considered carefully because of its security implications.