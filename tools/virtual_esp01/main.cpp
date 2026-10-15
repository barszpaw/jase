// virtual_esp01: a standalone process that pretends to be an ESP-01
// running ESP-AT firmware, so JASE's ZiFi peripheral (a plain TCP client,
// see src/ZiFi.h) can test real networking without real hardware.
//
// Deliberately a SEPARATE binary, not linked into JASE: no code is copied
// from pico-spec's ZiFi.cpp (GPLv3) — this implements Espressif's own
// PUBLICLY documented ESP-AT command set (a published firmware command
// reference, not pico-spec's invention) from scratch. Being a separate
// process also means JASE's own MIT license is never in question here
// regardless of how this file is written.
//
// Listens on 127.0.0.1:<port> (default 9100). One client at a time (the
// single real UART a real ESP-01 has). Minimal command subset: AT,
// AT+CIPSTART="TCP",host,port, AT+CIPSEND=<len>, AT+CIPCLOSE. Incoming
// bytes on the real remote connection are pushed to the client as
// +IPD,<len>:<data>, matching real ESP-AT's single-connection framing.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

static int remoteFd = -1; // the "real" TCP connection AT+CIPSTART opened

static void sendLine(int fd, const char* s)
{
	send(fd, s, strlen(s), 0);
}

// Parses AT+CIPSTART="TCP","host",port (quotes optional, whitespace
// tolerant) — real ESP-AT accepts both quoted and unquoted forms.
static bool parseCipStart(const std::string& args, std::string& host, int& port)
{
	size_t p1 = args.find(',');
	if (p1 == std::string::npos) return false;
	size_t p2 = args.find(',', p1 + 1);
	if (p2 == std::string::npos) return false;

	host = args.substr(p1 + 1, p2 - p1 - 1);
	// strip quotes
	if (!host.empty() && host.front() == '"') host.erase(0, 1);
	if (!host.empty() && host.back() == '"') host.pop_back();

	std::string portStr = args.substr(p2 + 1);
	port = atoi(portStr.c_str());
	return port > 0;
}

static void handleCommand(int clientFd, const std::string& line, std::string& lineBuf)
{
	std::string cmd = line;
	while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == '\n')) cmd.pop_back();

	printf("virtual_esp01: <- %s\n", cmd.c_str());

	if (cmd == "AT") {
		sendLine(clientFd, "OK\r\n");
	}
	else if (cmd.rfind("AT+CWMODE", 0) == 0 || cmd.rfind("AT+CWJAP", 0) == 0) {
		// No real WiFi join needed — the host already has a network.
		sendLine(clientFd, "OK\r\n");
	}
	else if (cmd.rfind("AT+CIPSTART=", 0) == 0) {
		std::string host; int port;
		if (!parseCipStart(cmd.substr(12), host, port)) {
			sendLine(clientFd, "ERROR\r\n");
			return;
		}
		if (remoteFd >= 0) { close(remoteFd); remoteFd = -1; }

		// getaddrinfo (not inet_pton) — AT+CIPSTART takes hostnames, not
		// just numeric IPs, same as real ESP-AT.
		char portStr[16];
		snprintf(portStr, sizeof(portStr), "%d", port);
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* res = nullptr;
		int fd = -1;
		bool ok = false;
		if (getaddrinfo(host.c_str(), portStr, &hints, &res) == 0 && res) {
			fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
			ok = fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0;
			freeaddrinfo(res);
		}
		if (ok) {
			int one = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			fcntl(fd, F_SETFL, O_NONBLOCK);
			remoteFd = fd;
			printf("virtual_esp01: connected to %s:%d\n", host.c_str(), port);
			sendLine(clientFd, "CONNECT\r\n\r\nOK\r\n");
		} else {
			close(fd);
			sendLine(clientFd, "ERROR\r\n");
		}
	}
	else if (cmd.rfind("AT+CIPSEND=", 0) == 0) {
		int len = atoi(cmd.c_str() + 11);
		if (len <= 0 || remoteFd < 0) { sendLine(clientFd, "ERROR\r\n"); return; }
		sendLine(clientFd, "\r\n> ");
		// Real ESP-AT switches to raw-byte mode here. The payload may
		// have arrived in the SAME recv() chunk as this command line (nc/
		// a guest not pacing writes) and already be sitting in lineBuf —
		// take that first before blocking on more from the socket.
		std::string data;
		data.reserve(len);
		if (!lineBuf.empty()) {
			size_t take = lineBuf.size() < (size_t)len ? lineBuf.size() : (size_t)len;
			data.append(lineBuf, 0, take);
			lineBuf.erase(0, take);
		}
		while (data.size() < (size_t)len) {
			char buf[512];
			size_t want = (size_t)len - data.size();
			if (want > sizeof(buf)) want = sizeof(buf);
			ssize_t n = recv(clientFd, buf, want, 0);
			if (n <= 0) { sendLine(clientFd, "ERROR\r\n"); return; }
			data.append(buf, n);
		}
		send(remoteFd, data.data(), data.size(), 0);
		sendLine(clientFd, "\r\nSEND OK\r\n");
	}
	else if (cmd == "AT+CIPCLOSE") {
		if (remoteFd >= 0) { close(remoteFd); remoteFd = -1; }
		sendLine(clientFd, "CLOSED\r\n\r\nOK\r\n");
	}
	else {
		sendLine(clientFd, "ERROR\r\n");
	}
}

int main(int argc, char* argv[])
{
	setvbuf(stdout, nullptr, _IOLBF, 0); // line-buffered even when redirected to a file
	int port = argc > 1 ? atoi(argv[1]) : 9100;

	int listenFd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	addr.sin_port = htons((uint16_t)port);

	if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}
	listen(listenFd, 1);
	printf("virtual_esp01: listening on 127.0.0.1:%d\n", port);

	for (;;) {
		int clientFd = accept(listenFd, nullptr, nullptr);
		if (clientFd < 0) continue;
		printf("virtual_esp01: client connected\n");

		std::string lineBuf;
		for (;;) {
			pollfd fds[2];
			int nfds = 1;
			fds[0].fd = clientFd; fds[0].events = POLLIN;
			if (remoteFd >= 0) { fds[1].fd = remoteFd; fds[1].events = POLLIN; nfds = 2; }

			if (poll(fds, nfds, 1000) <= 0) continue;

			if (fds[0].revents & POLLIN) {
				char buf[512];
				ssize_t n = recv(clientFd, buf, sizeof(buf), 0);
				if (n <= 0) break; // client disconnected
				lineBuf.append(buf, n);
				size_t pos;
				while ((pos = lineBuf.find('\n')) != std::string::npos) {
					std::string line = lineBuf.substr(0, pos + 1);
					lineBuf.erase(0, pos + 1);
					if (line.size() > 1 || line[0] != '\n') // skip bare blank lines
						handleCommand(clientFd, line, lineBuf);
				}
			}
			if (nfds == 2 && (fds[1].revents & POLLIN)) {
				char buf[512];
				ssize_t n = recv(remoteFd, buf, sizeof(buf), 0);
				if (n <= 0) {
					close(remoteFd);
					remoteFd = -1;
				} else {
					char header[32];
					snprintf(header, sizeof(header), "+IPD,%zd:", n);
					send(clientFd, header, strlen(header), 0);
					send(clientFd, buf, n, 0);
				}
			}
		}

		printf("virtual_esp01: client disconnected\n");
		close(clientFd);
		if (remoteFd >= 0) { close(remoteFd); remoteFd = -1; }
	}
}
