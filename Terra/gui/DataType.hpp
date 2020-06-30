#pragma once

NS_HWM_BEGIN

template<class T>
struct Point
{
    T x = 0;
    T y = 0;

    Point()
    {}

    explicit
    Point(wxPoint pt) : x(pt.x), y(pt.y)
    {}

    Point(T x, T y) : x(x), y(y) {}

    operator wxPoint() const {
        return wxPoint {
            (int)std::round(x),
            (int)std::round(y)
        };
    }
};

template<class T>
struct Size
{
    T w = 0;
    T h = 0;

    Size()
    {}

    explicit
    Size(wxSize sz) : w(sz.x), h(sz.y)
    {}

    Size(T w, T h) : w(w), h(h) {}

    operator wxSize() const {
        return wxSize {
            (int)std::round(w),
            (int)std::round(h)
        };
    }

    void Scale(double xy) noexcept { Scale(xy, xy); }
    void Scale(double x, double y) noexcept
    {
        w *= x;
        h *= y;
    }

    [[nodiscard]] Size<T> Scaled(double xy) const noexcept { return Scaled(xy, xy); }
    [[nodiscard]] Size<T> Scaled(double x, double y) const noexcept {
        auto tmp = *this;
        tmp.Scale(x, y);
        return tmp;
    }
};

template<class T>
bool operator==(Point<T> const &lhs, Point<T> const &rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

template<class T>
bool operator!=(Point<T> const &lhs, Point<T> const &rhs)
{
    return !(lhs == rhs);
}

template<class T>
bool operator==(Size<T> const &lhs, Size<T> const &rhs)
{
    return lhs.w == rhs.w && lhs.h == rhs.h;
}

template<class T>
bool operator!=(Size<T> const &lhs, Size<T> const &rhs)
{
    return !(lhs == rhs);
}

template<class T>
Size<T> operator-(Point<T> const &pt1, Point<T> const &pt2)
{
    return Size<T> { pt1.x - pt2.x, pt1.y - pt2.y };
}

template<class T>
Point<T> operator+(Point<T> const &pos, Size<T> const &size)
{
    return Point<T> { pos.x + size.w, pos.y + size.h };
}

template<class T>
Point<T> & operator+=(Point<T> &pos, Size<T> const &size)
{
    pos = pos + size;
    return pos;
}

template<class T>
Point<T> operator-(Point<T> const &pos, Size<T> const &size)
{
    return Point<T> { pos.x - size.w, pos.y - size.h };
}

template<class T>
Point<T> & operator-=(Point<T> &pos, Size<T> const &size)
{
    pos = pos - size;
    return pos;
}

template<class T>
struct Rect {
    Point<T> pos;
    Size<T> size;

    Rect()
    {}

    Rect(Point<T> _pos, Size<T> _size)
    :   pos(_pos)
    ,   size(_size)
    {}

    Rect(Point<T> topleft, Point<T> rightbottom)
    :   pos(topleft)
    ,   size(rightbottom - topleft)
    {}

    [[nodiscard]] T GetX() const noexcept       { return pos.x; }
    [[nodiscard]] T GetY() const noexcept       { return pos.y; }
    [[nodiscard]] T GetLeft() const noexcept    { return GetX(); }
    [[nodiscard]] T GetTop() const noexcept     { return GetY(); }
    [[nodiscard]] T GetWidth() const noexcept   { return size.w; }
    [[nodiscard]] T GetHeight() const noexcept  { return size.h; }

    [[nodiscard]] T GetRight() const noexcept   { return GetX() + GetWidth(); }
    [[nodiscard]] T GetBottom() const noexcept  { return GetY() + GetHeight(); }

    [[nodiscard]] Point<T>  GetPosition() const noexcept    { return pos; }
    [[nodiscard]] Size<T>   GetSize() const noexcept        { return size; }

    [[nodiscard]] Point<T> GetTopLeft() const noexcept      { return Point<T> { GetLeft(),  GetTop() }; }
    [[nodiscard]] Point<T> GetTopRight() const noexcept     { return Point<T> { GetRight(), GetTop() }; }
    [[nodiscard]] Point<T> GetBottomLeft() const noexcept   { return Point<T> { GetLeft(),  GetBottom() }; }
    [[nodiscard]] Point<T> GetBottomRight() const noexcept  { return Point<T> { GetRight(), GetBottom() }; }

    [[nodiscard]] Point<T> GetCenter() const noexcept {
        return Point<T> {
            GetX() + GetWidth() / 2.0,
            GetY() + GetHeight() / 2.0,
        };
    }

    void Inflate(T xy) noexcept { Inflate(xy, xy); }
    void Inflate(T x, T y) noexcept
    {
        pos.x -= x;
        pos.y -= y;
        size.w += x * 2;
        size.h += y * 2;

        size.w = std::max<T>(size.w, 0);
        size.h = std::max<T>(size.h, 0);
    }

    void Deflate(T xy) noexcept { Deflate(xy, xy); }
    void Deflate(T x, T y) noexcept { Inflate(-x, -y); }

    void Translate(T x, T y) noexcept
    {
        pos.x += x;
        pos.y += y;
    }

    void Scale(double xy) noexcept { Scale(xy, xy); }
    void Scale(double x, double y) noexcept
    {
        size.Scale(x, y);
    }

    [[nodiscard]] Rect Inflated(T xy) const noexcept { return Inflated(xy, xy); }
    [[nodiscard]] Rect Inflated(T x, T y) const noexcept {
        auto tmp = *this;
        tmp.Inflate(x, y);
        return tmp;
    }

    [[nodiscard]] Rect Deflated(T xy) const noexcept { return Deflated(xy, xy); }
    [[nodiscard]] Rect Deflated(T x, T y) const noexcept { return Inflated(-x, -y); }

    [[nodiscard]] Rect Translated(T x, T y) const noexcept
    {
        auto tmp = *this;
        tmp.Translate(x, y);
        return tmp;
    }

    [[nodiscard]] Rect Scaled(double xy) const noexcept { Scaled(xy, xy); }
    [[nodiscard]] Rect Scaled(double x, double y) const noexcept {
        auto tmp = *this;
        tmp.Scale(x, y);
        return tmp;
    }

    [[nodiscard]] Rect WithPosition(Point<T> pos) const noexcept
    {
        return Rect { pos, this->size };
    }

    [[nodiscard]] Rect WithPosition(T x, T y) const noexcept {
        return WithPosition(Point<T> { x, y });
    }

    [[nodiscard]] Rect WithSize(Size<T> size) const noexcept
    {
        return Rect { this->pos, size };
    }

    [[nodiscard]] Rect WithSize(T w, T h) const noexcept {
        return WithSize(Size<T> { w, h });
    }

    [[nodiscard]] bool Contain(Point<T> pt) const noexcept {
        return
        (GetLeft() <= pt.x && pt.x < GetRight()) &&
        (GetTop() <= pt.y && pt.y < GetBottom());
    }

    [[nodiscard]]
    static
    Rect From_wxRect(wxRect const &rc) noexcept {
        Rect tmp;
        tmp.pos.x = rc.GetX();
        tmp.pos.y = rc.GetY();
        tmp.size.w = rc.GetWidth();
        tmp.size.h = rc.GetHeight();

        return tmp;
    }

    [[nodiscard]] operator wxRect() const noexcept {
        return wxRect {
            (int)std::round(GetX()),
            (int)std::round(GetY()),
            (int)std::round(GetWidth()),
            (int)std::round(GetHeight()),
        };
    }
};

using FPoint = Point<float>;
using FSize = Size<float>;
using FRect = Rect<float>;

using DPoint = Point<double>;
using DSize = Size<double>;
using DRect = Rect<double>;

NS_HWM_END
