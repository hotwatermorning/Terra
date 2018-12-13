#pragma once

#include <utility>

NS_HWM_BEGIN

namespace ArrayRefDetail {
    
    template<class T>
    struct DummyArray
    {
        static T value;
        static T * GetAddress() { return &value; }
    };
    
    template<class T>
    T DummyArray<T>::value = {};
    
}    // ::ArrayRefDetail

//! 配列やvectorクラスなど、メモリ上にオブジェクトを連続して配置するクラスへのアクセスを抽象化する。
template<class T>
struct ArrayRef
{
    typedef ArrayRef<T>     this_type;
    typedef T               value_type;
    typedef size_t          size_type;
    typedef T *             iterator;
    typedef T const *       const_iterator;
    
    //! デフォルトコンストラクタ
    /*!
     @post arr_ != nullptr
     @post length == 0
     */
    ArrayRef()
    :    arr_(ArrayRefDetail::DummyArray<T>::GetAddress())
    ,    length_(0)
    {}
    
    //! Iteratorのペアから構築するコンストラクタ
    /*!
     @tparam ContigousInputIterator std::vectorやstd::string(C++11以降)のイテレータなど、\n
     データがメモリ上で連続していることが保証されているクラスのイテレータ。\n
     この要件を満たすイテレータは`Contigous Iterator`と呼ばれたりする。\n
     @note ランダムアクセスイテレータであっても、リングバッファのようなクラスのイテレータは\n
     メモリ上でデータが連続していないため、このクラスでは使用してはいけない。\n
     テンプレート引数ContiguousInputIteratorが`Contiguous Iterator`要件を満たすかどうかを\n
     型レベルでチェックする仕組みが現在無いため、実行時アサーションでメモリの連続性をチェックしている。
     */
    template<class ContigousInputIterator>
    ArrayRef(ContigousInputIterator begin, ContigousInputIterator end)
    :    arr_(nullptr)
    ,    length_(0)
    {
        if(begin == end) {
            arr_ = ArrayRefDetail::DummyArray<T>::GetAddress();
            length_ = 0;
        } else {
            arr_ = &*begin;
            length_ = end - begin;
            assert(((&*(end - 1)) - &*begin == length_ - 1) &&
                   "Iterator must be a Contiguous Iterator"
                   );
        }
    }
    
    template<class Container>
    ArrayRef(Container &c)
    :   ArrayRef(std::begin(c), std::end(c))
    {}
    
    void swap(this_type &rhs)
    {
        std::swap(arr_, rhs.arr_);
        std::swap(length_, rhs.length_);
    }
    
    size_type   size() const { return length_; }
    bool        empty() const { return length_ == 0; }
    T *         data() { return arr_; }
    T const *   data() const { return arr_; }
    T &         operator[](size_type index) { return arr_[index]; }
    T const &   operator[](size_type index) const { return arr_[index]; }
    
    T &         front()       { assert(length_ > 0); return arr_[0]; }
    T const &   front() const { assert(length_ > 0); return arr_[0]; }
    T &         back()        { assert(length_ > 0); return arr_[length_ - 1]; }
    T const &   back() const  { assert(length_ > 0); return arr_[length_ - 1]; }
    
    T *         begin()         { return arr_; }
    T const *   begin() const   { return arr_; }
    T const *   cbegin() const  { return arr_; }
    T *         end()           { return arr_ + length_; }
    T const *   end() const     { return arr_ + length_; }
    T const *   cend() const    { return arr_ + length_; }
    
private:
    T *         arr_;
    size_type   length_;
};

NS_HWM_END
