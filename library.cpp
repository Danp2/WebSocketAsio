#include "library.hpp"

int EnableVerbose = 0;

class session;

on_connect_t on_connect_cb = nullptr;
on_fail_t on_fail_cb = nullptr;
on_disconnect_t on_disconnect_cb = nullptr;
on_data_t on_data_cb = nullptr;

// Global variables
net::io_context Ioc;
// Global instance of session class which contains callback functions to handle websocket async operations.
std::shared_ptr<session> Session_Ioc;
// Make sure the io_context thread exists through whole dll lifecycle until Ioc.run() is unblocked
boost::thread New_Thread;
// This is for making sure we won't get dirty Is_Connected value from main thread or io_context thread
boost::mutex mtx_;
// To indicate if the server is running or not
bool Is_Connected = false;

EXPORT void enable_verbose(intptr_t enabled) {
	{
		boost::lock_guard<boost::mutex> guard(mtx_);
		if(enabled)
			std::wcout << L"<WsDll-" ARCH_LABEL "> Verbose output enabled" << std::endl;
		else
			std::wcout << L"<WsDll-" ARCH_LABEL "> Verbose output disabled" << std::endl;
	}		

    EnableVerbose = enabled;
}

EXPORT size_t websocket_connect(const wchar_t *szServer) {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is running. Can't run again." << std::endl;
            return 0;
        }
    }

    boost::regex pat(R"(^wss?://([\w\.]+):(\d+)(.*)$)");

    const std::wstring line(szServer);
    const std::string utf_line = utf8_encode(line);

    boost::smatch matches;
    if (!boost::regex_match(utf_line, matches, pat)) {
        std::wcerr << L"<WsDll-" ARCH_LABEL "> failed to parse host & port. Correct example: ws://localhost:8080/" << std::endl;
        return 0;
    }

    const std::wstring host(matches[1].begin(), matches[1].end());
    const std::wstring port(matches[2].begin(), matches[2].end());
    const std::wstring path(matches[3].begin(), matches[3].end());
    if(EnableVerbose)
	{
		boost::lock_guard<boost::mutex> guard(mtx_);
		std::wcout << L"<WsDll-" ARCH_LABEL "> host: " << host << L", port: " << port << L", path: " << path << std::endl;
        std::wcout << L"<WsDll-" ARCH_LABEL "> Connecting to the server: " << szServer << std::endl;
	}		

    Session_Ioc = std::make_shared<session>(Ioc);
    // must pass value to lambda otherwise it will cause unexpected exit (no any error message)
    New_Thread = boost::thread([path, host, port]() {
        if(EnableVerbose)
        {
            boost::lock_guard<boost::mutex> guard(mtx_);
            std::wcout << L"<WsDll-" ARCH_LABEL "> in thread" << std::endl;
        }		
        Ioc.stop();
        Ioc.reset();
        std::wstring tmp_path = path;
        boost::trim(tmp_path);
        if(tmp_path.empty()) {
            tmp_path.append(L"/");
        }
        Session_Ioc->run(host.c_str(), port.c_str(), tmp_path.c_str(), L"");
        // {
        //     boost::lock_guard<boost::mutex> guard(mtx_);
        //     Is_Connected = true;
        // }
        if(EnableVerbose)
        {
            boost::lock_guard<boost::mutex> guard(mtx_);
            std::wcout << L"<WsDll-" ARCH_LABEL "> Calling Ioc.run()" << std::endl;
        }		
        Ioc.run();
        {
            boost::lock_guard<boost::mutex> guard(mtx_);

			if(EnableVerbose)
				std::wcout << L"<WsDll-" ARCH_LABEL "> After calling Ioc.run()" << std::endl;

            Is_Connected = false;
        }
        Ioc.stop();
    });

    return 1;
}

EXPORT size_t websocket_disconnect() {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(!Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is not running. Can't disconnect." << std::endl;
            return 0;
        }
    }
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        Is_Connected = false;
        if(EnableVerbose)
            std::wcout << L"<WsDll-" ARCH_LABEL "> Connection is closed after Ioc.run() is completed." << std::endl;
    }

    Session_Ioc->disconnect();
    return 1;
}

EXPORT size_t websocket_send(const wchar_t *szMessage, size_t dwLen, bool isBinary) {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        if(!Is_Connected) {
            std::wcerr << L"<WsDll-" ARCH_LABEL "> Server is not running. Can't send data." << std::endl;
            return 0;
        }
    }

    Session_Ioc->send_message(szMessage);

    return 1;
}

EXPORT size_t websocket_isconnected() {
    {
        boost::lock_guard<boost::mutex> guard(mtx_);
        return Is_Connected ? 1 : 0;
    }
}

EXPORT size_t websocket_register_on_connect_cb(size_t dwAddress) {
    if(EnableVerbose)
	{
		boost::lock_guard<boost::mutex> guard(mtx_);
		std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_connect callback" << std::endl;
	}		
    on_connect_cb = reinterpret_cast<on_connect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_fail_cb(size_t dwAddress) {
    if(EnableVerbose)
 	{
		boost::lock_guard<boost::mutex> guard(mtx_);
		std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_fail callback" << std::endl;
	}
    on_fail_cb = reinterpret_cast<on_fail_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_disconnect_cb(size_t dwAddress) {
    if(EnableVerbose)
  	{
		boost::lock_guard<boost::mutex> guard(mtx_);
		std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_disconnect callback" << std::endl;
	}
    on_disconnect_cb = reinterpret_cast<on_disconnect_t>(dwAddress);

    return 1;
}

EXPORT size_t websocket_register_on_data_cb(size_t dwAddress) {
    if(EnableVerbose)
 	{
		boost::lock_guard<boost::mutex> guard(mtx_);
        std::wcout << L"<WsDll-" ARCH_LABEL "> registering on_data callback" << std::endl;
	}
    on_data_cb = reinterpret_cast<on_data_t>(dwAddress);

    return 1;
}


// EXPORT void enable_verbose(intptr_t enabled);
// EXPORT size_t websocket_connect(const wchar_t *szServer);
// EXPORT size_t websocket_disconnect();
// EXPORT size_t websocket_send(const wchar_t *szMessage, size_t dwLen, bool isBinary);
// EXPORT size_t websocket_isconnected();

// EXPORT size_t websocket_register_on_connect_cb(size_t dwAddress);
// EXPORT size_t websocket_register_on_fail_cb(size_t dwAddress);
// EXPORT size_t websocket_register_on_disconnect_cb(size_t dwAddress);
// EXPORT size_t websocket_register_on_data_cb(size_t dwAddress);