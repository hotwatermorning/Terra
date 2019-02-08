#pragma once

#include <algorithm>
#include <vector>

NS_HWM_BEGIN

template<class T>
struct DefaultExtractor
{
    using id_type = decltype(std::declval<T>().id_);
    id_type operator()(T const &info) const { return info.id_; }
};

template<class T, class Extractor = DefaultExtractor<T>>
class IdentifiedValueList
{
public:
    using value_type = T;
    using container = std::vector<value_type>;
    using iterator = typename container::iterator;
    using const_iterator = typename container::const_iterator;
    using extractor_type = Extractor;
    using id_type = decltype(std::declval<extractor_type>()(std::declval<T>()));
    typedef size_t size_type;
    
    T const & GetItemByID(id_type id) const
    {
        return list_[GetIndexByID(id)];
    }
    
    std::optional<T> FindItemByID(id_type id) const
    {
        auto const index = GetIndexByID(id);
        if(index == -1) {
            return std::nullopt;
        } else {
            return GetItemByIndex(index);
        }
    }
    
    T const & GetItemByIndex(size_type index) const
    {
        assert(index < list_.size());
        return list_[index];
    }
    
    //! @return (size_type)-1 if not found.
    size_type GetIndexByID(id_type id) const
    {
        auto found = std::find_if(list_.begin(),
                                  list_.end(),
                                  [id](auto &x) { return Extractor{}(x) == id; }
                                  );
        if(found == list_.end()) { return -1; }
        else { return found - list_.begin(); }
    }
    
    void AddItem(T const &item)
    {
        auto new_id = Extractor{}(item);
        assert(GetIndexByID(new_id) == -1);
        list_.push_back(item);
    }
    
    size_type size() const { return list_.size(); }
    bool empty() const { return list_.empty(); }
    
    const_iterator begin() const { return list_.begin(); }
    const_iterator end() const { return list_.end(); }
    
private:
    std::vector<T> list_;
};

NS_HWM_END
