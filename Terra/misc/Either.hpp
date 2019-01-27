#pragma once

NS_HWM_BEGIN

//! 失敗か成功かどちらかの状況を返すクラス
//! is_right() == trueの時は成功の状況
template<class Left, class Right>
class Either
{
public:
    template<class T>
    Either(T &&data) : data_(std::forward<T>(data)) {}
    
    bool is_right() const { return data_.index() == 1; }
    
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
