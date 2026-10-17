#include "Tnfs.h"
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>

// Protocol constants -- see Tnfs.h for how this understanding was arrived at.
enum {
	TNFS_MOUNT      = 0x00,
	TNFS_UMOUNT     = 0x01,
	TNFS_CLOSEDIR   = 0x12,
	TNFS_OPENDIRX   = 0x17,
	TNFS_READDIRX   = 0x18,
	TNFS_OPENFILE   = 0x29,
	TNFS_READBLOCK  = 0x21,
	TNFS_CLOSEFILE  = 0x23,
};
static const uint8_t  TNFS_STATUS_EOF = 0x21;
static const uint16_t TNFS_FLAG_RDONLY = 0x0001;
static const size_t   TNFS_MAX_BLOCK = 512;
static const size_t   TNFS_MAX_PKT = 540;

uint8_t Tnfs::nextSeq() { seq_ = (uint8_t)(seq_ + 1); return seq_; }

std::string Tnfs::absPath(const std::string& name) const
{
	if (!name.empty() && name[0] == '/')
		return name;
	if (curDir_ == "/")
		return "/" + name;
	return curDir_ + "/" + name;
}

bool Tnfs::transact(uint8_t cmd, const uint8_t* payload, size_t plen,
                     uint16_t& outSid, uint8_t& outStatus, uint8_t* outBuf, size_t outCap, size_t& outLen)
{
	if (sock_ < 0)
		return false;
	uint8_t sq = nextSeq();
	uint8_t pkt[TNFS_MAX_PKT];
	if (4 + plen > sizeof(pkt))
		return false;
	pkt[0] = (uint8_t)(sid_ & 0xFF);
	pkt[1] = (uint8_t)((sid_ >> 8) & 0xFF);
	pkt[2] = sq;
	pkt[3] = cmd;
	if (plen)
		memcpy(pkt + 4, payload, plen);

	static const int MAX_RETRIES = 5;
	static const int RETRY_TIMEOUT_MS = 2000;
	for (int attempt = 0; attempt < MAX_RETRIES; attempt++)
	{
		if (send(sock_, pkt, 4 + plen, 0) != (ssize_t)(4 + plen))
			return false;

		struct pollfd pfd = { sock_, POLLIN, 0 };
		int remaining = RETRY_TIMEOUT_MS;
		while (remaining > 0)
		{
			int pr = poll(&pfd, 1, remaining);
			if (pr <= 0)
				break; // timeout or error -> next attempt (resend)
			uint8_t rbuf[TNFS_MAX_PKT];
			ssize_t n = recv(sock_, rbuf, sizeof(rbuf), 0);
			if (n < 4)
				continue;
			uint8_t rSeq = rbuf[2], rCmd = rbuf[3];
			if (rCmd != cmd || rSeq != sq)
				continue; // stray/duplicate -- keep waiting
			outSid = (uint16_t)(rbuf[0] | ((uint16_t)rbuf[1] << 8));
			outStatus = (n >= 5) ? rbuf[4] : 0;
			size_t dataLen = (n > 5) ? (size_t)n - 5 : 0;
			if (dataLen > outCap) dataLen = outCap;
			if (dataLen) memcpy(outBuf, rbuf + 5, dataLen);
			outLen = dataLen;
			return true;
		}
	}
	return false;
}

bool Tnfs::connect(const std::string& host, uint16_t port)
{
	host_ = host;
	port_ = port ? port : 16384;

	struct addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	char portStr[8];
	snprintf(portStr, sizeof(portStr), "%u", (unsigned)port_);
	struct addrinfo* res = nullptr;
	if (0 != getaddrinfo(host_.c_str(), portStr, &hints, &res) || !res)
		return false;
	int fd = -1;
	for (struct addrinfo* p = res; p; p = p->ai_next)
	{
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) continue;
		if (0 == ::connect(fd, p->ai_addr, p->ai_addrlen))
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0)
		return false;
	sock_ = fd;

	// MOUNT payload: version(2LE) + retry_ms hint(2LE, 0=server default) +
	// mount path + NUL + username + NUL + password + NUL. Anonymous: empty
	// user/pass (just their NUL terminators).
	uint8_t payload[8];
	size_t n = 0;
	payload[n++] = 0x00; payload[n++] = 0x01; // version 0x0100
	payload[n++] = 0x00; payload[n++] = 0x00; // retry hint
	payload[n++] = '/';  payload[n++] = 0;    // mount path "/"
	payload[n++] = 0;                         // user ""
	payload[n++] = 0;                         // pass ""

	sid_ = 0; seq_ = 0;
	uint16_t rsid; uint8_t status; uint8_t resp[8]; size_t rlen;
	bool ok = transact(TNFS_MOUNT, payload, n, rsid, status, resp, sizeof(resp), rlen);
	if (!ok || status != 0)
	{
		close(sock_); sock_ = -1;
		return false;
	}
	sid_ = rsid;
	curDir_ = "/";
	connected_ = true;
	return true;
}

bool Tnfs::cwd(const std::string& name)
{
	if (name == "..")
	{
		if (curDir_ != "/")
		{
			size_t sl = curDir_.find_last_of('/');
			curDir_ = (sl == 0) ? "/" : curDir_.substr(0, sl);
		}
		return true;
	}
	if (name.empty() || name == "/")
	{
		curDir_ = "/";
		return true;
	}
	curDir_ = absPath(name);
	return true;
}

bool Tnfs::list(std::vector<Entry>& out)
{
	if (!connected_)
		return false;
	out.clear();

	// OPENDIRX/READDIRX (type/size inline, batched) rather than the plain
	// OPENDIR/READDIR+STATFILE-per-entry dance.
	uint8_t opayload[4 + 1 + 256];
	size_t n = 0;
	opayload[n++] = 0; // diropts: default
	opayload[n++] = 0; // sortopts: default
	opayload[n++] = 0; opayload[n++] = 0; // maxresults = 0 (no limit)
	opayload[n++] = 0; // pattern = "" (match everything)
	if (n + curDir_.size() + 1 > sizeof(opayload))
		return false;
	memcpy(opayload + n, curDir_.c_str(), curDir_.size() + 1); n += curDir_.size() + 1;

	uint16_t sid; uint8_t status; uint8_t resp[4]; size_t rlen;
	if (!transact(TNFS_OPENDIRX, opayload, n, sid, status, resp, sizeof(resp), rlen) || status != 0 || rlen < 1)
		return false;
	uint8_t handle = resp[0];

	bool eof = false;
	while (!eof)
	{
		uint8_t rpayload[2] = { handle, 0 }; // req_count=0 -> as many as fit in one response
		uint8_t rbuf[516]; size_t rrlen; uint8_t rstatus; uint16_t rsid;
		if (!transact(TNFS_READDIRX, rpayload, 2, rsid, rstatus, rbuf, sizeof(rbuf), rrlen))
			break;
		if (rstatus == TNFS_STATUS_EOF)
			break;
		if (rstatus != 0 || rrlen < 4)
			break;
		uint8_t count = rbuf[0];
		eof = (rbuf[1] & 0x01) != 0;
		size_t off = 4;
		for (uint8_t i = 0; i < count; i++)
		{
			if (off + 13 >= rrlen)
				break; // truncated entry -- stop safely
			uint8_t flags = rbuf[off];
			uint32_t size = (uint32_t)rbuf[off + 1] | ((uint32_t)rbuf[off + 2] << 8) |
			                ((uint32_t)rbuf[off + 3] << 16) | ((uint32_t)rbuf[off + 4] << 24);
			off += 13; // flags(1) + size(4) + mtime(4) + ctime(4)
			const char* name = (const char*)rbuf + off;
			size_t avail = rrlen - off;
			size_t nlen = 0;
			while (nlen < avail && name[nlen] != '\0') nlen++;
			if (nlen >= avail)
				break; // no NUL terminator -- malformed, stop safely
			off += nlen + 1;
			if (name[0] && strcmp(name, ".") && strcmp(name, ".."))
			{
				Entry e;
				e.name = name;
				e.isDir = (flags & 0x01) != 0; // bit0 = TNFS_DIRENTRY_DIR
				e.size = size;
				out.push_back(e);
			}
		}
	}
	uint16_t csid; uint8_t cstatus; uint8_t cresp[4]; size_t clen;
	transact(TNFS_CLOSEDIR, &handle, 1, csid, cstatus, cresp, sizeof(cresp), clen); // best-effort
	return true;
}

void Tnfs::closeHandle(uint8_t fd)
{
	uint16_t sid; uint8_t status; uint8_t resp[4]; size_t rlen;
	transact(TNFS_CLOSEFILE, &fd, 1, sid, status, resp, sizeof(resp), rlen); // best-effort
}

bool Tnfs::getToFile(const std::string& name, const std::string& destPath)
{
	if (!connected_)
		return false;
	std::string full = absPath(name);

	uint8_t payload[4 + 256];
	size_t n = 0;
	payload[n++] = (uint8_t)(TNFS_FLAG_RDONLY & 0xFF); payload[n++] = (uint8_t)((TNFS_FLAG_RDONLY >> 8) & 0xFF);
	payload[n++] = 0; payload[n++] = 0; // mode, unused for read
	if (n + full.size() + 1 > sizeof(payload))
		return false;
	memcpy(payload + n, full.c_str(), full.size() + 1); n += full.size() + 1;

	uint16_t sid; uint8_t status; uint8_t resp[4]; size_t rlen;
	if (!transact(TNFS_OPENFILE, payload, n, sid, status, resp, sizeof(resp), rlen) || status != 0 || rlen < 1)
		return false;
	uint8_t fd = resp[0];

	FILE* out = fopen(destPath.c_str(), "wb");
	if (!out) { closeHandle(fd); return false; }

	uint32_t total = 0; bool ok = true;
	for (;;)
	{
		uint8_t rpayload[3] = { fd, (uint8_t)(TNFS_MAX_BLOCK & 0xFF), (uint8_t)((TNFS_MAX_BLOCK >> 8) & 0xFF) };
		uint8_t rbuf[2 + TNFS_MAX_BLOCK]; size_t rrlen; uint8_t rstatus; uint16_t rsid;
		if (!transact(TNFS_READBLOCK, rpayload, sizeof(rpayload), rsid, rstatus, rbuf, sizeof(rbuf), rrlen))
		{
			ok = false; break;
		}
		if (rstatus == TNFS_STATUS_EOF)
			break;
		if (rstatus != 0 || rrlen < 2)
		{
			ok = false; break;
		}
		uint16_t got = (uint16_t)(rbuf[0] | (rbuf[1] << 8));
		if (got == 0)
			break;
		fwrite(rbuf + 2, 1, got, out);
		total += got;
	}
	fclose(out);
	closeHandle(fd);
	return ok && total > 0;
}

void Tnfs::disconnect()
{
	if (connected_)
	{
		uint16_t sid; uint8_t status; uint8_t resp[4]; size_t rlen;
		transact(TNFS_UMOUNT, nullptr, 0, sid, status, resp, sizeof(resp), rlen); // best-effort
	}
	if (sock_ >= 0)
		close(sock_);
	sock_ = -1;
	connected_ = false;
}
