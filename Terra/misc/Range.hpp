#pragma once

//! @file
/*! simple range operations.
 */

NS_HWM_BEGIN

template<class Range, class Elem>
static constexpr
auto find(Range &range, Elem &&elem)
{
    return std::find(std::begin(range), std::end(range), std::forward<Elem>(elem));
}

template<class Range, class Pred>
static constexpr
auto find_if(Range &range, Pred pred)
{
    return std::find_if(std::begin(range), std::end(range), pred);
}

template<class Range, class T>
static constexpr
auto find_last(Range &range, T &&value)
{
    auto const begin = std::begin(range);
    auto const end = std::end(range);
    auto const rbegin = std::make_reverse_iterator(end);
    auto const rend = std::make_reverse_iterator(begin);
    auto found = std::find(rbegin, rend, std::forward<T>(value));
    
    return (found != rend) ? (++found).base() : end;
}

template<class Range, class Predicate>
static constexpr
auto find_last_if(Range &range, Predicate pred)
{
    auto const begin = std::begin(range);
    auto const end = std::end(range);
    auto const rbegin = std::make_reverse_iterator(end);
    auto const rend = std::make_reverse_iterator(begin);
    auto found = std::find_if(rbegin, rend, pred);
    
    return (found != rend) ? (++found).base() : end;
}

//! erase if found.
/*! @return true if found and erased
 */
template<class Range, class Elem>
static constexpr
bool erase_element(Range &range, Elem &&elem)
{
    auto found = find(range, std::forward<Elem>(elem));
    if(found == std::end(range)) {
        return false;
    } else {
        range.erase(found);
        return true;
    }
}

//! erase if found.
/*! @return true if found and erased
 */
template<class Range, class Pred>
static constexpr
bool erase_element_if(Range &range, Pred pred)
{
    auto found = find(range, pred);
    if(found == std::end(range)) {
        return false;
    } else {
        range.erase(found);
        return true;
    }
}

template<class Range, class Elem>
static constexpr
bool contains(Range &range, Elem &&elem)
{
    return find(range, std::forward<Elem>(elem)) != std::end(range);
}

template<class Range, class Pred>
static constexpr
bool contains_if(Range &range, Pred pred)
{
    return find(range, pred) != std::end(range);
}

template<class Iterator>
struct Range
{
    Range(Iterator begin, Iterator end)
    :   begin_(begin)
    ,   end_(end)
    {}
    
    Iterator begin_;
    Iterator end_;
    
    Iterator begin() const { return begin_; }
    Iterator end() const { return end_; }
};

template<class Iterator>
Range<Iterator> MakeIteratorRange(Iterator begin, Iterator end)
{
    return Range<Iterator>{begin, end};
}

template<class RangeType>
auto reversed(RangeType &r)
{
    auto begin = std::rbegin(r);
    auto end = std::rend(r);
    return Range<decltype(begin)>{begin, end};
}


NS_HWM_END
