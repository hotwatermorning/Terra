#include "catch2/catch.hpp"

#include <fstream>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <string_view>

#include <wx/ffile.h>

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

TEST_CASE("global logger access test", "[log]")
{
    CHECK(GetGlobalLogger() == nullptr);
    
    auto tmp = std::make_unique<Logger>();
    auto new_logger = tmp.get();
    auto prev = ReplaceGlobalLogger(std::move(tmp));
    
    CHECK(prev == nullptr);
    CHECK(GetGlobalLogger() == new_logger);
    
    int const kMaxShare = 50;
    std::atomic<bool> start_trying_to_replace {false};
    std::atomic<bool> still_blocked {true};
    
    std::vector<LoggerRef> ls;
    for(int i = 0; i < kMaxShare; ++i) {
        ls.push_back(GetGlobalLogger());
    }
    
    auto th = std::thread([&] {
        start_trying_to_replace.store(true);
        
        // ReplaceGlobalLogger() will be blocked until all LoggerRefs are destructed.
        ReplaceGlobalLogger(nullptr);
        still_blocked.store(false);
    });
    
    for( ; ; ) {
        if(start_trying_to_replace.load()) { break; }
        std::this_thread::yield();
    }
    
    for(int i = 0; i < kMaxShare; ++i) {
        REQUIRE(still_blocked.load() == true);
        ls.pop_back();
        std::this_thread::yield();
    }
    
    for(Int32 i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(still_blocked.load() == false) {
            break;
        }
    }
    
    CHECK(still_blocked.load() == false);
    
    th.join();
}

TEST_CASE("multithread logging test", "[log]")
{
    auto st = std::make_shared<TestLoggingStrategy>();
    Logger lg;
    
    lg.SetLoggingLevels({L"Info"});
    lg.SetStrategy(st);
    lg.StartLogging(true);
    
    int const kNumThreads = 50;
    int const kMsgPerThread = 1000;
    bool is_ready_to_output = false;
    LockFactory lf;
    std::condition_variable cv;
    
    std::vector<std::thread> ths;
    for(int t = 0; t < kNumThreads; ++t) {
        ths.push_back(std::thread([&, t] {
            {
                auto lock = lf.make_lock();
                cv.wait(lock, [&] { return is_ready_to_output; });
            }
            
            for(int i = 0; i < kMsgPerThread; ++i) {
                lg.OutputLog(L"Info", [&, t, i]{ return L"[{:02d}]({:03d}): hello"_format(t, i); });
            }
        }));
    }
    
    {
        auto lock = lf.make_lock();
        is_ready_to_output = true;
    }

    // let all threads start logging.
    cv.notify_all();
    
    // all threads should successfully finished.
    for(int t = 0; t < kNumThreads; ++t) {
        ths[t].join();
    }
    
    CHECK(st->history.size() == kNumThreads * kMsgPerThread);
    
    std::vector<std::wstring_view> tmp;
    tmp.reserve(kNumThreads * kMsgPerThread);
    for(auto const &s: st->history) {
        tmp.push_back(std::wstring_view(s).substr(s.size() - 16, String::npos));
    }
    std::sort(tmp.begin(), tmp.end());
    
    for(int t = 0; t < kNumThreads; ++t) {
        for(int i = 0; i < kMsgPerThread; ++i) {
            if(tmp[t * kMsgPerThread + i] != L"[{:02d}]({:03d}): hello"_format(t, i)) {
                REQUIRE(tmp[t * kMsgPerThread + i] == L"[{:02d}]({:03d}): hello"_format(t, i));
            }
        }
    }
}

TEST_CASE("logfile rotation test", "[log]")
{
    auto get_file_size = [](wxFileName path) {
        wxFFile tmp(path.GetFullPath(), "rb");
        assert(tmp.IsOpened());
        return tmp.Length();
    };
    
    SECTION("rotate function test") {
        TestApp app;
        auto scoped_dir = ScopedTemporaryDirectoryProvider(L"logging-test");
        
        auto const test_file_name = L"logging-test.log";
        auto const test_file_path = wxFileName(scoped_dir.GetPath(), test_file_name);

#if defined(_MSC_VER)
        std::ofstream ofs(test_file_path.GetFullPath().ToStdWstring(), std::ios::binary|std::ios::out|std::ios_base::trunc);
#else
        std::ofstream ofs(test_file_path.GetFullPath().ToStdString(), std::ios::binary|std::ios::out|std::ios_base::trunc);
#endif
        
        REQUIRE(ofs.is_open());
        
        std::string test_str = "hello, world";
        ofs.write(test_str.data(), test_str.size());
        ofs.close();
        REQUIRE(get_file_size(test_file_path) == test_str.size());
        
        auto err = FileLoggingStrategy::Rotate(test_file_path.GetFullPath().ToStdWstring(), 4);
        CHECK(err.has_error() == false);
        REQUIRE(get_file_size(test_file_path) == 4);
        
        std::string tmp;
        tmp.resize(4);
        
#if defined(_MSC_VER)
        std::ifstream ifs(test_file_path.GetFullPath().ToStdWstring(), std::ios::binary|std::ios::in);
#else
        std::ifstream ifs(test_file_path.GetFullPath().ToStdString(), std::ios::binary|std::ios::in);
#endif
        
        ifs.read(tmp.data(), tmp.size());
        REQUIRE(tmp == "orld");
    }
    
    SECTION("rotated file size test") {
        TestApp app;
        auto scoped_dir = ScopedTemporaryDirectoryProvider(L"logging-test");
        
        auto const test_file_name = L"logging-test.log";
        auto const test_file_path = wxFileName(scoped_dir.GetPath(), test_file_name);
        
        auto st = std::make_shared<FileLoggingStrategy>(test_file_path.GetFullPath().ToStdWstring());
        
        st->EnableRedirectionToDebugConsole(false);
        st->SetFileSizeLimit(10000);
        
        for(int i = 0; i < 1000; ++i) {
            st->OutputLog(L"hello world. hello world. hello world.");
            auto const file_size = get_file_size(test_file_path);
            if(st->GetFileSizeLimit() < file_size) { // reduce too-much tracing
                REQUIRE(st->GetFileSizeLimit() >= file_size);
            }
        }
        
        st->SetFileSizeLimit(1000);
        st->OpenPermanently();
        REQUIRE(st->GetFileSizeLimit() >= get_file_size(test_file_path));
    }
}
