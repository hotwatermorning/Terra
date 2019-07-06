#include "catch2/catch.hpp"

#include <fstream>

#include "../log/Logger.hpp"
#include "../log/LoggingStrategy.hpp"
#include "../misc/ScopeExit.hpp"

#include "./TestApp.hpp"
#include "./PathUtil.hpp"

using namespace hwm;

class TestLoggingStrategy : public Logger::LoggingStrategy
{
public:
    void OnAfterAssigned(Logger *logger) override {
        num_assigned_ += 1;
    }
    
    void OnBeforeDeassigned(Logger *logger) override {
        num_deassigned_ += 1;
    }
    
    Logger::Error OutputLog(String const &message) override
    {
        history.push_back(message);
        return Logger::Error::NoError();
    }
    
    Int32 num_assigned_ = 0;
    Int32 num_deassigned_ = 0;
    std::vector<String> history;
};

TEST_CASE("Logger test", "[log]")
{
    TestApp app;
    auto const list1 = std::vector<String>{L"abc", L"def", L"ghi"};
    auto const list2 = std::vector<String>{L"abc", L"def", L"ghi", L"jkl"};
    
    SECTION("logging level test") {
        Logger lg;
        lg.SetLoggingLevels(list1);
        
        CHECK(lg.GetLoggingLevels() == list1);
        lg.SetLoggingLevels(list2);
        CHECK(lg.GetLoggingLevels() == list2);
        
        CHECK(lg.IsValidLoggingLevel(L"def") == true);
        CHECK(lg.IsValidLoggingLevel(L"xyz") == false);
        
        CHECK(lg.GetMostDetailedActiveLoggingLevel() == L"jkl");
        CHECK(lg.IsActiveLoggingLevel(L"ghi") == true);
        
        lg.SetMostDetailedActiveLoggingLevel(L"def");
        
        CHECK(lg.GetMostDetailedActiveLoggingLevel() == L"def");
        CHECK(lg.IsActiveLoggingLevel(L"def") == true);
        CHECK(lg.IsActiveLoggingLevel(L"ghi") == false);
    }
    
    SECTION("strategy test") {
        Logger lg;
        auto st = std::make_shared<TestLoggingStrategy>();
        
        CHECK(dynamic_cast<DebugConsoleLoggingStrategy *>(lg.GetStrategy().get()) != nullptr);
        CHECK(st->num_assigned_ == 0);
        CHECK(st->num_deassigned_ == 0);
        
        lg.SetStrategy(st);
        CHECK(lg.GetStrategy() == st);
        CHECK(st->num_assigned_ == 1);
        CHECK(st->num_deassigned_ == 0);
        
        lg.SetStrategy(nullptr);
        CHECK(lg.GetStrategy() == nullptr);
        CHECK(st->num_assigned_ == 1);
        CHECK(st->num_deassigned_ == 1);
        
        lg.SetStrategy(st);
        auto st2 = std::make_shared<TestLoggingStrategy>();
        lg.SetStrategy(st2);
        CHECK(lg.GetStrategy() == st2);
        CHECK(st->num_assigned_ == 2);
        CHECK(st->num_deassigned_ == 2);
        CHECK(st2->num_assigned_ == 1);
        CHECK(st2->num_deassigned_ == 0);
    }
    
    SECTION("logging test") {
        auto st = std::make_shared<TestLoggingStrategy>();
        Logger lg;
        
        lg.SetStrategy(st);
        REQUIRE(lg.GetStrategy() == st);
        
        lg.SetLoggingLevels(list2);
        lg.SetMostDetailedActiveLoggingLevel(L"def");
        
        // before the logging started => failed.
        REQUIRE(lg.IsLoggingStarted() == false);
        auto err = lg.OutputLog(L"abc", []{ return L"hello"; });
        CHECK(err.has_error() == false);
        CHECK(st->history.size() == 0);
        
        // start logging
        lg.StartLogging(true);
        REQUIRE(lg.IsLoggingStarted() == true);
        
        auto ends_with = [](String const &str, String const &suffix) {
            auto sub = str.substr(std::max<size_t>(str.size(), suffix.size()) - suffix.size(),
                                  String::npos);
            return sub == suffix;
        };
        
        // for active logging levels => succeed and logged.
        err = lg.OutputLog(L"abc", []{ return L"hello"; });
        CHECK(err.has_error() == false);
        CHECK((st->history.size() >= 1 && ends_with(st->history.back(), L"hello")));
        
        // for non-active logging levels => succeed but ignored.
        err = lg.OutputLog(L"ghi", []{ return L"world"; });
        REQUIRE(err.has_error() == false);
        CHECK((st->history.size() >= 1 && ends_with(st->history.back(), L"hello")));
        
        // for unknown logging levels => failed.
        err = lg.OutputLog(L"foo", []{ return L"world"; });
        REQUIRE(err.has_error() == true);
        CHECK((st->history.size() >= 1 && ends_with(st->history.back(), L"hello")));
    }
}

TEST_CASE("FileLoggingStrategy test", "[log]")
{
    TestApp app;
    auto scoped_dir = ScopedTemporaryDirectoryProvider(L"logging-test");
    
    auto const test_file_name = L"logging-test.log";
    auto const test_file_path = wxFileName(scoped_dir.GetPath(), test_file_name);
    
    FileLoggingStrategy st(test_file_path.GetFullPath().ToStdWstring());
    
    st.EnableRedirectionToDebugConsole(false);
    
    REQUIRE(st.IsOpenedPermanently() == false);
    
    auto err = st.OpenPermanently();
    REQUIRE(err.has_error() == false);
    REQUIRE(st.IsOpenedPermanently() == true);
    st.Close();
    REQUIRE(st.IsOpenedPermanently() == false);
    
    st.OpenPermanently();
    st.OutputLog(L"hello");
    st.OutputLog(L"world");
    st.OutputLog(L"");
    st.OutputLog(L"c++");
    st.Close();
    
#if defined(_MSC_VER)
    std::ifstream ifs(test_file_path.GetFullPath().ToStdWstring(), std::ios_base::binary|std::ios_base::in);
#else
    std::ifstream ifs(test_file_path.GetFullPath().ToStdString(), std::ios_base::binary|std::ios_base::in);
#endif
    
    char buf[32] = {};
    ifs.read(buf, 31);
    auto const num = ifs.gcount();
    std::string s(buf, buf + num);
#if defined(_MSC_VER)
    REQUIRE(s == "hello\r\nworld\r\n\r\nc++\r\n");
#else
    REQUIRE(s == "hello\nworld\n\nc++\n");
#endif
}
