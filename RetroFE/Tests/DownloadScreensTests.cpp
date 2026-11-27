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

// The emulator download screens (Manager/Downloads): the forms, the
// provider's background jobs, installing and removing through the forms'
// actions, Stop, and the links from Add a launcher and Add a system. No
// network: the provider is given a fake Http (DownloadFakes.h) and told
// downloads are supported, so this runs on Linux too.

#include "TestSupport.h"
#include "DownloadFakes.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Downloads/Download.h"
#include "../Source/Downloads/Emulators.h"
#include "../Source/Downloads/Hash.h"
#include "../Source/Downloads/Installer.h"
#include "../Source/Downloads/Service.h"
#include "../Source/Forms/Form.h"
#include "../Source/Library/Edits.h"
#include "../Source/Library/Indexer.h"
#include "../Source/Manager/Downloads/DownloadProvider.h"
#include "../Source/Manager/Emulators/EmulatorProvider.h"
#include "../Source/Manager/ManagerModel.h"
#include "../Source/Manager/Systems/SystemProvider.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Test;
using namespace DownloadFakes;

namespace
{
    const std::string windowsOnly = "Emulator downloads are Windows-only for now.";

    std::string releasesUrl(const std::string& repo)
    {
        return "https://api.github.com/repos/" + repo + "/releases?per_page=30";
    }

    std::string assetUrl(const std::string& entryId, const std::string& tag, const std::string& asset)
    {
        return "https://github.com/" + Downloads::findEmulator("emulator-" + entryId)->repo + "/releases/download/" + tag + "/" + asset;
    }

    std::string sha256Of(const std::string& text)
    {
        Downloads::Sha256 hash;
        hash.update(text.data(), text.size());
        return hash.hex();
    }

    // GitHub's release list with one release of one asset
    std::string releaseList(const std::string& tag, const std::string& asset, const std::string& url, const std::string& content)
    {
        return nlohmann::json::array({ { { "tag_name", tag },
            { "assets", nlohmann::json::array({ { { "name", asset }, { "browser_download_url", url },
                                                  { "size", content.size() }, { "digest", "sha256:" + sha256Of(content) } } }) } } }).dump();
    }

    // Every entry with no release, then DuckStation's
    void serveDuckStation(ServingHttp& http, const std::string& content)
    {
        for (const auto& entry : Downloads::emulators()) http.documents[releasesUrl(entry.repo)] = "[]";
        const std::string asset = "duckstation-windows-x64-release.zip";
        const std::string url = assetUrl("duckstation", "v0.1-12083", asset);
        http.files[url] = content;
        http.documents[releasesUrl("stenzek/duckstation")] = releaseList("v0.1-12083", asset, url, content);
    }

    std::string duckZip(const std::string& name)
    {
        std::string zip = makeRoot(name) + "/duck.zip";
        makeZip(zip, { { "duckstation-qt-x64-ReleaseLTCG.exe", "program" } });
        return readFile(zip);
    }

    // A download that goes on until it's stopped (a deadline keeps it from hanging the tests)
    class StallingHttp : public ServingHttp
    {
    public:
        bool download(const std::string& url, const std::string& file, uint64_t offset,
                      const Progress& progress, Transfer& transfer) override
        {
            (void)url; (void)file; (void)offset;
            transfer = Transfer();
            const uint64_t total = 3 * 1024 * 1024;
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (std::chrono::steady_clock::now() < until) {
                if (progress && !progress(total / 2, total)) {
                    transfer.cancelled = true;
                    transfer.error = "Cancelled";
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            transfer.error = "never stopped";
            return false;
        }
    };

    void writeInstalled(const std::string& root, const std::string& id, const std::string& name, const std::string& version,
                        const std::vector<Downloads::InstalledFile>& files = {})
    {
        Downloads::Manifest manifest;
        manifest.files = files;
        manifest.id = id;
        manifest.name = name;
        manifest.kind = id.rfind("core-", 0) == 0 ? "core" : "emulator";
        manifest.version = version;
        std::string error;
        check(Downloads::writeManifest(Downloads::Service::downloadsFolder(root) + "/installed/" + id + ".json", manifest, error),
              "installed " + id + ": " + error);
    }

    // The manager with the Downloads, Launchers and Systems providers over a
    // library of RetroFE's Windows launchers, as the downloads write them
    struct Fixture
    {
        Fixture(const std::string& root, std::unique_ptr<Downloads::Http> http, bool supported = true)
            : indexer(options(root))
            , editor(root)
        {
            Configuration::absolutePath = root;
            config = std::make_unique<Configuration>();
            model = std::make_unique<ManagerModel>(*config);
            Manager::Services services{ *config };
            services.library = &indexer;
            services.editor = &editor;
            Manager::DownloadProvider::Hooks hooks;
            auto owned = std::make_shared<std::unique_ptr<Downloads::Http>>(std::move(http));
            hooks.http = [this, owned] {
                httpMade++;
                return std::move(*owned);
            };
            hooks.supported = supported;
            hooks.prepare = [this](Downloads::Service& service) {
                service.now = [this] { return seconds; };
                service.freeSpace = [](const std::string&, uint64_t& available) {
                    available = 1ull << 40;
                    return true;
                };
            };
            model->addProvider(std::make_unique<Manager::DownloadProvider>(services, hooks));
            model->addProvider(std::make_unique<EmulatorProvider>(services));
            model->addProvider(std::make_unique<Manager::SystemProvider>(services, false));
            indexer.rebuild();
            indexer.wait(10000);
        }

        static Library::ScanOptions options(const std::string& root)
        {
            Library::ScanOptions scan;
            scan.root = root;
            scan.os = "windows";
            scan.metadata = false;
            return scan;
        }

        // Until the job is done, calling update() as the manager does every frame
        bool waitForJob()
        {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (model->get("download:running") == "yes" && std::chrono::steady_clock::now() < until) {
                model->update(0.01f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            model->update(0.01f);
            return model->get("download:running") == "no";
        }

        // Until get(key) starts with prefix, or a deadline
        bool waitFor(const std::string& key, const std::string& prefix)
        {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (model->get(key).rfind(prefix, 0) != 0 && std::chrono::steady_clock::now() < until) {
                model->update(0.01f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return model->get(key).rfind(prefix, 0) == 0;
        }

        const Forms::Row* row(const std::string& key, const std::string& id)
        {
            rows = model->rows(key, 1000);
            for (const Forms::Row& item : rows.items) {
                if (item.id == id) return &item;
            }
            return nullptr;
        }

        std::string detail(const std::string& id)
        {
            const Forms::Row* found = row("download:emulators", id);
            return found ? found->detail : "(no row)";
        }

        // A button's confirmation as the screen shows it
        std::string confirmation(const std::string& form, const std::string& action)
        {
            std::string error;
            auto parsed = Forms::parse(Forms::builtinForm(form), error);
            const Forms::Field* button = parsed ? Forms::findField(*parsed, action) : nullptr;
            return button ? Forms::confirmText(*button, *model) : "(no button)";
        }

        Library::Indexer indexer;
        Library::Editor editor;
        std::unique_ptr<Configuration> config;
        std::unique_ptr<ManagerModel> model;
        Forms::Rows rows;
        int httpMade = 0;
        int64_t seconds = 1000;
    };

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // The fields a screen shows now: visible, and notes and lists with something in them
    void shown(const std::vector<Forms::Field>& fields, Forms::Model& model, std::vector<const Forms::Field*>& result)
    {
        for (const Forms::Field& field : fields) {
            if (!Forms::isVisible(field, model)) continue;
            if (field.type == Forms::FieldType::Section || field.type == Forms::FieldType::Actions) {
                shown(field.children, model, result);
                continue;
            }
            if (field.type == Forms::FieldType::Note && Forms::noteText(field, model).empty()) continue;
            if (field.type == Forms::FieldType::Value && model.get(field.bind).empty()) continue;
            result.push_back(&field);
        }
    }
}

static Test::Suite downloadScreens("download screens", [] {
    const std::vector<std::string> forms = { "download-emulators", "download-emulator", "download-cores", "download-core" };

    run("download screens: every form parses and binds only what the provider serves", [&] {
        const std::set<std::string>& served = Manager::DownloadProvider::keys();
        for (const std::string& id : forms) {
            std::string error;
            const char* xml = Forms::builtinForm(id);
            auto form = xml ? Forms::parse(xml, error) : nullptr;
            check(form != nullptr, id + " parses: " + error);
            if (!form) continue;
            std::vector<std::string> keys = { form->titleBind };
            std::function<void(const std::vector<Forms::Field>&)> walk = [&](const std::vector<Forms::Field>& fields) {
                for (const Forms::Field& field : fields) {
                    for (std::string key : { field.bind, field.visibleWhen, field.enabledWhen, field.status, field.options }) {
                        keys.push_back(!key.empty() && key[0] == '!' ? key.substr(1) : key);
                    }
                    if (!field.action.empty()) keys.push_back(field.action);
                    for (size_t at = field.confirm.find("{bind:"); at != std::string::npos; at = field.confirm.find("{bind:", at + 1)) {
                        keys.push_back(field.confirm.substr(at + 6, field.confirm.find('}', at) - at - 6));
                    }
                    if (!field.open.empty()) {
                        check(Forms::builtinForm(field.open) != nullptr, id + " opens a built-in form: " + field.open);
                    }
                    walk(field.children);
                }
            };
            walk(form->fields);
            for (const std::string& key : keys) {
                if (key.empty()) continue;
                bool ours = key.rfind("download:", 0) == 0 || key.rfind("download.", 0) == 0;
                check(ours, id + " binds only download keys: " + key);
                if (ours) check(served.count(key.substr(9)) == 1, id + ": the provider serves " + key);
            }
        }
    });

    run("download screens: nothing happens until one opens", [] {
        std::string root = makeRoot("download-not-at-start");
        Fixture f(root, std::make_unique<ServingHttp>());
        f.model->setScreen("manage", {});
        f.model->setScreen("emulator-new", {});
        f.model->update(0.01f);
        check(f.httpMade == 0, "no Http made");
        check(!fs::exists(root + "/manage/downloads"), "nothing written");
    });

    run("download screens: where downloads aren't supported, only the Windows note", [&] {
        std::string root = makeRoot("download-unsupported");
        Fixture f(root, std::make_unique<ServingHttp>(), false);
        for (const auto& [id, params] : std::vector<std::pair<std::string, Forms::Params>>{
                 { "download-emulators", {} }, { "download-emulator", { { "id", "emulator-duckstation" } } },
                 { "download-cores", {} }, { "download-core", { { "core", "genesis_plus_gx" } } } }) {
            f.model->setScreen(id, params);
            std::string error;
            auto form = Forms::parse(Forms::builtinForm(id), error);
            if (!form) continue;
            std::vector<const Forms::Field*> fields;
            shown(form->fields, *f.model, fields);
            check(fields.size() == 1 && fields[0]->type == Forms::FieldType::Note && Forms::noteText(*fields[0], *f.model) == windowsOnly,
                  id + ": only the note (" + std::to_string(fields.size()) + " fields shown)");
            checkEqual(f.model->actionProblem("download.install"), windowsOnly, id + ": actions say why not");
        }
        check(f.model->get("download:running") == "no" && f.httpMade == 0, "nothing ran, nothing asked");
    });

    run("download screens: the list shows the six emulators at once, without an Http", [] {
        std::string root = makeRoot("download-list");
        Fixture f(root, nullptr);
        f.model->setScreen("download-emulators", {});
        Forms::Rows rows = f.model->rows("download:emulators", 200);
        std::vector<std::string> names;
        for (const auto& item : rows.items) names.push_back(item.label);
        check(names == std::vector<std::string>({ "RetroArch", "MAME", "DuckStation", "PCSX2", "PPSSPP", "Flycast" }),
              "the built-in six, listed when the screen opens");
        check(std::all_of(rows.items.begin(), rows.items.end(), [](const Forms::Row& r) { return r.detail.rfind("Not installed", 0) == 0; }),
              "none installed");
        check(f.waitForJob(), "the check finishes");
        check(contains(f.model->get("download:checkStatus"), "Couldn't check for releases"),
              "nothing to ask: " + f.model->get("download:checkStatus"));
        checkEqual(f.detail("emulator-duckstation"), "Not installed, couldn't check", "the row says so");
        checkEqual(f.model->get("download:retroArchInstalled"), "no", "no cores link without RetroArch");
    });

    run("download screens: a check shows each emulator's state", [] {
        std::string root = makeRoot("download-check");
        writeInstalled(root, "emulator-retroarch", "RetroArch", "1.22.1");
        writeInstalled(root, "emulator-pcsx2", "PCSX2", "2.8.2");
        auto http = std::make_unique<ServingHttp>();
        ServingHttp* fake = http.get();
        serveDuckStation(*fake, "zip");
        fake->documents[releasesUrl("libretro/RetroArch")] = R"([{"tag_name":"v1.22.2"}])";
        const std::string pcsx2 = "pcsx2-v2.8.2-windows-x64-Qt.7z";
        fake->documents["https://api.github.com/repos/PCSX2/pcsx2/releases/latest"] = nlohmann::json::parse(releaseList("v2.8.2", pcsx2, assetUrl("pcsx2", "v2.8.2", pcsx2), "7z"))[0].dump();
        Fixture f(root, std::move(http));
        f.model->setScreen("download-emulators", {});
        checkEqual(f.model->get("download:checkStatus"), "Checking for releases...", "checking in the background");
        check(f.waitForJob(), "the check finishes");
        checkEqual(f.model->get("download:checkStatus"), "", "nothing wrong");
        checkEqual(f.detail("emulator-retroarch"), "Installed 1.22.1, 1.22.2 available", "an update");
        checkEqual(f.detail("emulator-duckstation"), "Not installed, 0.1.12083 available", "not installed");
        checkEqual(f.detail("emulator-pcsx2"), "Installed 2.8.2", "up to date");
        checkEqual(f.detail("emulator-mame"), "Not installed, no Windows release", "nothing offered");
        checkEqual(f.model->get("download:retroArchInstalled"), "yes", "RetroArch cores link shows");
        size_t asked = fake->requested.size();
        check(asked == 6, "one request per entry: " + std::to_string(asked));

        // Back from an emulator's screen: listed again, not checked again
        f.model->setScreen("download-emulator", { { "id", "emulator-retroarch" } });
        checkEqual(f.model->get("download:name"), "RetroArch", "title");
        checkEqual(f.model->get("download:installed"), "1.22.1", "installed version");
        checkEqual(f.model->get("download:available"), "1.22.2", "available version");
        checkEqual(f.model->get("download:source"), "libretro's server (buildbot.libretro.com)", "where it comes from");
        checkEqual(f.model->get("download:checksumNote"), "libretro doesn't publish a checksum for this download.", "no checksum");
        check(f.model->get("download:canUpdate") == "yes" && f.model->get("download:canInstall") == "no", "Update, not Install");
        check(contains(f.confirmation("download-emulator", "download.update"), "Download RetroArch from libretro's server (buildbot.libretro.com)?"),
              "the update names the source: " + f.confirmation("download-emulator", "download.update"));
        f.model->leaving(f.model->screen());
        f.model->setScreen("download-emulators", {});
        f.model->update(0.01f);
        check(f.model->get("download:running") == "no" && fake->requested.size() == asked, "coming back doesn't check again");

        // Check again asks again (within GitHub's minute it's the copy kept)
        f.seconds += 120;
        check(f.model->runAction("download.check").ok, "Check again");
        check(f.waitForJob() && fake->requested.size() == asked * 2, "asked again");
    });

    run("download screens: offline and GitHub's limit", [] {
        {
            std::string root = makeRoot("download-offline");
            Fixture f(root, std::make_unique<ServingHttp>());   // serves nothing: offline
            f.model->setScreen("download-emulators", {});
            check(f.waitForJob(), "checked");
            std::string status = f.model->get("download:checkStatus");
            check(contains(status, "offline") && f.model->severity("download:checkStatus") == Forms::Severity::Warning, "offline: " + status);
        }
        std::string root = makeRoot("download-limit");
        auto http = std::make_unique<ServingHttp>();
        Downloads::Http::Response limited;
        limited.status = 403;
        limited.headers["x-ratelimit-remaining"] = "0";
        limited.headers["x-ratelimit-reset"] = "5000";
        http->responses[releasesUrl("libretro/RetroArch")] = limited;
        Fixture f(root, std::move(http));
        f.model->setScreen("download-emulators", {});
        check(f.waitForJob(), "checked");
        std::string status = f.model->get("download:checkStatus");
        check(contains(status, "GitHub's limit of 60 checks an hour") && contains(status, "Check again after "), "the limit, and when: " + status);
        checkEqual(f.detail("emulator-flycast"), "Not installed, not checked (GitHub's limit)", "every entry waits");
    });

    run("download screens: installing DuckStation through the form makes its launcher", [] {
        std::string root = makeRoot("download-install");
        auto http = std::make_unique<ServingHttp>();
        const std::string content = duckZip("download-install-server");
        serveDuckStation(*http, content);
        Fixture f(root, std::move(http));
        f.model->setScreen("system-add", {});   // where the user came from
        f.model->setScreen("download-emulators", {});
        check(f.waitForJob(), "checked");
        Forms::Params duck = { { "id", "emulator-duckstation" } };
        f.model->setScreen("download-emulator", duck);
        checkEqual(f.model->get("download:source"), "github.com/stenzek/duckstation", "where it comes from");
        checkEqual(f.model->get("download:size"), Downloads::describeSize(content.size()), "its size");
        checkEqual(f.confirmation("download-emulator", "download.install"),
                   "Download DuckStation from github.com/stenzek/duckstation? RetroFE checks the published checksum, but not what it contains.",
                   "the confirmation names the source");
        check(contains(f.confirmation("download-emulator", "download.remove"), "settings and saves stay in emulators\\DuckStation"),
              "Remove says what stays");
        checkEqual(f.model->actionProblem("download.update"), "It isn't installed.", "nothing to update");
        Forms::Model::Result started = f.model->runAction("download.install");
        check(started.ok, "Install starts: " + started.message);
        check(f.waitForJob(), "installed");
        checkEqual(f.model->get("download:message"),
                   "DuckStation is installed in emulators\\DuckStation, with the launcher DuckStation.", "the message names the launcher");
        check(f.model->severity("download:message") == Forms::Severity::Ok, "as good news");
        check(fs::exists(root + "/launchers.windows/DuckStation.conf"), "launchers.windows/DuckStation.conf");
        check(f.indexer.changesPending(), "the library is marked changed, so RetroFE reloads when Manage closes");
        check(f.model->get("download:isInstalled") == "yes" && f.model->get("download:canInstall") == "no", "installed now");
        checkEqual(f.model->get("download:launcher"), "DuckStation", "its launcher");

        // Back to Add a system: its launchers include DuckStation
        f.model->leaving(f.model->screen());
        f.model->setScreen("download-emulators", {});
        checkEqual(f.detail("emulator-duckstation"), "Installed 0.1.12083", "the list says so");
        f.model->leaving(f.model->screen());
        check(f.indexer.wait(10000), "the library rebuilt");
        f.model->setScreen("system-add", {});
        auto launchers = f.model->options("system:addLaunchers");
        check(std::any_of(launchers.begin(), launchers.end(), [](const auto& option) { return option.first == "DuckStation"; }),
              "Add a system offers DuckStation");
        checkEqual(f.model->get("system:addLauncher"), "DuckStation", "and chooses it, the one launcher added");

        // Remove
        f.model->setScreen("download-emulator", duck);
        check(f.model->runAction("download.remove").ok, "Remove starts");
        check(f.waitForJob(), "removed");
        std::string removed = f.model->get("download:message");
        check(contains(removed, "Removed DuckStation.") && contains(removed, "Removed launcher DuckStation"), "removed with its launcher: " + removed);
        check(!fs::exists(root + "/launchers.windows/DuckStation.conf"), "the launcher went");
        checkEqual(f.model->get("download:isInstalled"), "no", "not installed");
        checkEqual(f.model->actionProblem("download.remove"), "It isn't installed.", "nothing to remove");
    });

    run("download screens: Stop, and leaving the screen, stop a download", [] {
        std::string root = makeRoot("download-stop");
        auto http = std::make_unique<StallingHttp>();
        serveDuckStation(*http, std::string(3 * 1024 * 1024, 'x'));
        Fixture f(root, std::move(http));
        Forms::Params duck = { { "id", "emulator-duckstation" } };
        f.model->setScreen("download-emulator", duck);
        check(f.model->runAction("download.install").ok, "Install starts");
        // The service reports 0 bytes first; the fake then sits at half of 3 MB
        check(f.waitFor("download:progress", "Downloading 1 of 3 MB"), "downloading, in MB: " + f.model->get("download:progress"));
        check(f.model->get("download:runningHere") == "yes" && f.model->get("download:installingHere") == "yes", "Stop and the leaving note show");
        checkEqual(f.model->actionProblem("download.remove"), "Wait for the current download to finish, or stop it.", "one job at a time");
        Forms::Model::Result stopped = f.model->runAction("download.stop");
        check(stopped.ok && f.model->get("download:running") == "no", "Stop ends the job");
        checkEqual(f.model->get("download:message"), "Stopped. Install again to continue where it left off.", "it says so");
        check(f.model->severity("download:message") == Forms::Severity::Warning, "as a warning");
        check(!fs::exists(root + "/launchers.windows/DuckStation.conf") && !fs::exists(root + "/emulators/DuckStation"), "nothing installed");

        check(f.model->runAction("download.install").ok, "Install again");
        check(f.waitFor("download:progress", "Downloading "), "downloading again");
        f.model->setScreen("download-emulators", {});   // another screen opened on top doesn't stop it
        check(f.model->get("download:running") == "yes", "still running under another screen");
        f.model->setScreen("download-emulator", duck);
        checkEqual(f.model->leaving(f.model->screen()), "", "leaving isn't held up");
        checkEqual(f.model->get("download:running"), "no", "leaving the screen stopped it");
    });

    run("download screens: RetroArch's cores", [] {
        std::string root = makeRoot("download-cores");
        writeFile(root + "/emulators/RetroArch/retroarch.exe", "program");
        writeFile(root + "/emulators/RetroArch/info/genesis_plus_gx_libretro.info", "corename = \"Genesis Plus GX\"\nsystemname = \"Sega\"\n");
        Downloads::InstalledFile program;
        program.path = "RetroArch/retroarch.exe";
        program.size = 7;
        program.sha256 = sha256Of("program");
        writeInstalled(root, "emulator-retroarch", "RetroArch", "1.22.2", { program });
        std::string zip = makeRoot("download-cores-server") + "/core.zip";
        check(makeZip(zip, { { "genesis_plus_gx_libretro.dll", "hello" } }), "core zip");
        auto http = std::make_unique<ServingHttp>();
        http->documents[Downloads::coreIndexUrl] = "2026-10-28 3610a686 genesis_plus_gx_libretro.dll.zip\n2026-10-28 00000001 fceumm_libretro.dll.zip\n";
        http->files[Downloads::coreUrl("genesis_plus_gx")] = readFile(zip);
        Fixture f(root, std::move(http));
        f.model->setScreen("download-cores", {});
        check(f.waitForJob(), "the core list was read");
        checkEqual(f.model->get("download:needsRetroArch"), "no", "RetroArch is installed");
        Forms::Rows cores = f.model->rows("download:cores", 1000);
        check(cores.items.size() == 2, "both cores: " + std::to_string(cores.items.size()));
        auto systems = f.model->options("download:coreSystems");
        check(systems.size() >= 2 && systems[0].first == "all"
              && std::any_of(systems.begin(), systems.end(), [](const auto& option) { return option.first == "Sega"; }), "a System choice");
        f.model->set("download:coreSystem", "Sega");
        cores = f.model->rows("download:cores", 1000);
        check(cores.items.size() == 1 && cores.items[0].id == "genesis_plus_gx" && cores.items[0].detail == "Not installed", "filtered by system");

        f.model->setScreen("download-core", { { "core", "genesis_plus_gx" } });
        checkEqual(f.model->get("download:name"), "Genesis Plus GX", "title");
        check(contains(f.confirmation("download-core", "download.install"), "from libretro's server (buildbot.libretro.com)?"), "names the source");
        check(f.model->runAction("download.install").ok, "Install starts");
        check(f.waitForJob(), "installed");
        checkEqual(f.model->get("download:message"), "Genesis Plus GX is installed, with the launcher RetroArch Genesis Plus GX.", "names its launcher");
        check(fs::exists(root + "/launchers.windows/RetroArch Genesis Plus GX.conf"), "core launcher written");
        check(f.indexer.changesPending(), "library changed");
        checkEqual(f.model->get("download:installed"), "2026-10-28", "installed");
        check(f.model->runAction("download.remove").ok && f.waitForJob(), "removed");
        checkEqual(f.model->get("download:message"), "Removed Genesis Plus GX. Removed launcher RetroArch Genesis Plus GX.", "core removal");

        std::string bare = makeRoot("download-cores-none");
        Fixture g(bare, std::make_unique<ServingHttp>());
        g.model->setScreen("download-cores", {});
        check(g.waitForJob(), "nothing to read");
        checkEqual(g.model->get("download:needsRetroArch"), "yes", "Install RetroArch first");
        const Forms::Row* link = g.row("download:retroArchLink", "emulator-retroarch");
        check(link && link->link && g.rows.items.size() == 1, "a link to RetroArch's screen");
        check(g.model->rows("download:cores", 100).items.empty(), "no cores");
    });

    run("download screens: Add a launcher and Add a system open Download an emulator", [] {
        std::string error;
        auto launcher = Forms::parse(Forms::builtinForm("emulator-new"), error);
        check(launcher && !launcher->fields.empty() && launcher->fields[0].type == Forms::FieldType::Link
              && launcher->fields[0].open == "download-emulators" && launcher->fields[0].label == "Download an emulator"
              && contains(launcher->fields[0].help, "makes the launcher"), "Add a launcher's first row");
        auto system = Forms::parse(Forms::builtinForm("system-add"), error);
        const Forms::Field* choice = system ? Forms::findField(*system, "system:addLauncher") : nullptr;
        const Forms::Field* link = system ? Forms::findField(*system, "download-emulators") : nullptr;
        // After the choice and the note under it (why there's no launcher to choose, when there isn't)
        check(choice && link && link->type == Forms::FieldType::Link && link == choice + 2 && (choice + 1)->bind == "system:addLauncherNote",
              "Add a system: right after the Launcher choice");
        auto hub = Forms::parse(Forms::builtinForm("manage"), error);
        const Forms::Field* first = hub ? Forms::findField(*hub, "emulator-new") : nullptr;
        check(first && first->label == "Add your first launcher/emulator", "the bare hub's link opens Add a launcher, one Select from downloads");
    });
});
