#pragma once

NS_HWM_BEGIN

//! 初期値がfalse、MoveするとMoveされた方の値がfalseになるようなBool型として使用可能な型。
class Flag
{
public:
	typedef Flag this_type;

	explicit
	Flag(bool init = false)
		:	b_(init)
	{}

	Flag (this_type &&rhs)
		:	b_(rhs.b_)
	{
		rhs.b_ = false;
	}

	Flag & operator=(this_type &&rhs)
	{
		b_ = rhs.b_;
		rhs.b_ = false;
		return *this;
	}

#if _MSC_VER <= 1700
private:
	Flag (this_type const &rhs);
	Flag & operator=(this_type &rhs);
#else
	Flag (this_type const &rhs) = delete;
	Flag & operator=(this_type &rhs) = delete;
#endif

public:
	Flag & operator=(bool state)
	{
		set(state);
		return *this;
	}

	void swap(this_type &rhs)
	{
		bool tmp = b_;
		b_ = rhs.b_;
		rhs.b_ = tmp;
	}

	void set(bool state)
	{
		b_ = state;
	}

	bool get() const
	{
		return b_;
	}

#if _MSC_VER <= 1700
	operator bool() const 
	{
		return get();
	}
#else
	explicit operator bool() const 
	{
		return get();
	}
#endif

private:
	bool b_;
};

inline void swap(Flag &lhs, Flag &rhs)
{
	lhs.swap(rhs);
}

inline
bool operator==(Flag const &f, bool b) { return (bool)f == b; }

inline
bool operator==(bool b, Flag const &f) { return f == b;  }

inline
bool operator!=(Flag const &f, bool b) { return (bool)f != b; }

inline
bool operator!=(bool b, Flag const &f) { return f != b; }

NS_HWM_END
