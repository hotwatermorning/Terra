#pragma once

#include <iostream>
#include <sstream>

#if defined(_MSC_VER)
#include <Windows.h>

NS_HWM_BEGIN

template<class Char, class Traits>
class basic_debugger_streambuf
:	public std::basic_streambuf<Char, Traits>
{
	static void Output(char const *str) { OutputDebugStringA(str); }
	static void Output(wchar_t const *str) { OutputDebugStringW(str); }
	std::vector<Char> buffer_;
public:
	basic_debugger_streambuf()
	{
		buffer_.reserve(1024);
	}

	~basic_debugger_streambuf()
	{
		if (buffer_.size() > 0) {
			buffer_.push_back(0);
			Output(buffer_.data());
		}
	}

	int_type overflow(int_type c) override {
		buffer_.push_back(c);
		if (buffer_.size() == buffer_.capacity()-1 || c == EOF) {
			buffer_.push_back(0);
			Output(buffer_.data());
			buffer_.clear();
		}

		return c;
	}
};

template<class Char, class Traits = std::char_traits<Char>>
class basic_debugger_ostream
:	public std::basic_ostream<Char, Traits>
{
public:
	using stream_type = std::basic_ostream<Char, Traits>;
	using streambuf_type = basic_debugger_streambuf<Char, Traits>;
	basic_debugger_ostream()
	:    stream_type(new streambuf_type)
	{}

	~basic_debugger_ostream()
	{
		flush();
		delete rdbuf();
	}
};

__declspec(selectany) basic_debugger_ostream<char>		dout;
__declspec(selectany) basic_debugger_ostream<wchar_t>	wdout;

NS_HWM_END

#else

NS_HWM_BEGIN

namespace {
    auto &dout = std::cout;
    auto &wdout = std::wcout;
}

NS_HWM_END

#endif
