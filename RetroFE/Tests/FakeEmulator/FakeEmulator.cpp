#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <Windows.h>

static std::string utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr);
    return result;
}

int wmain(int argc, wchar_t* argv[])
{
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    std::ofstream out(std::filesystem::path(std::wstring(path, length)).parent_path() / L"launches.txt", std::ios::app | std::ios::binary);
    out << "launch\n" << utf8(std::filesystem::current_path().wstring()) << '\n';
    for (int i = 1; i < argc; ++i) out << utf8(argv[i]) << '\n';
    out << "end\n";
}
#else
#include <unistd.h>
int main(int argc, char* argv[])
{
    char path[4096];
    auto length = readlink("/proc/self/exe", path, sizeof(path));
    if (length < 0) return 1;
    std::ofstream out(std::filesystem::path(std::string(path, length)).parent_path() / "launches.txt", std::ios::app | std::ios::binary);
    out << "launch\n" << std::filesystem::current_path().string() << '\n';
    for (int i = 1; i < argc; ++i) out << argv[i] << '\n';
    out << "end\n";
}
#endif
