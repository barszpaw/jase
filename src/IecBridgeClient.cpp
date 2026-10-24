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

	// sdi_bridge.py sends nothing unprompted on accept() -- unlike the
	// earlier fdemu-derived bridge, there's no banner line to drain here.
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
	snprintf(hdr, sizeof(hdr), "OPEN %X ", channel);
	sendLine(hdr + name);
	readLine(lastReply_);
	if (lastReply_.rfind("OK", 0) != 0) return false;

	// "OK <len>" -- the whole channel's content follows immediately.
	size_t sp = lastReply_.find(' ');
	int len = (sp == std::string::npos) ? 0 : atoi(lastReply_.c_str() + sp + 1);
	if (len < 0) return false;

	std::vector<uint8_t> buf((size_t)len);
	if (len > 0 && !readExact(buf.data(), (size_t)len)) return false;

	ChannelBuffer& cb = buffers_[channel];
	cb.data.assign((const char*)buf.data(), buf.size());
	cb.pos = 0;
	return true;
}

void IecBridgeClient::close(int channel)
{
	buffers_.erase(channel);
	if (fd_ < 0) return;
	char cmd[16];
	snprintf(cmd, sizeof(cmd), "CLOSE %X", channel);
	sendLine(cmd);
	readLine(lastReply_);
}

bool IecBridgeClient::write(int channel, const std::string& data, bool eoi)
{
	if (fd_ < 0) return false;
	char hdr[32];
	snprintf(hdr, sizeof(hdr), "WRITE %X %zu %c", channel, data.size(), eoi ? 'E' : 'C');
	sendLine(hdr);
	send(fd_, data.data(), data.size(), 0);
	readLine(lastReply_);
	return lastReply_.rfind("OK", 0) == 0;
}

bool IecBridgeClient::requestRead(int channel, int size, std::string& outData, bool& outEoi)
{
	outData.clear();
	outEoi = false;

	auto it = buffers_.find(channel);
	if (it == buffers_.end()) return false; // no open() ever fetched this channel

	ChannelBuffer& cb = it->second;
	size_t remaining = cb.data.size() - cb.pos;
	size_t n = (size_t)size < remaining ? (size_t)size : remaining;
	outData.assign(cb.data, cb.pos, n);
	cb.pos += n;
	outEoi = (cb.pos >= cb.data.size());
	return true;
}
