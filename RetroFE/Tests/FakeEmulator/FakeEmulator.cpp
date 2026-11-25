/* This file is part of RetroFE.
 *
 * RetroFE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RetroFE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RetroFE.  If not, see <http://www.gnu.org/licenses/>.
 */

// retrofe_fake_emulator: an emulator for the tests (FirstSetupTests). Each run
// appends to launches.txt beside its own executable (on Linux and macOS, the
// path it was started by) "launch", its current directory, each argument on
// its own line, then "end", and exits 0.

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
    // Started by its path (as RetroFE starts a launcher's program): beside
    // that path, which may be a symlink to this one
    std::string self = argv[0] ? argv[0] : "";
    if (self.find('/') == std::string::npos) {
        char path[4096];
        auto length = readlink("/proc/self/exe", path, sizeof(path));
        if (length < 0) return 1;
        self.assign(path, static_cast<size_t>(length));
    }
    std::ofstream out(std::filesystem::path(self).parent_path() / "launches.txt", std::ios::app | std::ios::binary);
    out << "launch\n" << std::filesystem::current_path().string() << '\n';
    for (int i = 1; i < argc; ++i) out << argv[i] << '\n';
    out << "end\n";
}
#endif
