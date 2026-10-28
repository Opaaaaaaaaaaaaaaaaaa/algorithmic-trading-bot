#define _CRT_SECURE_NO_WARNINGS

#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <vector>

namespace {

std::mutex g_mutex;
std::string g_dir;
std::string g_date;
std::ofstream g_file;
bool g_atLineStart = true;

std::tm localNow()
{
	std::time_t t = std::time(nullptr);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	return tm;
}

void writeToFile(const char* s, std::streamsize n)
{
	for (std::streamsize i = 0; i < n; ++i) {
		if (g_atLineStart) {
			std::tm tm = localNow();
			char date[16];
			std::strftime(date, sizeof(date), "%Y-%m-%d", &tm);
			if (date != g_date || !g_file.is_open()) {
				g_file.close();
				g_date = date;
				g_file.open(g_dir + "/" + g_date + ".txt", std::ios::app);
			}
			char stamp[16];
			std::strftime(stamp, sizeof(stamp), "[%H:%M:%S] ", &tm);
			g_file << stamp;
			g_atLineStart = false;
		}
		g_file.put(s[i]);
		if (s[i] == '\n') {
			g_file.flush();
			g_atLineStart = true;
		}
	}
}

class TeeBuf : public std::streambuf {
public:
	explicit TeeBuf(std::streambuf* console) : m_console(console) {}

protected:
	int_type overflow(int_type ch) override
	{
		if (traits_type::eq_int_type(ch, traits_type::eof()))
			return traits_type::not_eof(ch);
		char c = traits_type::to_char_type(ch);
		xsputn(&c, 1);
		return ch;
	}

	std::streamsize xsputn(const char* s, std::streamsize n) override
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		m_console->sputn(s, n);
		writeToFile(s, n);
		return n;
	}

	int sync() override
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_file.flush();
		return m_console->pubsync();
	}

private:
	std::streambuf* m_console;
};

}

void startLogging(const std::string& dir)
{
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	g_dir = dir;

	static TeeBuf outBuf(std::cout.rdbuf());
	static TeeBuf errBuf(std::cerr.rdbuf());
	std::cout.rdbuf(&outBuf);
	std::cerr.rdbuf(&errBuf);
}

void logPrintf(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	va_list copy;
	va_copy(copy, args);
	int len = std::vsnprintf(nullptr, 0, fmt, copy);
	va_end(copy);
	if (len > 0) {
		std::vector<char> buf(static_cast<size_t>(len) + 1);
		std::vsnprintf(buf.data(), buf.size(), fmt, args);
		std::cout.write(buf.data(), len);
		std::cout.flush();
	}
	va_end(args);
}
