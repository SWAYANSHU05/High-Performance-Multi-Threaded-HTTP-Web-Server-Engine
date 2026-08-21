# High-Performance Multi-Threaded HTTP Web Server Engine

A modern C++ HTTP/1.1 server built from the socket layer up. The project combines non-blocking TCP networking, a fixed worker-thread pool, bounded request parsing, RAII resource management, and graceful shutdown into one compact systems-programming project.

It is designed to make the mechanics of a web server visible: connections arrive through a non-blocking listener, workers process them from a synchronized queue, and every request produces a standards-shaped HTTP response.

## Highlights

- Non-blocking TCP sockets with `poll`/`WSAPoll`
- POSIX socket support on Linux and macOS
- Windows Winsock support with MinGW-w64
- Fixed-size thread pool with no thread-per-request overhead
- Thread-safe work queue using `std::mutex`, `std::queue`, and `std::condition_variable`
- Atomic server and pool shutdown state
- RAII ownership for client and listener sockets
- HTTP/1.1 request-line parsing for `GET` and `POST`
- `Content-Length`-aware request-body handling
- Bounded request size to avoid unbounded memory growth
- `200`, `400`, and `404` HTTP responses
- Per-request worker and byte-count metrics
- Graceful `Ctrl+C` shutdown

## Repository

**Repository name:**

```text
High-Performance-Multi-Threaded-HTTP-Web-Server-Engine
```

**GitHub description:**

```text
A modern C++ HTTP/1.1 server using raw non-blocking sockets, a custom thread pool, and RAII resource management.
```

## Quick Start: Windows

### Requirements

- Windows 10 or newer
- MinGW-w64 with `g++`
- C++17 or newer; C++20 recommended
- `curl` for testing

Open Command Prompt or PowerShell in the project directory and compile:

```powershell
g++ -DLOCAL -std=gnu++20 -Wall -Wextra -pedantic "Web Server.cpp" -lws2_32 -o "Web Server.exe"
```

Start the server in PowerShell:

```powershell
& ".\Web Server.exe"
```

Start it in Command Prompt:

```cmd
".\Web Server.exe"
```

The quotes are required because the filename contains spaces. The server listens at:

```text
http://127.0.0.1:8080
```

## Quick Start: Linux and macOS

```bash
g++ -std=c++20 -Wall -Wextra -pedantic "Web Server.cpp" -pthread -o web-server
./web-server
```

## Try the Server

Keep the server terminal open. Use a second terminal to send requests.

### Root endpoint

```bash
curl http://127.0.0.1:8080/
```

Response:

```text
High-performance C++ HTTP server is running
```

### Health endpoint

```bash
curl http://127.0.0.1:8080/health
```

Response:

```text
OK
```

### POST echo endpoint

```bash
curl -X POST -d "hello server" http://127.0.0.1:8080/echo
```

Response:

```text
hello server
```

### Not-found response

Use `-i` to display the HTTP status and headers:

```bash
curl -i http://127.0.0.1:8080/missing
```

Response begins with:

```text
HTTP/1.1 404 Not Found
```

### Stop the server

Press `Ctrl+C` in the server terminal. A clean shutdown ends with:

```text
Server stopped.
```

## HTTP API

| Method | Path | Response |
| --- | --- | --- |
| `GET` | `/` | Server status message |
| `GET` | `/health` | `200 OK` and `OK` |
| `POST` | `/echo` | `200 OK` and the request body |
| `GET`, `POST` | Any unknown path | `404 Not Found` |
| Unsupported method or HTTP version | Any path | `400 Bad Request` |

Every response includes `Content-Type`, `Content-Length`, and `Connection: close` headers.

## How It Works

```text
Non-blocking listener
        |
        v
   poll / WSAPoll
        |
        v
  accept new clients
        |
        v
 synchronized job queue
        |
        v
   worker thread pool
        |
        v
 parse request -> create response -> send response
```

1. The listener is configured as non-blocking and waits for connections with `poll` or `WSAPoll`.
2. The main thread accepts ready clients and places their socket handles into a synchronized queue.
3. Worker threads sleep on a condition variable until work arrives.
4. A worker reads the request with a timeout and a maximum request-size limit.
5. The HTTP request line and `Content-Length` header are parsed.
6. The route handler creates a response with standard HTTP headers.
7. The response is sent through the non-blocking client socket.
8. RAII closes the client socket, while the thread pool joins all workers during shutdown.

## Runtime Metrics

The server prints the worker thread and request size after each completed request:

```text
thread=6 bytes=141
thread=4 bytes=208
```

Different thread IDs show that requests are being distributed through the worker pool instead of creating a new operating-system thread for every connection.

## Project Structure

```text
.
├── Web Server.cpp       # Server implementation
├── README.md            # Project documentation
├── .gitignore           # Local build and test exclusions
└── .vscode/
    └── tasks.json       # VS Code build and run tasks
```

## Design Goals

- Keep the networking path explicit and easy to inspect.
- Avoid blocking the accept loop on slow clients.
- Reuse worker threads rather than spawning per request.
- Make socket ownership and cleanup automatic.
- Keep the implementation small enough to study and extend.

## Current Scope

This is a focused systems-programming server and a strong foundation for learning network services. It is not yet a drop-in replacement for a hardened production server such as nginx, Apache, or a mature C++ networking framework.

Not currently included:

- TLS/HTTPS
- HTTP keep-alive
- Chunked transfer encoding
- Static-file serving
- Request routing configuration
- Authentication and authorization
- Rate limiting
- Access logging and metrics export
- Persistent storage

Possible next extensions include a configurable port, static-file responses, keep-alive connections, structured logging, a bounded queue, and TLS through a dedicated library.

## License

No license has been selected yet. Add a license before accepting external contributions or distributing the project publicly.
