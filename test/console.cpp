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

    static std::atomic_int tid_gen{0};
    thread_local int       tid = tid_gen++;
    std::mutex             console_mx;

    template <typename... Args>
    void trace(Args const&... args) {
        std::lock_guard<std::mutex> lk(console_mx);
        std::wcout << L"\tThread:" << std::setw(2) << tid << L" ";
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

    on_fail_t       on_fail       = [](websocket_handle_t h, wchar_t const* wsz) { trace(L"ON_FAIL handle#", h, ": ", std::quoted(wsz)); };
    on_disconnect_t on_disconnect = [](websocket_handle_t h) { trace(L"ON_DISCONNECT handle#", h); };
    on_data_t       on_data       = [](websocket_handle_t h, wchar_t const* wsz, size_t n) { trace(L"ON_DATA handle#", h, ": ", std::quoted(std::wstring_view(wsz).substr(0, n))); };

    ::enable_verbose(1);

    auto url = L"ws://localhost:8080/something";

    {
        std::wcout << L"\n======================= First ==============\n" << std::endl;

        websocket_handle_t h {};
        TRACED(::websocket_isconnected(h));
        h = TRACED(::websocket_connect(url, unsafe_cb(on_fail), unsafe_cb(on_disconnect), unsafe_cb(on_data)));
        TRACED(::websocket_isconnected(h));

        TRACED(::websocket_send(h, L"First message\n", 14));

        TRACED(::websocket_disconnect(h));
        TRACED(::websocket_isconnected(h));
    }

    {
        std::wcout << L"\n======================= Second ==============\n" << std::endl;

        websocket_handle_t h {};
        TRACED(::websocket_isconnected(h));
        h = TRACED(::websocket_connect(url, unsafe_cb(on_fail), unsafe_cb(on_disconnect), unsafe_cb(on_data)));
        TRACED(::websocket_isconnected(h));

        sleep_for(2s);
        TRACED(::websocket_send(h, L"Second message\n", 15));

        sleep_for(2s);
        TRACED(::websocket_disconnect(h));
        TRACED(::websocket_isconnected(h));
    }
}
