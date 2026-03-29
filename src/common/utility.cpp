#ifdef _WIN32
#include <Windows.h>
#endif

#include "utility.hpp"

namespace util {

#ifdef _WIN32
// windows users most likely don't like stderr, as with subsystem:windows they
// will not see it and it makes debugging a chore on users pc
auto reportFatalError(std::string_view error_message) -> void {
    std::wstring wstr_msg;
    auto result =
        ::MultiByteToWideChar(CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), nullptr, 0);

    if (result <= 0) {
        goto fatal_conversion_error;
    }

    // why +10? this is how its done on msdn, im not sure why
    // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
    wstr_msg.resize(result + 10);
    result = ::MultiByteToWideChar(
        CP_UTF8, 0, error_message.data(), static_cast<int>(error_message.size()), wstr_msg.data(),
        static_cast<int>(wstr_msg.size()));

    if (result <= 0) {
        goto fatal_conversion_error;
    }

    MessageBoxW(NULL, wstr_msg.data(), L"fatal error", MB_OK | MB_ICONERROR);
    return;

fatal_conversion_error:
    MessageBoxW(
        NULL,
        L"fatal error has occured when converting the error message from "
        L"utf-8, this is probably a memory "
        L"corruption",
        L"fatal error", MB_OK | MB_ICONERROR);
}
#else
// for unix users stderr is natural place for fatal error to be reported, so
// just let it be printed there
void reportFatalError(std::string_view error_message) { fmt::println(stderr, "fatal error: {}", error_message); }
#endif

} // namespace util
