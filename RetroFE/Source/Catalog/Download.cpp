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
#include "Download.h"
#include "Hash.h"
#include "Index.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>
#include <thread>

namespace fs = std::filesystem;

namespace
{
    const uint64_t margin = 64ull * 1024 * 1024;

    // The file name a URL or path ends in, for messages
    std::string displayName(const std::string& location)
    {
        std::string name = location.substr(0, location.find_first_of("?#"));
        size_t slash = name.find_last_of("/\\");
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }

    uint64_t sizeOf(const fs::path& path)
    {
        std::error_code ec;
        uint64_t size = fs::file_size(path, ec);
        return ec ? 0 : size;
    }
}

namespace Catalog
{
    std::string describeSize(uint64_t bytes)
    {
        const char* units[] = { "bytes", "KB", "MB", "GB", "TB" };
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024 && unit < 4) {
            value /= 1024;
            ++unit;
        }
        char text[32];
        snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
        return text;
    }

    Resume planResume(bool exists, uint64_t partSize, uint64_t expected)
    {
        if (!exists || partSize == 0) return Resume::Fresh;
        if (expected == 0) return Resume::Continue;
        if (partSize == expected) return Resume::Complete;
        if (partSize > expected) return Resume::Discard;
        return Resume::Continue;
    }

    int backoffDelay(int attempt)
    {
        int delay = 2000;
        for (int i = 1; i < attempt && delay < 60000; ++i) {
            delay *= 2;
        }
        return std::min(delay, 60000);
    }

    uint64_t spaceNeeded(uint64_t total, uint64_t alreadyDownloaded)
    {
        uint64_t remaining = total > alreadyDownloaded ? total - alreadyDownloaded : 0;
        return remaining + total + margin;
    }

    bool verifyFile(const std::string& path, const PackageFile& file, std::string& error)
    {
        std::string name = displayName(file.url);
        std::error_code ec;
        uint64_t size = fs::file_size(path, ec);
        if (ec) {
            error = "Can't read " + name;
            return false;
        }
        if (file.size > 0 && size != file.size) {
            error = name + " is " + describeSize(size) + " but should be " + describeSize(file.size);
            return false;
        }
        std::string expected = !file.sha256.empty() ? file.sha256 : file.md5;
        if (!expected.empty()) {
            std::string actual = !file.sha256.empty() ? sha256File(path) : md5File(path);
            if (actual != expected) {
                error = name + " doesn't match its checksum: it's damaged, or changed on the server";
                return false;
            }
        }
        return true;
    }

    Downloader::Downloader(Http* http, const std::string& downloadsFolder)
        : http_(http)
        , folder_(downloadsFolder)
    {
    }

    bool Downloader::fetch(const Package& package, const Progress& progress, Result& result)
    {
        result = Result();
        if (!isValidId(package.id)) {
            result.error = "Invalid package id: " + package.id;
            return false;
        }
        fs::path folder = fs::path(folder_) / package.id;

        // Where each remote file goes, and how much of it is here already
        std::vector<fs::path> finals(package.files.size());
        std::set<fs::path> keep;
        uint64_t total = 0;
        uint64_t remoteTotal = 0;
        uint64_t already = 0;
        bool sizesKnown = true;
        bool anyRemote = false;
        for (size_t i = 0; i < package.files.size(); ++i) {
            const PackageFile& file = package.files[i];
            total += file.size;
            sizesKnown = sizesKnown && file.size > 0;
            if (!isUrl(file.url)) continue;
            anyRemote = true;
            Sha256 key;
            std::string identity = file.url + "|" + std::to_string(file.size) + "|" + file.sha256 + "|" + file.md5;
            key.update(identity.data(), identity.size());
            finals[i] = folder / (std::to_string(i) + "-" + key.hex().substr(0, 12));
            fs::path part = finals[i];
            part += ".part";
            keep.insert(finals[i]);
            keep.insert(part);
            remoteTotal += file.size;
            already += fs::exists(finals[i]) ? sizeOf(finals[i]) : sizeOf(part);
        }
        if (!sizesKnown) total = 0;

        if (anyRemote && !http_) {
            result.error = "Downloading isn't supported on this system yet; use a folder source";
            return false;
        }

        // Earlier versions' leftovers would only take up space
        std::error_code ec;
        if (fs::is_directory(folder, ec)) {
            for (const auto& entry : fs::directory_iterator(folder, ec)) {
                if (!keep.count(entry.path())) {
                    fs::remove_all(entry.path(), ec);
                }
            }
        }

        if (anyRemote) {
            fs::create_directories(folder, ec);
            if (ec) {
                result.error = "Could not create " + folder.string();
                return false;
            }
            uint64_t available = 0;
            bool known = false;
            if (freeSpace) {
                known = freeSpace(folder.string(), available);
            }
            else {
                fs::space_info space = fs::space(folder, ec);
                known = !ec;
                available = space.available;
            }
            uint64_t needed = spaceNeeded(remoteTotal, already);
            if (known && remoteTotal > 0 && available < needed) {
                result.error = "Not enough free space: " + package.name + " needs " + describeSize(needed)
                             + " and the drive has " + describeSize(available);
                return false;
            }
        }

        uint64_t done = 0;
        for (size_t i = 0; i < package.files.size(); ++i) {
            const PackageFile& file = package.files[i];
            if (finals[i].empty()) {
                // From a folder source: use it where it is
                if (!fs::exists(file.url, ec)) {
                    result.error = "Can't find " + file.url;
                    return false;
                }
                if (!verifyFile(file.url, file, result.error)) {
                    return false;
                }
                result.files.push_back(file.url);
                done += sizeOf(file.url);
                continue;
            }

            std::string problem;
            if (fs::exists(finals[i], ec) && verifyFile(finals[i].string(), file, problem)) {
                result.files.push_back(finals[i].string());
                done += sizeOf(finals[i]);
                if (progress && !progress(done, total)) {
                    result.cancelled = true;
                    result.error = "Cancelled";
                    return false;
                }
                continue;
            }
            fs::remove(finals[i], ec);

            fs::path part = finals[i];
            part += ".part";
            if (!fetchFile(file, part.string(), done, total, progress, result)) {
                return false;
            }
            fs::rename(part, finals[i], ec);
            if (ec) {
                result.error = "Could not finish " + displayName(file.url) + ": " + ec.message();
                return false;
            }
            result.files.push_back(finals[i].string());
            done += sizeOf(finals[i]);
        }
        return true;
    }

    bool Downloader::fetchFile(const PackageFile& file, const std::string& part, uint64_t done, uint64_t total,
                               const Progress& progress, Result& result)
    {
        int attempt = 0;
        while (true) {
            std::error_code ec;
            bool exists = fs::exists(part, ec);
            uint64_t partSize = exists ? sizeOf(part) : 0;
            Resume plan = planResume(exists, partSize, file.size);
            if (plan == Resume::Discard) {
                fs::remove(part, ec);
                partSize = 0;
            }
            else if (plan == Resume::Fresh) {
                partSize = 0;
            }

            bool continued = false;
            if (plan != Resume::Complete) {
                Http::Transfer transfer;
                auto report = [&](uint64_t received, uint64_t) {
                    return !progress || progress(done + received, total);
                };
                if (!http_->download(file.url, part, partSize, report, transfer)) {
                    if (transfer.cancelled) {
                        result.cancelled = true;
                        result.error = "Cancelled";
                        return false;
                    }
                    if (transfer.retry && ++attempt < tries) {
                        if (!wait(backoffDelay(attempt), done + sizeOf(part), total, progress)) {
                            result.cancelled = true;
                            result.error = "Cancelled";
                            return false;
                        }
                        continue;
                    }
                    result.error = displayName(file.url) + ": " + transfer.error;
                    return false;
                }
                continued = transfer.resumed;
            }

            std::string problem;
            if (verifyFile(part, file, problem)) {
                return true;
            }
            fs::remove(part, ec);
            // A continued download may have joined two different files;
            // only a download from the start proves the file itself is wrong
            if ((continued || plan == Resume::Complete) && ++attempt < tries) {
                continue;
            }
            result.error = problem;
            return false;
        }
    }

    bool Downloader::wait(int milliseconds, uint64_t received, uint64_t total, const Progress& progress)
    {
        if (sleep) {
            sleep(milliseconds);
            return !progress || progress(received, total);
        }
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (progress && !progress(received, total)) {
                return false;
            }
        }
        return true;
    }

    void Downloader::discard(const std::string& id)
    {
        if (!isValidId(id)) return;
        std::error_code ec;
        fs::remove_all(fs::path(folder_) / id, ec);
    }
}
