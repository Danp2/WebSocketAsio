#include "library.h"
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/regex.hpp>
#include <iomanip>
#include <iostream>
#include <map>

namespace net       = boost::asio;          // from <boost/asio.hpp>
namespace beast     = boost::beast;         // from <boost/beast.hpp>
namespace http      = beast::http;          // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;     // from <boost/beast/websocket.hpp>
using tcp           = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using namespace std::chrono_literals;

#define TRACE(stream, msg) do { stream << L"<WsDll-" ARCH_LABEL L"> " << msg << std::endl; } while(0)
#define VERBOSE(msg) do { if (g_enable_verbose) { TRACE(std::wcout, msg); } } while(0)
#define COUT(msg) TRACE(std::wcout, msg)
#define CERR(msg) TRACE(std::wcerr, msg)

namespace /*anon*/ {
    // Global variables
    static std::atomic_bool g_enable_verbose{false};
    static net::thread_pool g_thread_pool;

    class Session;
    using SessionPtr = std::shared_ptr<Session>;
    using Handle     = size_t;

    struct Manager {

        inline Handle Register(SessionPtr const& sess)
        {
            std::lock_guard lk(mx_);

            garbage_collect();
            assert(sess);

            Handle handle = next_handle_++;
            auto [it, ok] = sessions_.emplace(handle, sess);
            assert(ok);

            return handle;
        }

        inline bool Forget(Handle h)
        {
            std::lock_guard lk(mx_);

            garbage_collect();

            if (auto it = sessions_.find(h); it != end(sessions_)) {
                sessions_.erase(it);
                return true;
            }

            return false;
        }

        inline SessionPtr Active(Handle h)
        {
            std::lock_guard lk(mx_);

            if (auto it = sessions_.find(h); it != end(sessions_))
                return it->second.lock();

            return nullptr;
        }

      private:
        using Registry = std::map<Handle, std::weak_ptr<Session>>;
        std::mutex mx_;
        size_t     next_handle_ = 1;
        Registry   sessions_;

        void garbage_collect() // lock must be held
        {
            for (auto it = begin(sessions_); it != end(sessions_);) {
                if (it->second.expired())
                    it = sessions_.erase(it);
                else
                    ++it;
            }
        }
    } g_session_manager;

    std::string utf8_encode(std::wstring const& wstr)
    {
        if (wstr.empty()) {
            return {};
        }
#if _WIN32 || _WIN64
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
#else // TODO review, should work on windows as well
        std::mbstate_t state{};

        auto in          = wstr.c_str();
        int  size_needed = std::wcsrtombs(nullptr, &in, 0, &state);

        std::string strTo(1 + size_needed, 0);
        size_t n = std::wcsrtombs(&strTo[0], &in, strTo.size(), &state);

        if (n == -1ul) {
            throw std::domain_error("wcsrtombs");
        }
        strTo.resize(n);
        return strTo;
#endif
    }

    // Convert an UTF8 string to a wide Unicode String
    std::wstring utf8_decode(std::string const& str)
    {
        if (str.empty()) {
            return {};
        }
#if _WIN32 || _WIN64
        int          size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
#else
        std::mbstate_t state{};

        char const* in          = str.c_str();
        size_t      size_needed = std::mbsrtowcs(nullptr, &in, 0, &state);

        std::wstring wstrTo(1 + size_needed, 0);
        size_t n = std::mbsrtowcs(&wstrTo[0], &in, wstrTo.size(), &state);

        if (n == -1ul) {
            throw std::domain_error("mbsrtowcs");
        }
        wstrTo.resize(size_needed);
        return wstrTo;
#endif
    }

    class Session : public std::enable_shared_from_this<Session> {
        websocket::stream<beast::tcp_stream> ws_{make_strand(g_thread_pool)};
        tcp::resolver                        resolver_{ws_.get_executor()};

        beast::flat_buffer buffer_;
        std::wstring       host_, port_, path_; // path part in url. For example: /v2/ws

        /// Print error related information in stderr
        /// \param ec instance that contains error related information
        /// \param what customize prefix in output
        void fail(beast::error_code ec, wchar_t const* what)
        {
            std::wstring const msg = what //
                ? what + (L": " + utf8_decode(ec.message()))
                : utf8_decode(ec.message());

            if (ec == net::error::operation_aborted) {
                VERBOSE(msg);
            }
            else {
                if (on_fail_cb)
                    on_fail_cb(handle_, msg.c_str());
                CERR(msg);
            }
        }

#include <boost/asio/yield.hpp>
        struct connect_op {
            using EC  = beast::error_code;
            using EP  = tcp::endpoint;
            using EPS = tcp::resolver::results_type;
            template <typename Self> void operator()(Self& self, EC ec, EPS r) { return call(self, ec, r); }
            template <typename Self> void operator()(Self& self, EC ec, EP) { return call(self, ec); }
            template <typename Self> void operator()(Self& self, EC ec) { return call(self, ec); }
            template <typename Self> void operator()(Self& self) { return call(self); }

            SessionPtr     s;
            net::coroutine coro;

          private:
            template <typename Self>
            void call(Self& self, beast::error_code ec = {}, tcp::resolver::results_type results = {})
            {
                auto& ws_ = s->ws_;
                reenter(coro)
                {
                    yield s->resolver_.async_resolve(utf8_encode(s->host_), utf8_encode(s->port_),
                                                     std::move(self));
                    if (ec) goto complete;

                    beast::get_lowest_layer(ws_).expires_after(30s);
                    yield beast::get_lowest_layer(ws_).async_connect(results, std::move(self));
                    if (ec) goto complete;

                    beast::get_lowest_layer(ws_).expires_never();
                    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
                    ws_.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
                        req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " WsDll");
                    }));

                    // Host HTTP header includes the port. See https://tools.ietf.org/html/rfc7230#section-5.4
                    yield ws_.async_handshake(utf8_encode(s->host_) + ":" + utf8_encode(s->port_),
                                              utf8_encode(s->path_), std::move(self));

complete:
                    s.reset(); // deallocate before completion
                    return self.complete(ec);
                }
            }
        };
#include <boost/asio/yield.hpp>

        template<typename Token>
        auto async_connect(Token&& token) {
            return net::async_compose<Token, void(beast::error_code)>(connect_op{shared_from_this(), {}},
                                                                      token);
        }

      public:
        Handle          handle_          = 0; // TODO make friend/private?
        on_fail_t       on_fail_cb       = nullptr;
        on_disconnect_t on_disconnect_cb = nullptr;
        on_data_t       on_data_cb       = nullptr;

        Session() = default;
        ~Session()
        {
            try {
                if (on_disconnect_cb)
                    std::exchange(on_disconnect_cb, nullptr)(handle_);
            }
            catch (std::exception const& e) {
                // swallow
            }
        }

        /// Start asynchronous operation
        ///
        /// Only returns when connection (attempt) completed
        int run(std::wstring host, std::wstring port, std::wstring path)
        {
            // Save these for later
            host_ = std::move(host);
            port_ = std::move(port);
            path_ = std::move(path);

            VERBOSE(L"Run host_: " << host_ << L", port: " << port_ << L", path_: " << path_);
            try {
                assert(shared_from_this());
                async_connect(net::use_future).get();

                VERBOSE(L"Issue async_read after connect");
                ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
                return 1;
            }
            catch (beast::system_error const& se) {
                fail(se.code(), L"Connection operation");
                return 0;
            }
        }

        /// Send message to remote websocket server
        /// \param data to be sent
        bool send_message(std::wstring const& data)
        {
            std::promise<void> p;
            auto fut = p.get_future();

            try {
                VERBOSE(L"Writing message: " << data);
                post(ws_.get_executor(), [this, &p, udata = utf8_encode(data)]() mutable {
                    ws_.async_write( //
                        net::buffer(udata), [&p](beast::error_code ec, size_t) mutable {
                            if (!ec)
                                p.set_value();
                            else {
                                p.set_exception(std::make_exception_ptr(beast::system_error(ec)));
                            }
                        });
                });
                fut.get();
                return true;
            }
            catch (beast::system_error const& se) {
                fail(se.code(), L"send_message");
                return false;
            }
        }

        /// Close the connect between websocket client and server. It call
        /// async_close to call a callback function which also calls user
        /// registered callback function to deal with close event.
        bool disconnect()
        {
            std::promise<void> p;
            auto fut = p.get_future();

            try {
                post(ws_.get_executor(), [this, &p]() mutable {
                    VERBOSE(L"Disconnecting");
                    ws_.async_close( //
                        websocket::close_code::normal, [&p](beast::error_code ec) mutable {
                            if (!ec)
                                p.set_value();
                            else {
                                p.set_exception(std::make_exception_ptr(beast::system_error(ec)));
                            }
                        });
                });
                fut.get();

                if (on_disconnect_cb)
                    std::exchange(on_disconnect_cb, nullptr)(handle_);

                return g_session_manager.Forget(handle_);
            }
            catch (beast::system_error const& se) {
                fail(se.code(), L"disconnect");
                return false;
            }
        }


      private: // all private (do_*/on_*) assumed on strand
        void on_read(beast::error_code ec, std::size_t bytes_transferred)
        {
            VERBOSE(L"In on_read");

            // error occurs
            if (ec)
                return fail(ec, L"read");

            const std::wstring wdata = utf8_decode(beast::buffers_to_string(buffer_.data()));
            VERBOSE(L"Received[" << bytes_transferred << L"] " << std::quoted(wdata));

            if (on_data_cb)
                on_data_cb(handle_, wdata.c_str(), wdata.length());

            buffer_.consume(bytes_transferred); // some forms of async_read can read extra data

            VERBOSE(L"Issue new async_read in on_read");
            ws_.async_read(buffer_, beast::bind_front_handler(&Session::on_read, shared_from_this()));
        }
    };
}

WSDLLAPI void enable_verbose(intptr_t enabled)
{
    COUT(L"Verbose output " << (enabled ? L"enabled" : L"disabled"));
    g_enable_verbose = enabled;
}


WSDLLAPI websocket_handle_t websocket_connect(wchar_t const* szServer, size_t dwOnFail, size_t dwOnDisconnect, size_t dwOnData)
{
    auto session     = std::make_shared<Session>();
    session->handle_ = g_session_manager.Register(session);

    session->on_fail_cb       = reinterpret_cast<on_fail_t>(dwOnFail);
    session->on_disconnect_cb = reinterpret_cast<on_disconnect_t>(dwOnDisconnect);
    session->on_data_cb       = reinterpret_cast<on_data_t>(dwOnData);

    if (!g_session_manager.Active(session->handle_)) {
        COUT(L"Session rejected"); // shouldn't happen currently
        return 0;
    }

    VERBOSE(L"Connecting to the server: " << szServer);

    static boost::wregex const s_pat(LR"(^wss?://([\w\.]+):(\d+)(.*)$)");

    boost::wcmatch matches;
    if (!boost::regex_match(szServer, matches, s_pat)) {
        COUT(L"Failed to parse host & port. Correct example: ws://localhost:8080/");
        return 0;
    }

    std::wstring path(boost::trim_copy(matches[3].str()));
    if (path.empty())
        path = L"/";

    return session->run(matches[1], matches[2], std::move(path)) ? session->handle_ : Handle{};
}

WSDLLAPI size_t websocket_disconnect(websocket_handle_t h)
{
    if (SessionPtr sess = g_session_manager.Active(h)) {
        return sess->disconnect();
    }
    CERR(L"Session not active. Can't disconnect.");
    return 0;
}

WSDLLAPI size_t websocket_send(websocket_handle_t h, wchar_t const* szMessage, size_t dwLen)
{
    if (SessionPtr sess = g_session_manager.Active(h)) {
        return sess->send_message(std::wstring(szMessage, dwLen));
    }
    CERR(L"Session not active. Can't send data.");
    return 0;
}

WSDLLAPI size_t websocket_isconnected(websocket_handle_t h)
{
    return g_session_manager.Active(h) != nullptr;
}
