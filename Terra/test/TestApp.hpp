#pragma once

#include "../misc/SingleInstance.hpp"

NS_HWM_BEGIN

//! TestApp is a minimap wxApp class used for testing.
/*! Some test cases use wxWidgets' functions which expect that there's already an initialized wxApp.
 *  To let the functions work correctly, construct a TestApp object before such test cases started.
 */
class TestApp
:   public wxApp
,   hwm::SingleInstance<TestApp>
{
public:
    using hwm::SingleInstance<TestApp>::GetInstance;
    
    TestApp() {}
    ~TestApp() {}
    
    bool OnInit() override
    {
        return true;
    }
};

NS_HWM_END
