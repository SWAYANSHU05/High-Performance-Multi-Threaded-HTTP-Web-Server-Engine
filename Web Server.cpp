// High-performance HTTP/1.1 server using POSIX sockets and a fixed thread pool.

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <csignal>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <mutex>
#ifndef _WIN32
#include <netinet/in.h>
#endif
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <algorithm>
#include <atomic>

namespace {

constexpr int kPort = 8080;
constexpr int kPollTimeoutMs = 5000;
constexpr std::size_t kMaxRequestBytes = 1024 * 1024;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void close_socket(Socket socket) noexcept { ::closesocket(socket); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void close_socket(Socket socket) noexcept { ::close(socket); }
#endif

std::atomic<bool> running{true};

void stop_server(int) noexcept {
	running.store(false, std::memory_order_relaxed);
}

class FileDescriptor {
public:
	explicit FileDescriptor(Socket value = kInvalidSocket) noexcept : value_(value) {}
	FileDescriptor(const FileDescriptor&) = delete;
	FileDescriptor& operator=(const FileDescriptor&) = delete;
	FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) {
		other.value_ = kInvalidSocket;
	}
	FileDescriptor& operator=(FileDescriptor&& other) noexcept {
		if (this != &other) {
			reset(other.release());
		}
		return *this;
	}
	~FileDescriptor() { reset(); }

	Socket get() const noexcept { return value_; }
	Socket release() noexcept {
		const Socket result = value_;
		value_ = kInvalidSocket;
		return result;
	}
	void reset(Socket value = kInvalidSocket) noexcept {
		if (value_ != kInvalidSocket) {
			close_socket(value_);
		}
		value_ = value;
	}

private:
	Socket value_;
};

void set_non_blocking(Socket descriptor) {
#ifdef _WIN32
	u_long enabled = 1;
	if (::ioctlsocket(descriptor, FIONBIO, &enabled) != 0) {
		throw std::runtime_error("ioctlsocket: " + std::to_string(::WSAGetLastError()));
	}
#else
	const int flags = ::fcntl(descriptor, F_GETFL, 0);
	if (flags == -1 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == -1) {
		throw std::runtime_error("fcntl(O_NONBLOCK): " + std::string(std::strerror(errno)));
	}
#endif
}

bool socket_would_block() noexcept {
#ifdef _WIN32
	const int error = ::WSAGetLastError();
	return error == WSAEWOULDBLOCK;
#else
	return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

bool socket_interrupted() noexcept {
#ifdef _WIN32
	return ::WSAGetLastError() == WSAEINTR;
#else
	return errno == EINTR;
#endif
}

std::string socket_error_message() {
#ifdef _WIN32
	return std::to_string(::WSAGetLastError());
#else
	return std::strerror(errno);
#endif
}

class ThreadPool {
public:
	explicit ThreadPool(std::size_t thread_count) {
		if (thread_count == 0) {
			thread_count = 1;
		}
		workers_.reserve(thread_count);
		for (std::size_t i = 0; i < thread_count; ++i) {
			workers_.emplace_back([this] { worker_loop(); });
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	~ThreadPool() {
		{
			std::lock_guard<std::mutex> lock(queue_mutex_);
			stopping_.store(true, std::memory_order_relaxed);
		}
		queue_condition_.notify_all();
		for (std::thread& worker : workers_) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}

	void submit(Socket client) {
		{
			std::lock_guard<std::mutex> lock(queue_mutex_);
			if (stopping_.load(std::memory_order_relaxed)) {
				close_socket(client);
				return;
			}
			jobs_.push(client);
		}
		queue_condition_.notify_one();
	}

private:
	void worker_loop() {
		while (true) {
			Socket client = kInvalidSocket;
			{
				std::unique_lock<std::mutex> lock(queue_mutex_);
				queue_condition_.wait(lock, [this] {
					return stopping_.load(std::memory_order_relaxed) || !jobs_.empty();
				});
				if (jobs_.empty()) {
					return;
				}
				client = jobs_.front();
				jobs_.pop();
			}
			handle_client(FileDescriptor(client));
		}
	}

	static void handle_client(FileDescriptor client) {
		std::string request;
		char buffer[8192];
		std::size_t header_end = std::string::npos;
		while ((header_end = request.find("\r\n\r\n")) == std::string::npos &&
			   request.size() < kMaxRequestBytes) {
#ifdef _WIN32
			WSAPOLLFD descriptor{client.get(), POLLRDNORM, 0};
#else
			pollfd descriptor{client.get(), POLLIN, 0};
#endif
#ifdef _WIN32
			const int ready = ::WSAPoll(&descriptor, 1, kPollTimeoutMs);
			if (ready <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
#else
			const int ready = ::poll(&descriptor, 1, kPollTimeoutMs);
			if (ready <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
#endif
				return;
			}
			const ssize_t received = ::recv(client.get(), buffer, sizeof(buffer), 0);
			if (received > 0) {
				request.append(buffer, static_cast<std::size_t>(received));
			} else if (received == 0 || (!socket_would_block() && !socket_interrupted())) {
				return;
			}
		}
		if (header_end == std::string::npos || request.size() > kMaxRequestBytes) {
			return;
		}

		const std::size_t body_start = header_end + 4;
		std::size_t content_length = 0;
		const std::string headers = request.substr(0, header_end);
		const std::string content_length_prefix = "Content-Length:";
		const std::size_t content_length_position = headers.find(content_length_prefix);
		if (content_length_position != std::string::npos) {
			try {
				content_length = std::stoull(headers.substr(
					content_length_position + content_length_prefix.size()));
			} catch (const std::exception&) {
				return;
			}
		}
		if (body_start + content_length > kMaxRequestBytes) {
			return;
		}
		while (request.size() < body_start + content_length) {
#ifdef _WIN32
			WSAPOLLFD descriptor{client.get(), POLLRDNORM, 0};
			const int ready = ::WSAPoll(&descriptor, 1, kPollTimeoutMs);
#else
			pollfd descriptor{client.get(), POLLIN, 0};
			const int ready = ::poll(&descriptor, 1, kPollTimeoutMs);
#endif
			if (ready <= 0) {
				return;
			}
			const ssize_t received = ::recv(client.get(), buffer, sizeof(buffer), 0);
			if (received > 0) {
				request.append(buffer, static_cast<std::size_t>(received));
			} else if (received == 0 || (!socket_would_block() && !socket_interrupted())) {
				return;
			}
		}

		const std::string response = make_response(request);
		std::size_t sent = 0;
		while (sent < response.size()) {
#ifdef _WIN32
			WSAPOLLFD descriptor{client.get(), POLLWRNORM, 0};
			if (::WSAPoll(&descriptor, 1, kPollTimeoutMs) <= 0) {
#else
			pollfd descriptor{client.get(), POLLOUT, 0};
			if (::poll(&descriptor, 1, kPollTimeoutMs) <= 0) {
#endif
				return;
			}
#ifdef _WIN32
			const int count = ::send(client.get(), response.data() + sent,
									 static_cast<int>(response.size() - sent), 0);
#else
			const ssize_t count = ::send(client.get(), response.data() + sent,
										 response.size() - sent, MSG_NOSIGNAL);
#endif
			if (count > 0) {
				sent += static_cast<std::size_t>(count);
			} else if (count < 0 && (socket_would_block() || socket_interrupted())) {
				continue;
			} else {
				return;
			}
		}

		std::cout << "thread=" << std::this_thread::get_id()
				  << " bytes=" << request.size() << '\n';
	}

	static std::string make_response(std::string_view request) {
		const std::size_t line_end = request.find("\r\n");
		if (line_end == std::string_view::npos) {
			return response("400 Bad Request", "Malformed HTTP request");
		}

		std::istringstream request_line{std::string(request.substr(0, line_end))};
		std::string method;
		std::string target;
		std::string version;
		request_line >> method >> target >> version;
		if (version != "HTTP/1.1" || (method != "GET" && method != "POST")) {
			return response("400 Bad Request", "Only HTTP/1.1 GET and POST are supported");
		}
		if (target == "/") {
			return response("200 OK", "High-performance C++ HTTP server is running\n");
		}
		if (target == "/health") {
			return response("200 OK", "OK\n");
		}
		if (method == "POST" && target == "/echo") {
			const std::size_t body_start = request.find("\r\n\r\n");
			return response("200 OK", std::string(request.substr(body_start + 4)));
		}
		return response("404 Not Found", "Not Found\n");
	}

	static std::string response(const std::string& status, const std::string& body) {
		return "HTTP/1.1 " + status + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
			   std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
	}

	std::vector<std::thread> workers_;
	std::queue<Socket> jobs_;
	std::mutex queue_mutex_;
	std::condition_variable queue_condition_;
	std::atomic<bool> stopping_{false};
};

}  // namespace

int main() {
	std::signal(SIGINT, stop_server);
	std::signal(SIGTERM, stop_server);

	try {
#ifdef _WIN32
		WSADATA winsock_data{};
		if (::WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
			throw std::runtime_error("WSAStartup failed");
		}
#endif
		FileDescriptor listener(::socket(AF_INET, SOCK_STREAM, 0));
		if (listener.get() == kInvalidSocket) {
			throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
		}

		int reuse = 1;
	#ifdef _WIN32
		::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
			     reinterpret_cast<const char*>(&reuse), sizeof(reuse));
	#else
		::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	#endif
		set_non_blocking(listener.get());

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		address.sin_port = htons(kPort);
		if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 ||
			::listen(listener.get(), SOMAXCONN) == -1) {
			throw std::runtime_error("bind/listen: " + socket_error_message());
		}

		const std::size_t thread_count = std::max(1u, std::thread::hardware_concurrency());
		ThreadPool pool(thread_count);
		std::cout << "Listening on http://127.0.0.1:" << kPort
				  << " with " << thread_count << " workers\n";

		while (running.load(std::memory_order_relaxed)) {
#ifdef _WIN32
			WSAPOLLFD descriptor{listener.get(), POLLRDNORM, 0};
			if (::WSAPoll(&descriptor, 1, 250) < 0) {
#else
			pollfd descriptor{listener.get(), POLLIN, 0};
			if (::poll(&descriptor, 1, 250) < 0) {
#endif
				if (errno == EINTR) {
					continue;
				}
				throw std::runtime_error("poll: " + std::string(std::strerror(errno)));
			}
			if (!(descriptor.revents & POLLIN)) {
				continue;
			}
			while (true) {
				const Socket client = ::accept(listener.get(), nullptr, nullptr);
				if (client == kInvalidSocket) {
				#ifdef _WIN32
					const int error = ::WSAGetLastError();
					if (error == 0 || error == WSAEWOULDBLOCK || error == WSAEINTR ||
						error == WSAECONNABORTED || error == WSAECONNRESET) {
				#else
					if (socket_would_block() || socket_interrupted()) {
				#endif
						break;
					}
					throw std::runtime_error("accept: " + socket_error_message());
				}
				try {
					set_non_blocking(client);
					pool.submit(client);
				} catch (...) {
					close_socket(client);
				}
			}
		}
		std::cout << "Server stopped.\n";
	} catch (const std::exception& error) {
		std::cerr << "Fatal error: " << error.what() << '\n';
#ifdef _WIN32
		::WSACleanup();
#endif
		return 1;
	}
#ifdef _WIN32
	::WSACleanup();
#endif
	return 0;
}
