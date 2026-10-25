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
#include "PathBrowser.h"
#include <cstdlib>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace Browser
{

#ifdef _WIN32

std::vector<Drive> listDrives()
{
    std::vector<Drive> drives;
    // An empty card reader or disc drive must not bring up "There is no disk in the drive"
    DWORD oldMode = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);
    char buffer[512] = {};
    DWORD length = GetLogicalDriveStringsA(sizeof(buffer) - 1, buffer);
    for (const char* root = buffer; length > 0 && length < sizeof(buffer) && *root; root += strlen(root) + 1) {
        Drive drive;
        drive.path = root;
        UINT type = GetDriveTypeA(root);
        switch (type) {
        case DRIVE_FIXED: drive.kind = Drive::Kind::Fixed; break;
        case DRIVE_REMOVABLE: drive.kind = Drive::Kind::Removable; break;
        case DRIVE_REMOTE: drive.kind = Drive::Kind::Network; break;
        case DRIVE_CDROM: drive.kind = Drive::Kind::Optical; break;
        case DRIVE_NO_ROOT_DIR: continue;
        default: drive.kind = Drive::Kind::Other; break;
        }
        // Network and disc drives aren't asked anything: a lost share or a
        // disc spinning up would hold the screen for seconds
        if (drive.kind == Drive::Kind::Fixed || drive.kind == Drive::Kind::Removable) {
            char label[MAX_PATH + 1] = {};
            if (!GetVolumeInformationA(root, label, sizeof(label), nullptr, nullptr, nullptr, nullptr, 0)) {
                if (drive.kind == Drive::Kind::Removable) {
                    continue;   // a card reader with no card in it
                }
            }
            drive.name = label;
            ULARGE_INTEGER free{};
            if (GetDiskFreeSpaceExA(root, &free, nullptr, nullptr)) {
                drive.free = free.QuadPart;
            }
        }
        drives.push_back(drive);
    }
    SetThreadErrorMode(oldMode, nullptr);
    return drives;
}

std::vector<Place> userFolders()
{
    std::vector<Place> places;
    const char* profile = std::getenv("USERPROFILE");
    if (!profile || !*profile) {
        return places;
    }
    for (const char* name : { "Downloads", "Desktop" }) {
        Place place;
        place.path = fs::path(profile) / name;
        place.detail = name;
        places.push_back(place);
    }
    return places;
}

#else

std::vector<Drive> listDrives()
{
    std::vector<Drive> drives;
    Drive top;
    top.path = "/";
    drives.push_back(top);
    // Where USB sticks and other drives are mounted
    std::vector<fs::path> mounts = { "/media", "/mnt", "/Volumes" };
    if (const char* user = std::getenv("USER")) {
        mounts.insert(mounts.begin(), fs::path("/run/media") / user);
        mounts.insert(mounts.begin(), fs::path("/media") / user);
    }
    for (const fs::path& mount : mounts) {
        std::error_code ec;
        for (fs::directory_iterator it(mount, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fec;
            if (it->is_directory(fec) && !isHidden(it->path().filename().string())) {
                Drive drive;
                drive.path = it->path().string();
                drive.kind = Drive::Kind::Removable;
                drives.push_back(drive);
            }
        }
    }
    return drives;
}

std::vector<Place> userFolders()
{
    std::vector<Place> places;
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return places;
    }
    Place homePlace;
    homePlace.path = home;
    homePlace.detail = "Home";
    places.push_back(homePlace);
    for (const char* name : { "Downloads", "Desktop" }) {
        Place place;
        place.path = fs::path(home) / name;
        place.detail = name;
        places.push_back(place);
    }
    return places;
}

#endif

}
