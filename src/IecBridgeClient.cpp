#include "IecBridgeClient.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

IecBridgeClient::~IecBridgeClient()
{
	if (fd_ >= 0) ::close(fd_);
}

bool IecBridgeClient::connect(const char* host, int port)
{
	if (fd_ >= 0) {
		::close(fd_);
		fd_ = -1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		::close(fd);
		return false;
	}

	if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "IecBridgeClient: connect to %s:%d failed\n", host, port);
		::close(fd);
		return false;
	}

	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	fd_ = fd;
	printf("IecBridgeClient: connected to %s:%d\n", host, port);

	// The companion's own reset handshake (see c64iec_fs_tcp.py) sends a
	// "D:<hex>\n" (or blank) line immediately on accept(), unprompted --
	// drain it now so it doesn't get mistaken for the reply to the
	// first real command later.
	std::string banner;
	readLine(banner, 1000);
	return true;
}

bool IecBridgeClient::sendLine(const std::string& line)
{
	if (fd_ < 0) return false;
	std::string withNl = line + "\n";
	return send(fd_, withNl.data(), withNl.size(), 0) == (ssize_t)withNl.size();
}

bool IecBridgeClient::readLine(std::string& out, int timeoutMs)
{
	out.clear();
	if (fd_ < 0) return false;
	for (;;) {
		pollfd pfd{fd_, POLLIN, 0};
		if (poll(&pfd, 1, timeoutMs) <= 0) return false; // timeout or error
		char c;
		ssize_t n = recv(fd_, &c, 1, 0);
		if (n <= 0) return false; // peer closed
		if (c == '\n') return true;
		if (c != '\r') out += c;
	}
}

bool IecBridgeClient::readExact(uint8_t* buf, size_t n, int timeoutMs)
{
	if (fd_ < 0) return false;
	size_t got = 0;
	while (got < n) {
		pollfd pfd{fd_, POLLIN, 0};
		if (poll(&pfd, 1, timeoutMs) <= 0) return false;
		ssize_t r = recv(fd_, buf + got, n - got, 0);
		if (r <= 0) return false;
		got += (size_t)r;
	}
	return true;
}

bool IecBridgeClient::open(int channel, const std::string& name)
{
	if (fd_ < 0) return false;
	char hdr[32];
	snprintf(hdr, sizeof(hdr), "O,%X,", channel);
	sendLine(hdr + name);
	readLine(lastReply_);
	return lastReply_.rfind("OK", 0) == 0;
}

void IecBridgeClient::close(int channel)
{
	if (fd_ < 0) return;
	char cmd[16];
	snprintf(cmd, sizeof(cmd), "C,%X", channel);
	sendLine(cmd);
	readLine(lastReply_);
}

bool IecBridgeClient::write(int channel, const std::string& data, bool eoi)
{
	if (fd_ < 0) return false;
	char hdr[32];
	snprintf(hdr, sizeof(hdr), "W,%X,%zu,%c", channel, data.size(), eoi ? 'E' : 'C');
	sendLine(hdr);
	send(fd_, data.data(), data.size(), 0);
	readLine(lastReply_);
	return lastReply_.rfind("OK", 0) == 0;
}

bool IecBridgeClient::requestRead(int channel, int size, std::string& outData, bool& outEoi)
{
	outData.clear();
	outEoi = false;
	if (fd_ < 0) return false;

	char cmd[32];
	snprintf(cmd, sizeof(cmd), "R,%X,%d", channel, size);
	sendLine(cmd);

	std::string header;
	if (!readLine(header) || header.empty()) return false;
	char flag = header[0];
	size_t comma = header.find(',');
	if (comma == std::string::npos) return false;
	int n = atoi(header.c_str() + comma + 1);
	if (n <= 0) {
		outEoi = (flag == 'L' || flag == 'E');
		return true;
	}

	std::vector<uint8_t> buf(n);
	if (!readExact(buf.data(), (size_t)n)) return false;
	outData.assign((const char*)buf.data(), buf.size());
	outEoi = (flag == 'L');
	return true;
}
