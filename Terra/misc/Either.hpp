#pragma once

NS_HWM_BEGIN

//! a class represents either success or failure
//! if succeeded, is_right() and operator bool() returns true.
template<class Left, class Right>
class Either
{
public:
    template<class T>
    Either(T &&data) : data_(std::forward<T>(data)) {}
    
    bool is_right() const { return data_.index() == 1; }
    
    explicit operator bool() const { return is_right(); }
    
    Left & left() { return std::get<Left>(data_); }
    Left const & left() const { return std::get<Left>(data_); }
    Right & right() { return std::get<Right>(data_); }
    Right & right() const { return std::get<Right>(data_); }
    
    template<class F>
    auto visit(F f) { return mpark::visit(f, data_); }
    
    template<class F>
    auto visit(F f) const { return mpark::visit(f, data_); }
    
private:
    std::variant<Left, Right> data_;
};

NS_HWM_END
