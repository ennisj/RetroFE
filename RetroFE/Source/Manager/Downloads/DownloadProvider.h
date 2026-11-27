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
#pragma once

#include "../Provider.h"
#include "../../Downloads/Service.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Emulator downloads (prefix "download"): the built-in emulators and
// RetroArch's cores, through one Downloads::Service, made the first time a
// download screen opens (never at RetroFE's start).
//
// Screens and parameters:
//   download-emulators  the six emulators with their states; opening it lists
//                       what's installed at once and checks for releases in
//                       the background (the only time versions are checked)
//   download-emulator   one emulator (param id, its package id, e.g.
//                       emulator-duckstation): Install, Update, Remove, Stop
//   download-cores      RetroArch's cores, filtered by system (download:coreSystem)
//   download-core       one core (param core, e.g. genesis_plus_gx)
//
// The service's calls are synchronous, so each runs as a job on a worker
// thread, one at a time; the screens read what it reports, and generation()
// bumps so they redraw. Leaving the screen a job was started on stops it
// (the stop flag, then a join), as does the destructor. A job that wrote or
// removed a launcher marks the library changed (from update(), on the
// manager's thread), so launcher lists refresh and RetroFE reloads when
// Manage closes.
//
// Values, each about the screen's emulator or core where there is one:
//   supported, unsupported, running, runningHere, installingHere, checkStatus, warnings (list),
//   hasWarnings, retroArchInstalled, needsRetroArch, name, description,
//   installed, available, size, source, launcher, checks, checksumNote,
//   progress, message, folder, isInstalled, canInstall, canUpdate,
//   coreSystem (choice, options coreSystems), coresStatus, coreWarnings
//   (list), hasCoreWarnings
// Rows: emulators, cores, retroArchLink
// Actions: check, checkCores, install, update, remove, stop
namespace Manager
{
    class DownloadProvider : public Provider
    {
    public:
        // What tests replace
        struct Hooks
        {
            // The Http the service uses; Downloads::createHttp() when unset
            std::function<std::unique_ptr<Downloads::Http>()> http;
            // Whether downloads work here; unset, the service says (Windows x64 only)
            std::optional<bool> supported;
            // Called with the service once it's made, e.g. to set its clock
            std::function<void(Downloads::Service&)> prepare;
        };

        explicit DownloadProvider(const Services& services, Hooks hooks = {});
        ~DownloadProvider() override;

        // Every key served, values, lists, rows, options and actions alike
        // (tests check the forms' bindings against it)
        static const std::set<std::string>& keys();

        std::string prefix() const override { return "download"; }
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void update(float dt) override;
        unsigned generation() const override { return generation_; }

        std::string get(const std::string& key, const Screen& screen) override;
        Forms::Severity severity(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        std::vector<std::string> list(const std::string& key, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::vector<std::pair<std::string, std::string>> options(const std::string& key, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

    private:
        enum class Job { None, Check, Install, Remove, Cores, InstallCore, RemoveCore };

        struct Message
        {
            std::string text;
            Forms::Severity severity = Forms::Severity::None;
        };

        Downloads::Service& service();
        bool supported();
        bool busy() const { return job_ != Job::None; }
        bool isRunningHere(const Screen& screen) const;
        void start(Job job, const std::string& target, const Screen& screen);
        void stopJob();
        void collect();   // the worker is done: join it, mark the library changed
        void work(Downloads::Service& service, Job job, const std::string& target);
        void finish(const std::string& id, const Downloads::Outcome& outcome);
        void listNow();

        // What the screens show; the caller holds mutex_
        const Downloads::EmulatorStatus* status(const std::string& id) const;
        const Downloads::Release* release(const std::string& id) const;
        const Downloads::Core* core(const std::string& name) const;
        bool retroArchInstalled() const;
        std::string emulatorState(const Downloads::EmulatorStatus& status, Forms::Severity& severity) const;
        std::string checkStatus(Forms::Severity& severity) const;
        std::vector<std::string> warnings() const;
        std::string emulatorValue(const std::string& key, const Screen& screen) const;
        std::string coreValue(const std::string& key, const Screen& screen) const;
        std::string progressText() const;

        Services services_;
        Hooks hooks_;
        std::unique_ptr<Downloads::Service> service_;

        // Written by the worker, read by the screens
        mutable std::mutex mutex_;
        std::vector<Downloads::EmulatorStatus> statuses_;
        std::vector<Downloads::Release> releases_;
        bool checked_ = false;
        Downloads::CoreList cores_;
        bool coresRead_ = false;
        std::map<std::string, Message> messages_;   // by package id
        std::string progress_;
        uint64_t done_ = 0, total_ = 0;
        bool libraryChanged_ = false;
        std::string changedWhat_;
        std::atomic<bool> finished_{ false };
        std::atomic<unsigned> generation_{ 0 };

        // The manager's thread only
        std::thread worker_;
        std::atomic<bool> stop_{ false };
        Job job_ = Job::None;
        std::string jobForm_;
        std::string jobTarget_;
        bool emulatorsOpen_ = false;   // download-emulators is open (so Back to it doesn't check again)
        bool coresOpen_ = false;
        std::string coreSystem_ = "all";
    };
}
