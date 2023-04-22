#include "library.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <type_traits>
using namespace std::chrono_literals;

namespace { // diagnostics tracing helpers
    using std::this_thread::sleep_for;
    static auto timestamp() {
        auto        now   = std::chrono::high_resolution_clock::now;
        static auto start = now();
        auto        t     = now();

        return (t - start) / 1.ms;
    }

    static std::atomic_int tid_gen{0};
    thread_local int       tid = tid_gen++;
    std::mutex             console_mx;

    template <typename... Args>
    void trace(Args const&... args) {
        std::lock_guard<std::mutex> lk(console_mx);
        std::wcout << L"\tThread:" << std::setw(2) << tid << std::right << std::setw(10) << timestamp() << L"ms ";
        (std::wcout << ... << args) << std::endl;
    }

    template <typename F> static intptr_t unsafe_cb(F* f)
    {
        // controlled unsafe casts
        static_assert(sizeof(f) == sizeof(intptr_t));
        return reinterpret_cast<intptr_t>(f);
    }

    #define TRACED(expr)                     \
        [&] {                                \
            auto&& r_ = (expr);              \
            trace(L## #expr, L"\t -> ", r_); \
            return std::move(r_);            \
        }()
} // namespace

int main()
{
    std::wcout << std::fixed << std::setprecision(2);

    struct {
        on_fail_t       on_fail; // allow type safe assignments
        on_connect_t    on_connect;
        on_disconnect_t on_disconnect;
        on_data_t       on_data;

        void do_register() {
            ::websocket_register_on_fail_cb(unsafe_cb(on_fail));
            ::websocket_register_on_data_cb(unsafe_cb(on_data));
            ::websocket_register_on_connect_cb(unsafe_cb(on_connect));
            ::websocket_register_on_disconnect_cb(unsafe_cb(on_disconnect));
        };

    } callbacks{
        [](wchar_t const* wsz) { trace(L"ON_FAIL: ", std::quoted(wsz)); } ,
        []()                   { trace(L"ON_CONNECT");                  } ,
        []()                   { trace(L"ON_DISCONNECT");               } ,
        [](wchar_t const* wsz, size_t n) {
            trace(L"ON_DATA: ", std::quoted(std::wstring_view(wsz).substr(0, n)));
        },
    };

    ::enable_verbose(1);
    callbacks.do_register();

    for (auto delay : {0ms, 200ms}) {
        std::wcout << L"\n=========================================== Start (with " << delay / 1.s
                   << L"s delay) ======\n" << std::endl;

        TRACED(::websocket_isconnected());

        TRACED(::websocket_connect(L"ws://localhost:8080/something"));
        TRACED(::websocket_isconnected());

        if (delay > 0s)
            sleep_for(delay);

        TRACED(::websocket_send(L"First message\n", 14, false));
        if (delay > 0s)
            sleep_for(delay);

        TRACED(::websocket_disconnect());
        TRACED(::websocket_isconnected());
        sleep_for(100ms);
        TRACED(::websocket_isconnected());

        sleep_for(1s);
    }
}
