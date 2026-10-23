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
#include "FileDialog.h"
#include "../Utility/Log.h"

#ifdef WIN32
#include <SDL2/SDL_syswm.h>
#include <Windows.h>
#include <shobjidl.h>

namespace
{
    // RetroFE handles paths as narrow strings in the system code page
    std::wstring toWide(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int length = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        std::wstring result(length, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, result.data(), length);
        result.resize(length - 1);
        return result;
    }

    std::string fromWide(const wchar_t* s)
    {
        int length = WideCharToMultiByte(CP_ACP, 0, s, -1, nullptr, 0, nullptr, nullptr);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_ACP, 0, s, -1, result.data(), length, nullptr, nullptr);
        result.resize(length - 1);
        return result;
    }

    HWND windowHandle(SDL_Window* window)
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (window && SDL_GetWindowWMInfo(window, &info)) {
            return info.info.win.window;
        }
        return nullptr;
    }

    bool show(SDL_Window* owner, const std::string& title, bool folders,
              const std::string& filterName, const std::string& pattern, std::string& path)
    {
        HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool uninitialize = SUCCEEDED(init);

        IFileOpenDialog* dialog = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
        if (FAILED(hr)) {
            LOG_ERROR("FileDialog", "Could not create the file dialog");
            if (uninitialize) CoUninitialize();
            return false;
        }

        DWORD options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | (folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
        dialog->SetOptions(options);
        dialog->SetTitle(toWide(title).c_str());

        std::wstring wideFilterName = toWide(filterName);
        std::wstring widePattern = toWide(pattern);
        if (!folders && !pattern.empty()) {
            COMDLG_FILTERSPEC filters[] = { { wideFilterName.c_str(), widePattern.c_str() }, { L"All files", L"*.*" } };
            dialog->SetFileTypes(2, filters);
        }

        // Start in the current folder, or the folder the current file is in
        if (!path.empty()) {
            std::wstring start = toWide(path);
            DWORD attributes = GetFileAttributesW(start.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                start = start.substr(0, start.find_last_of(L"\\/"));
            }
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }

        // Let the mouse leave the window while the dialog is up
        SDL_bool grabbed = owner ? SDL_GetWindowGrab(owner) : SDL_FALSE;
        if (owner) SDL_SetWindowGrab(owner, SDL_FALSE);

        bool chosen = false;
        if (SUCCEEDED(dialog->Show(windowHandle(owner)))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR selected = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
                    path = fromWide(selected);
                    CoTaskMemFree(selected);
                    chosen = true;
                }
                item->Release();
            }
        }

        if (owner) SDL_SetWindowGrab(owner, grabbed);
        dialog->Release();
        if (uninitialize) CoUninitialize();
        return chosen;
    }
}

namespace FileDialog
{
    bool isSupported() { return true; }

    bool pickFolder(SDL_Window* owner, const std::string& title, std::string& path)
    {
        return show(owner, title, true, "", "", path);
    }

    bool pickFile(SDL_Window* owner, const std::string& title, const std::string& filterName,
                  const std::string& pattern, std::string& path)
    {
        return show(owner, title, false, filterName, pattern, path);
    }
}

#else

namespace FileDialog
{
    bool isSupported() { return false; }

    bool pickFolder(SDL_Window*, const std::string&, std::string&) { return false; }

    bool pickFile(SDL_Window*, const std::string&, const std::string&, const std::string&, std::string&) { return false; }
}

#endif
