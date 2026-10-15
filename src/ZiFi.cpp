#include "ZiFi.h"
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

ZiFi::ZiFi()
{
	rxRing_.reserve(kRingCap);
	txRing_.reserve(kRingCap);
}

ZiFi::~ZiFi()
{
	if (sockFd_ >= 0)
		close(sockFd_);
}

bool ZiFi::connectTo(const char* host, int port)
{
	if (sockFd_ >= 0) {
		close(sockFd_);
		sockFd_ = -1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(fd);
		return false;
	}

	if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "ZiFi: connect to %s:%d failed\n", host, port);
		close(fd);
		return false;
	}

	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	fcntl(fd, F_SETFL, O_NONBLOCK);

	sockFd_ = fd;
	printf("ZiFi: connected to %s:%d\n", host, port);
	return true;
}

bool ZiFi::in(uint16_t port, uint8_t& value)
{
	if ((port & 0xFF) == 0xEF) {
		uint8_t hi = port >> 8;
		if (hi == 0xC0) { // ZIFR: RX fill count
			value = (uint8_t)(rxRing_.size() > 255 ? 255 : rxRing_.size());
			return true;
		}
		if (hi == 0xC1) { // ZOFR: TX fill count
			value = (uint8_t)(txRing_.size() > 255 ? 255 : txRing_.size());
			return true;
		}
		if (hi <= 0xBF) { // DR: pull next RX byte, 0xFF if empty
			if (rxRing_.empty()) {
				value = 0xFF;
			} else {
				value = rxRing_.front();
				rxRing_.erase(rxRing_.begin());
			}
			return true;
		}
	}

	if ((port & 0xFEFF) == 0xFC3B) {
		bool isDataPort = (port & 0x0100) != 0;
		if (!isDataPort) {
			value = unoSelectedReg_; // latch port: no real IN use, but harmless to answer
			return true;
		}
		if (unoSelectedReg_ == 0xC6) { // UART data
			if (rxRing_.empty()) {
				value = 0xFF;
			} else {
				value = rxRing_.front();
				rxRing_.erase(rxRing_.begin());
			}
		} else if (unoSelectedReg_ == 0xC7) { // status: bit0 RX_RECV, bit1 TX_BUSY
			value = (uint8_t)((rxRing_.empty() ? 0 : 1) | (txRing_.size() >= kRingCap ? 2 : 0));
		} else {
			value = 0xFF; // UART2 (0xC8/0xC9) etc. — absent
		}
		return true;
	}

	return false;
}

bool ZiFi::out(uint16_t port, uint8_t value)
{
	if ((port & 0xFF) == 0xEF) {
		uint8_t hi = port >> 8;
		if (hi <= 0xBF) { // DR: push a TX byte, dropped if the ring is full
			if (txRing_.size() < kRingCap)
				txRing_.push_back(value);
			return true;
		}
		// ZIFR/ZOFR are read-only; writes to them are accepted and ignored.
		return true;
	}

	if ((port & 0xFEFF) == 0xFC3B) {
		bool isDataPort = (port & 0x0100) != 0;
		if (!isDataPort) {
			unoSelectedReg_ = value; // latch port: select the internal register
			return true;
		}
		if (unoSelectedReg_ == 0xC6) { // UART data: push a TX byte
			if (txRing_.size() < kRingCap)
				txRing_.push_back(value);
		}
		// Status/UART2 writes are accepted and ignored.
		return true;
	}

	return false;
}

std::string ZiFi::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "ZIFI")
		return std::string();

	char out[128];
	snprintf(out, sizeof(out), "OK connected=%d rx=%zu tx=%zu",
	         sockFd_ >= 0 ? 1 : 0, rxRing_.size(), txRing_.size());
	return out;
}

void ZiFi::pump()
{
	if (sockFd_ < 0)
		return;

	// Drain the TX ring to the socket.
	while (!txRing_.empty()) {
		ssize_t n = send(sockFd_, txRing_.data(), txRing_.size(), 0);
		if (n > 0)
			txRing_.erase(txRing_.begin(), txRing_.begin() + n);
		else
			break; // EAGAIN or error — try again next frame
	}

	// Pull whatever's waiting into the RX ring (bounded).
	uint8_t buf[1024];
	for (;;) {
		ssize_t n = recv(sockFd_, buf, sizeof(buf), 0);
		if (n <= 0)
			break;
		for (ssize_t i = 0; i < n && rxRing_.size() < kRingCap; i++)
			rxRing_.push_back(buf[i]);
	}
}
