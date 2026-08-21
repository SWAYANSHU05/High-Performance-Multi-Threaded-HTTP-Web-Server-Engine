# SocketForge HTTP Server

A lightweight, high-performance HTTP/1.1 server written in modern C++ using raw sockets and a custom thread pool.

This project is built to demonstrate how an HTTP server works close to the operating system: non-blocking sockets, polling, worker-thread coordination, request parsing, response construction, and graceful shutdown.

## Features

- Non-blocking TCP sockets
- POSIX networking on Linux and macOS
- Windows Winsock support through MinGW
- Fixed-size worker thread pool using `std::thread`
- Thread-safe job queue with `std::mutex` and `std::condition_variable`
- Atomic shutdown state
- RAII socket/file-descriptor ownership
- HTTP/1.1 request-line parsing
- `GET` and `POST` support
- `Content-Length` request-body handling
- Clean `200`, `400`, `404`, and shutdown behavior
- Request metrics showing worker thread IDs and request sizes
- Graceful `Ctrl+C` shutdown

## Repository Name

Recommended name:

```text
socketforge-http-server
```

## Repository Description

```text
A high-performance multithreaded HTTP/1.1 server in modern C++ using raw non-blocking sockets and a custom thread pool.
```

## Requirements

- C++17 or newer
- C++20 recommended
- GCC or Clang
- Windows: MinGW-w64 with Winsock2
- Linux/macOS: POSIX socket development environment

## Build

### Windows with MinGW-w64

Run from the repository directory:

```powershell
g++ -DLOCAL -std=gnu++20 -Wall -Wextra -pedantic "Web Server.cpp" -lws2_32 -o "Web Server.exe"
```

Run the server in PowerShell:

```powershell
& ".\Web Server.exe"
```

Run the server in Command Prompt:

```cmd
".\Web Server.exe"
```

The quotes are required because the source and executable names contain spaces.

### Linux or macOS

```bash
g++ -std=c++20 -Wall -Wextra -pedantic "Web Server.cpp" -pthread -o web-server
./web-server
```

The server listens on:

```text
http://127.0.0.1:8080
```

## Try It

Keep the server terminal open and use a second terminal for requests.

### Health check

```bash
curl http://127.0.0.1:8080/health
```

Expected response:

```text
OK
```

### Root route

```bash
curl http://127.0.0.1:8080/
```

Expected response:

```text
High-performance C++ HTTP server is running
```

### POST echo

```bash
curl -X POST -d "hello server" http://127.0.0.1:8080/echo
```

Expected response:

```text
hello server
```

### Missing route

```bash
curl -i http://127.0.0.1:8080/missing
```

Expected status:

```text
HTTP/1.1 404 Not Found
```

### Graceful shutdown

Press `Ctrl+C` in the server terminal. The server should print:

```text
Server stopped.
```

## Endpoints

| Method | Path | Result |
|---|---|---|
| `GET` | `/` | Server status message |
| `GET` | `/health` | `OK` health response |
| `POST` | `/echo` | Returns the request body |
| `GET` or `POST` | Any other path | `404 Not Found` |
| Unsupported method/version | Any path | `400 Bad Request` |

## Architecture

1. The listening socket is configured as non-blocking.
2. The main thread polls for incoming connections.
3. Accepted clients are placed into a synchronized worker queue.
4. Worker threads wait efficiently on a condition variable.
5. Each worker reads a bounded HTTP request using polling.
6. The request is parsed and converted into an HTTP response.
7. The response is sent through the non-blocking client socket.
8. RAII closes the client and listener sockets automatically.

## Example Metrics

The server logs the worker that processed each request:

```text
thread=6 bytes=141
thread=4 bytes=208
```

Multiple thread IDs demonstrate that requests are being handled by the worker pool rather than by creating a new thread for every connection.

## Scope and Limitations

This is a focused HTTP server implementation for systems programming and networking practice. It intentionally does not attempt to replace a mature production server such as nginx or Apache.

Not currently included:

- TLS/HTTPS
- HTTP keep-alive connections
- Chunked transfer encoding
- Static-file serving
- Virtual hosts
- Authentication and access control
- Request routing frameworks
- Persistent application storage

These are natural next steps for extending the project.

## Project Structure

```text
.
├── Web Server.cpp       # HTTP server implementation
├── .vscode/tasks.json   # VS Code build/run tasks
└── README.md            # Project documentation
```

## License

No license has been selected yet. Add a license before accepting external contributions or distributing the project publicly.
