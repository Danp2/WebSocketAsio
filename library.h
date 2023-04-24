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
    typedef intptr_t websocket_handle_t;
    typedef void (*on_fail_t)(websocket_handle_t, wchar_t const* from);
    typedef void (*on_disconnect_t)(websocket_handle_t);
    typedef void (*on_data_t)(websocket_handle_t, wchar_t const*, size_t);

    WSDLLAPI websocket_handle_t websocket_connect(wchar_t const* szServer, size_t dwOnFail,
                                                  size_t dwOnDisconnect, size_t dwOnData);

    WSDLLAPI void   enable_verbose(intptr_t enabled);
    WSDLLAPI size_t websocket_disconnect(websocket_handle_t);
    WSDLLAPI size_t websocket_send(websocket_handle_t, wchar_t const* szMessage, size_t dwLen);
    WSDLLAPI size_t websocket_isconnected(websocket_handle_t);
}

#endif // WebSocketAsio_LIBRARY_H
