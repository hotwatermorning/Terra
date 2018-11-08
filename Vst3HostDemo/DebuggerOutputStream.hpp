#pragma once

#include <iostream>
#include <sstream>

#if defined(_MSC_VER)
#include <Windows.h>

NS_HWM_BEGIN

//! OutputDebugStringへ出力するストリームクラス
//! 複数のoperator<<を最後にまとめて出力する部分は
//! http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3535.html
//! これを参考にした

template<class Char, class Traits = std::char_traits<Char>>
class basic_debugger_output;

template<class Char, class Traits = std::char_traits<Char>>
class basic_debugger_output_impl;

template<class Char, class Traits>
class basic_debugger_output_impl
:	public std::basic_ostream<Char, Traits>
{
public:
	typedef basic_debugger_output<Char, Traits> owner_type;
	typedef std::basic_ostream<Char, Traits> stream_type;
	typedef basic_debugger_output_impl<Char, Traits> this_type;
	typedef stream_type & (manip)(stream_type &);
	typedef std::ios_base & (ios_base_manip)(std::ios_base &);

	basic_debugger_output_impl(owner_type &owner)
		:	stream_type(static_cast<std::basic_streambuf<Char, Traits> *>(nullptr))
		,	owner_(&owner)
	{}

	~basic_debugger_output_impl()
	{
		if(owner_) {
			owner_->Output();
		}
	}

	basic_debugger_output_impl(this_type &&rhs)
		:	stream_type(static_cast<std::basic_streambuf<Char, Traits> *>(nullptr))
		,	owner_(rhs.owner_)
	{
		rhs.owner_ = nullptr;
	}

	this_type & operator=(this_type &&rhs)
	{
		owner_ = rhs.owner_;
		rhs.owner_ = nullptr;
		return *this;
	}

	template<class T>
	this_type const & operator<<(T &&t) const
	{
		stream() << std::forward<T>(t);
		return *this;
	}

	this_type const & operator<<(manip &m) const
	{
		m(stream());
		return *this;
	}

	this_type const & operator<<(ios_base_manip &m) const
	{
		m(stream());
		return *this;
	}

	stream_type & stream() const
	{
		return owner_->stream();
	}

private:
	owner_type *owner_;
};

template<class Char, class Traits>
class basic_debugger_output
:	public std::basic_ostream<Char, Traits>
{
public:
	typedef basic_debugger_output<Char, Traits> this_type;
	typedef basic_debugger_output_impl<Char, Traits> impl_type;
	typedef std::basic_ostream<Char, Traits> stream_type;
	typedef stream_type & (manip)(stream_type &);
	typedef std::ios_base & (ios_base_manip)(std::ios_base &);

	basic_debugger_output()
		:	stream_type(static_cast<std::basic_streambuf<Char, Traits> *>(nullptr))
	{}

	basic_debugger_output(this_type const &) = delete;
	this_type & operator=(this_type const &) = delete;
	
	template<class T>
	impl_type operator<<(T &&t)
	{
		auto impl = impl_type(*this);
		impl << std::forward<T>(t);
		return impl;
	}

	impl_type operator<<(manip &m)
	{
		auto impl = impl_type(*this);
		impl << m;
		return impl;
	}

	impl_type operator<<(ios_base_manip &m)
	{
		auto impl = impl_type(*this);
		impl << m;
		return impl;
	}

	stream_type & stream()
	{
		return ss_;
	}

	template<class Char2>
	static
	void OutputImpl(Char2 const *str);

	template<>
	static
	void OutputImpl<char>(char const *str)
	{
		OutputDebugStringA(str);
	}

	template<>
	static
	void OutputImpl<wchar_t>(wchar_t const *str)
	{
		OutputDebugStringW(str);
	}

	void Output()
	{
		OutputImpl(ss_.str().c_str());

		static std::basic_string<Char> const null_string;
		ss_.str(null_string);
	}

private:
	std::basic_stringstream<Char, Traits> ss_;
};

__declspec(selectany) basic_debugger_output<char>		dout;
__declspec(selectany) basic_debugger_output<wchar_t>	wdout;

NS_HWM_END

#else

NS_HWM_BEGIN

namespace {
    auto &dout = std::cout;
    auto &wdout = std::wcout;
}

NS_HWM_END

#endif
