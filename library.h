#ifndef WebSocketAsio_LIBRARY_H
#define WebSocketAsio_LIBRARY_H
#include <cinttypes>

#if _WIN32 || _WIN64
    #ifdef BUILDING_WSDLL
        #define WSDLLAPI extern "C" __declspec(dllexport)
    #else
        #define WSDLLAPI extern "C" __declspec(dllimport)
    #endif

    #if _WIN64
        #define ENVIRONMENT64
        #define ARCH_LABEL L"x64"
    #else
        #define ENVIRONMENT32
        #define ARCH_LABEL L"x86"
    #endif
#else
    #define ENVIRONMENT64
    #define ARCH_LABEL L"x64"
    #define WSDLLAPI extern "C"
#endif

#include <cstddef>
extern "C" {
    // typedef void (*on_connect_t)();
    typedef void (*on_fail_t)(wchar_t const* from);
    typedef void (*on_disconnect_t)();
    typedef void (*on_data_t)(wchar_t const*, size_t);

    WSDLLAPI void   enable_verbose(intptr_t enabled);
    WSDLLAPI size_t websocket_connect(wchar_t const* szServer);
    WSDLLAPI size_t websocket_disconnect();
    WSDLLAPI size_t websocket_send(wchar_t const* szMessage, size_t dwLen, bool isBinary);
    WSDLLAPI size_t websocket_isconnected();

    WSDLLAPI size_t websocket_register_on_connect_cb(size_t dwAddress);
    WSDLLAPI size_t websocket_register_on_fail_cb(size_t dwAddress);
    WSDLLAPI size_t websocket_register_on_disconnect_cb(size_t dwAddress);
    WSDLLAPI size_t websocket_register_on_data_cb(size_t dwAddress);
}

#endif // WebSocketAsio_LIBRARY_H
