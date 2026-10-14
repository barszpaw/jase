#include "debugserver.h"
#include "minzx.h"
#include "z80.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

static std::vector<std::string> splitWs(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

bool DebugServer::init(int port)
{
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return false;

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (listen(listenFd_, 1) < 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    fcntl(listenFd_, F_SETFL, O_NONBLOCK);
    printf("DebugServer: listening on port %d\n", port);
    return true;
}

void DebugServer::reply(const std::string& msg)
{
    if (clientFd_ < 0) return;
    std::string out = msg + "\n";
    ssize_t n = ::send(clientFd_, out.data(), out.size(), 0);
    (void)n; // best-effort; a dropped reply just looks like a timeout to the client
}

void DebugServer::poll(MinZX& minZX)
{
    if (listenFd_ < 0) return;

    // Accept a new client if we don't have one — one at a time is enough
    // for agent-driven debugging.
    if (clientFd_ < 0) {
        int fd = accept(listenFd_, nullptr, nullptr);
        if (fd >= 0) {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            clientFd_ = fd;
            rxBuf_.clear();
            printf("DebugServer: client connected\n");
        }
    }
    if (clientFd_ < 0) return;

    char buf[1024];
    ssize_t n = recv(clientFd_, buf, sizeof(buf), 0);
    if (n == 0) {
        // Clean disconnect.
        close(clientFd_);
        clientFd_ = -1;
        printf("DebugServer: client disconnected\n");
        return;
    }
    if (n < 0) return; // EAGAIN/EWOULDBLOCK — no data waiting, normal

    rxBuf_.append(buf, (size_t)n);
    size_t pos;
    while ((pos = rxBuf_.find('\n')) != std::string::npos) {
        std::string line = rxBuf_.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        rxBuf_.erase(0, pos + 1);
        if (!line.empty()) handleLine(line, minZX);
    }
}

void DebugServer::handleLine(const std::string& line, MinZX& minZX)
{
    std::vector<std::string> toks = splitWs(line);
    if (toks.empty()) return;
    const std::string& cmd = toks[0];

    Z80* z80 = minZX.getCPU();
    uint8_t* mem = minZX.getMemory();
    char out[512];

    if (cmd == "REGS") {
        snprintf(out, sizeof(out),
            "OK AF=%04x BC=%04x DE=%04x HL=%04x AFx=%04x BCx=%04x DEx=%04x HLx=%04x "
            "IX=%04x IY=%04x SP=%04x PC=%04x I=%02x R=%02x IFF1=%d IFF2=%d IM=%d",
            z80->getRegAF(), z80->getRegBC(), z80->getRegDE(), z80->getRegHL(),
            z80->getRegAFx(), z80->getRegBCx(), z80->getRegDEx(), z80->getRegHLx(),
            z80->getRegIX(), z80->getRegIY(), z80->getRegSP(), z80->getRegPC(),
            z80->getRegI(), z80->getRegR(),
            z80->isIFF1() ? 1 : 0, z80->isIFF2() ? 1 : 0, (int)z80->getIM());
        reply(out);
    }
    else if (cmd == "MEM" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        long len = strtol(toks[2].c_str(), nullptr, 10);
        if (len < 0) len = 0;
        if (len > 4096) len = 4096; // one 1024-byte recv buffer's worth of hex, several times over — plenty for a debug peek
        std::string resp = "OK ";
        char hex[3];
        for (long i = 0; i < len; i++) {
            snprintf(hex, sizeof(hex), "%02x", mem[(uint16_t)(addr + i)]);
            resp += hex;
        }
        reply(resp);
    }
    else if (cmd == "POKE" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        std::istringstream bs(toks[2]);
        std::string b;
        int count = 0;
        while (std::getline(bs, b, ',')) {
            mem[(uint16_t)(addr + count)] = (uint8_t)strtoul(b.c_str(), nullptr, 16);
            count++;
        }
        reply("OK");
    }
    else if (cmd == "PORT" && toks.size() >= 2) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        uint8_t val = minZX.inPort(addr);
        snprintf(out, sizeof(out), "OK %02x", val);
        reply(out);
    }
    else if (cmd == "OUT" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        uint8_t val = (uint8_t)strtoul(toks[2].c_str(), nullptr, 16);
        minZX.outPort(addr, val);
        reply("OK");
    }
    else if (cmd == "STATE") {
        snprintf(out, sizeof(out), "OK border=%u tstates=%u",
                 (unsigned)minZX.getBorderColor(), (unsigned)minZX.getTStates());
        reply(out);
    }
    else {
        reply("ERR unknown command");
    }
}

void DebugServer::shutdown()
{
    if (clientFd_ >= 0) { close(clientFd_); clientFd_ = -1; }
    if (listenFd_ >= 0) { close(listenFd_); listenFd_ = -1; }
}
