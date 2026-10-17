#include "Gopher.h"
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdio>

static int openTcp(const std::string& host, uint16_t port)
{
	struct addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	char portStr[8];
	snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);
	struct addrinfo* res = nullptr;
	if (0 != getaddrinfo(host.c_str(), portStr, &hints, &res) || !res)
		return -1;
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
	return fd;
}

bool Gopher::fetchLines(const std::string& host, uint16_t port, const std::string& selector, std::vector<std::string>& lines)
{
	lines.clear();
	int fd = openTcp(host, port ? port : 70);
	if (fd < 0)
		return false;

	std::string req = selector + "\r\n";
	if (send(fd, req.data(), req.size(), 0) != (ssize_t)req.size())
	{
		close(fd);
		return false;
	}

	std::string buf;
	char chunk[2048];
	bool done = false;
	while (!done)
	{
		ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
		if (n <= 0)
			break;
		buf.append(chunk, (size_t)n);
		size_t pos;
		while ((pos = buf.find('\n')) != std::string::npos)
		{
			std::string line = buf.substr(0, pos);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			buf.erase(0, pos + 1);
			if (line == ".")
			{
				done = true;
				break;
			}
			lines.push_back(line);
		}
	}
	close(fd);
	return true;
}

// "TChar Display \t Selector \t Host \t Port" -- informational ('i') and
// error ('3') lines have no real selector/host/port and are never
// selectable.
static bool parseLine(const std::string& line, Gopher::Entry& out)
{
	if (line.empty())
		return false;
	char type = line[0];
	size_t t1 = line.find('\t', 1);
	if (t1 == std::string::npos)
		return false;
	out.display = line.substr(1, t1 - 1);
	size_t t2 = line.find('\t', t1 + 1);
	if (t2 == std::string::npos)
		return false;
	out.selector = line.substr(t1 + 1, t2 - (t1 + 1));
	size_t t3 = line.find('\t', t2 + 1);
	if (t3 != std::string::npos)
	{
		out.host = line.substr(t2 + 1, t3 - (t2 + 1));
		out.port = (uint16_t)atoi(line.substr(t3 + 1).c_str());
	}
	else
	{
		out.host = line.substr(t2 + 1);
		out.port = 70;
	}
	out.isDir = (type == '1');
	return type != 'i' && type != '3' && !out.display.empty();
}

bool Gopher::connect(const std::string& host, uint16_t port)
{
	stack_.clear();
	lastEntries_.clear();
	Frame root;
	root.host = host;
	root.port = port ? port : 70;
	root.selector = "";
	std::vector<std::string> lines;
	if (!fetchLines(root.host, root.port, root.selector, lines))
		return false;
	stack_.push_back(root);
	connected_ = true;
	return true;
}

bool Gopher::list(std::vector<Entry>& out)
{
	if (!connected_ || stack_.empty())
		return false;
	const Frame& f = stack_.back();
	std::vector<std::string> lines;
	if (!fetchLines(f.host, f.port, f.selector, lines))
		return false;
	lastEntries_.clear();
	for (const auto& line : lines)
	{
		Entry e;
		if (parseLine(line, e))
			lastEntries_.push_back(e);
	}
	out = lastEntries_;
	return true;
}

bool Gopher::cwd(const std::string& display)
{
	if (!connected_ || stack_.empty())
		return false;
	for (const auto& e : lastEntries_)
	{
		if (e.display == display && e.isDir)
		{
			Frame nf;
			nf.host = e.host; nf.port = e.port; nf.selector = e.selector; nf.dispName = display;
			stack_.push_back(nf);
			return true;
		}
	}
	return false;
}

bool Gopher::cwdUp()
{
	if (stack_.size() > 1)
	{
		stack_.pop_back();
		return true;
	}
	return false;
}

std::string Gopher::cwdPath() const
{
	if (stack_.size() <= 1)
		return "/";
	std::string p;
	for (size_t i = 1; i < stack_.size(); i++)
		p += "/" + stack_[i].dispName;
	return p;
}

bool Gopher::getToFile(const std::string& display, const std::string& destPath)
{
	if (!connected_)
		return false;
	const Entry* found = nullptr;
	for (const auto& e : lastEntries_)
		if (e.display == display && !e.isDir) { found = &e; break; }
	if (!found)
		return false;

	int fd = openTcp(found->host, found->port ? found->port : 70);
	if (fd < 0)
		return false;
	std::string req = found->selector + "\r\n";
	if (send(fd, req.data(), req.size(), 0) != (ssize_t)req.size())
	{
		close(fd);
		return false;
	}

	FILE* out = fopen(destPath.c_str(), "wb");
	if (!out)
	{
		close(fd);
		return false;
	}

	char chunk[4096];
	size_t total = 0;
	for (;;)
	{
		ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
		if (n <= 0)
			break;
		fwrite(chunk, 1, (size_t)n, out);
		total += (size_t)n;
	}
	fclose(out);
	close(fd);
	return total > 0;
}

std::string Gopher::downloadBasename(const std::string& display) const
{
	for (const auto& e : lastEntries_)
	{
		if (e.display == display && !e.isDir)
		{
			size_t sl = e.selector.find_last_of('/');
			std::string fname = (sl == std::string::npos) ? e.selector : e.selector.substr(sl + 1);
			return fname.empty() ? display : fname;
		}
	}
	return display;
}

void Gopher::disconnect()
{
	connected_ = false;
	stack_.clear();
	lastEntries_.clear();
}
