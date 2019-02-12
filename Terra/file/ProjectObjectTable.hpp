#pragma once

#include <unordered_map>

#include "../project/GraphProcessor.hpp"
#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

template<class T>
class ObjectTable
{
public:
    using id_type = UInt64;
    
    //! register an object with the key id.
    /*! @return true if registered, false if not because already registered.
     */
    bool Register(id_type key_id, T *obj)
    {
        assert(obj != nullptr);
        if(table_.count(key_id) == 0) {
            table_[key_id] = obj;
            return true;
        } else {
            return false;
        }
    }
    
    //! @return returns the object.
    T * Find(id_type key_id) const
    {
        auto const found = table_.find(key_id);
        if(found == table_.end()) {
            return nullptr;
        }
        
        return found->second;
    }
    
    //! @return true if removed, false if not found.
    bool Remove(id_type key_id)
    {
        if(table_.count(key_id) > 0) {
            table_.erase(key_id);
            return true;
        } else {
            return false;
        }
    }
    
    void ClearTable()
    {
        table_.clear();
    }
    
private:
    std::unordered_map<id_type, T *> table_;
};

class ProjectObjectTable
:   public SingleInstance<ProjectObjectTable>
{
public:
    ObjectTable<GraphProcessor::Node> nodes_;
};

NS_HWM_END
