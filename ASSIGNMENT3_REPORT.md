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