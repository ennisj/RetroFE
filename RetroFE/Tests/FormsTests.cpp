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

// Tests for the forms layer: SettingsFile, form parsing and ManagerModel.
// Build with -DRETROFE_BUILD_TESTS=ON and run retrofe_tests.

#include "../Source/Builtin/Builtin.h"
#include "../Source/Database/Configuration.h"
#include "../Source/Forms/Form.h"
#include "../Source/Forms/SettingsFile.h"
#include "../Source/Manager/ManagerModel.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    int failures = 0;
    int checks = 0;
    std::string currentTest;

    void check(bool condition, const std::string& what)
    {
        checks++;
        if (!condition) {
            failures++;
            std::cout << "  FAIL [" << currentTest << "] " << what << std::endl;
        }
    }

    void checkEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        check(actual == expected, what + ": expected \"" + expected + "\", got \"" + actual + "\"");
    }

    // A fresh, empty folder per test
    std::string makeRoot(const std::string& name)
    {
        fs::path root = fs::temp_directory_path() / "retrofe_tests" / name;
        fs::remove_all(root);
        fs::create_directories(root);
        return root.string();
    }

    void writeFile(const std::string& path, const std::string& content)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content;
    }

    std::string readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    void run(const std::string& name, const std::function<void()>& test)
    {
        currentTest = name;
        int before = failures;
        test();
        std::cout << (failures == before ? "ok   " : "FAIL ") << name << std::endl;
    }
}

int main()
{
    std::string error;

    // --- SettingsFile ---------------------------------------------------------

    run("edits keep comments, spacing and CRLF line endings", [&] {
        std::string root = makeRoot("comments");
        writeFile(root + "/settings.conf",
                  "# Display\r\n"
                  "horizontal = stretch # number of horizontal pixels, or use stretch\r\n"
                  "attractModeTime\t= 180\r\n");
        SettingsFile file(root);
        check(file.set("horizontal", "1600", "", error), "set horizontal");
        check(file.set("attractModeTime", "30", "", error), "set attractModeTime");
        checkEqual(readFile(root + "/settings.conf"),
                   "# Display\r\n"
                   "horizontal = 1600 # number of horizontal pixels, or use stretch\r\n"
                   "attractModeTime\t= 30\r\n",
                   "file content");
    });

    run("LF files stay LF and new keys are appended", [&] {
        std::string root = makeRoot("append");
        writeFile(root + "/settings.conf", "layout = Arcades\n");
        SettingsFile file(root);
        check(file.set("videoEnable", "no", "", error), "set videoEnable");
        checkEqual(readFile(root + "/settings.conf"), "layout = Arcades\nvideoEnable = no\n", "file content");
    });

    run("values are written to the override file they come from", [&] {
        std::string root = makeRoot("override");
        std::string settings = "horizontal = stretch\nvertical = stretch\n";
        writeFile(root + "/settings.conf", settings);
        writeFile(root + "/settings1.conf", "horizontal = 1280\n");
        SettingsFile file(root);
        checkEqual(file.get("horizontal"), "1280", "effective value");
        checkEqual(file.source("horizontal"), "settings1.conf", "source");
        checkEqual(file.source("vertical"), "settings.conf", "source of vertical");
        check(file.set("horizontal", "1600", "", error), "set horizontal");
        checkEqual(readFile(root + "/settings1.conf"), "horizontal = 1600\n", "settings1.conf");
        checkEqual(readFile(root + "/settings.conf"), settings, "settings.conf untouched");
    });

    run("settings_saved.conf wins over numbered files", [&] {
        std::string root = makeRoot("saved");
        writeFile(root + "/settings.conf", "layout = A\n");
        writeFile(root + "/settings3.conf", "layout = B\n");
        writeFile(root + "/settings_saved.conf", "layout = C\n");
        SettingsFile file(root);
        checkEqual(file.get("layout"), "C", "effective value");
        checkEqual(file.source("layout"), "settings_saved.conf", "source");
    });

    run("the last of duplicate lines is the one edited", [&] {
        std::string root = makeRoot("duplicates");
        writeFile(root + "/settings.conf", "rememberMenu = true\nother = 1\nrememberMenu = yes\n");
        SettingsFile file(root);
        checkEqual(file.get("rememberMenu"), "yes", "effective value");
        check(file.set("rememberMenu", "no", "", error), "set");
        checkEqual(readFile(root + "/settings.conf"), "rememberMenu = true\nother = 1\nrememberMenu = no\n", "file content");
    });

    run("a bare install's first save starts from the built-in defaults", [&] {
        std::string root = makeRoot("bare");
        SettingsFile file(root);
        check(!fs::exists(root + "/settings.conf"), "no settings.conf yet");
        check(file.set("horizontal", "1600", Builtin::settings, error), "set horizontal");
        std::string content = readFile(root + "/settings.conf");
        check(content.find("horizontal = 1600") != std::string::npos, "edited in place");
        check(content.find("horizontal = 1280") == std::string::npos, "built-in value replaced, not duplicated");
        check(content.find("homeMenu = yes") != std::string::npos, "built-in defaults kept");
    });

    run("a missing override file isn't created", [&] {
        std::string root = makeRoot("no-overrides");
        writeFile(root + "/settings.conf", "layout = A\n");
        SettingsFile file(root);
        check(file.set("layout", "B", "", error), "set");
        check(!fs::exists(root + "/settings1.conf"), "no settings1.conf");
        check(!fs::exists(root + "/settings.conf.tmp"), "no temp file left");
    });

    // --- Form parsing ---------------------------------------------------------

    run("built-in forms parse", [&] {
        for (const char* id : { "settings", "manage", "catalog" }) {
            auto form = Forms::parse(Forms::builtinForm(id), error);
            check(form != nullptr, std::string(id) + ": " + error);
            if (form) {
                checkEqual(form->id, id, "id");
                check(!form->fields.empty(), std::string(id) + " has fields");
            }
        }
    });

    run("form elements, attributes and entities", [&] {
        auto form = Forms::parse(R"(
            <form id="t" title="Test">
              <section label="S">
                <text label="Args" bind="draft:a" default="&quot;%ITEM_FILEPATH%&quot;"/>
                <bogus/>
                <number label="N" bind="settings:n" min="1" max="9"/>
              </section>
              <note value="Hello"/>
            </form>)", error);
        check(form != nullptr, "parsed: " + error);
        if (!form) return;
        checkEqual(form->title, "Test", "title");
        check(form->fields.size() == 2, "section and note");
        const Forms::Field& section = form->fields[0];
        check(section.type == Forms::FieldType::Section, "section type");
        check(section.children.size() == 2, "unknown element skipped");
        checkEqual(section.children[0].defaultValue, "\"%ITEM_FILEPATH%\"", "entities decoded");
        check(section.children[1].min == 1 && section.children[1].max == 9, "min and max");
        checkEqual(form->fields[1].label, "Hello", "note value");
    });

    run("invalid form XML is reported", [&] {
        check(Forms::parse("<form><text", error) == nullptr, "broken XML");
        check(Forms::parse("<layout/>", error) == nullptr, "not a form");
    });

    // --- ManagerModel ---------------------------------------------------------

    run("model resolves screen keys and keeps edits pending until saved", [&] {
        std::string root = makeRoot("model");
        writeFile(root + "/settings.conf", "horizontal = 1280\nhorizontal0 = 1920\nvideoEnable = true\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);

        checkEqual(model.get("settings:horizontal"), "1920", "horizontal0 wins for the main screen");
        checkEqual(model.actionProblem("settings.save"), "No changes to save.", "nothing to save");

        model.set("settings:horizontal", "1600");
        model.set("settings:videoEnable", "no");
        checkEqual(model.get("settings:horizontal"), "1600", "pending value shown");
        checkEqual(model.actionProblem("settings.save"), "", "can save");
        checkEqual(readFile(root + "/settings.conf"), "horizontal = 1280\nhorizontal0 = 1920\nvideoEnable = true\n", "nothing written yet");

        Forms::Model::Result result = model.runAction("settings.save");
        check(result.ok, "saved: " + result.message);
        checkEqual(readFile(root + "/settings.conf"), "horizontal = 1280\nhorizontal0 = 1600\nvideoEnable = false\n",
                   "written to horizontal0, true/false style kept");
    });

    run("setting a value back to what's saved isn't a change", [&] {
        std::string root = makeRoot("model-unchanged");
        writeFile(root + "/settings.conf", "videoEnable = yes\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.set("settings:videoEnable", "no");
        model.set("settings:videoEnable", "yes");
        checkEqual(model.actionProblem("settings.save"), "No changes to save.", "no pending changes");
    });

    run("form defaults: fallbacks for settings, initial values for drafts", [&] {
        std::string root = makeRoot("model-defaults");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        model.setDefault("settings:attractModeTime", "0");
        model.setDefault("draft:extensions", "zip");
        checkEqual(model.get("settings:attractModeTime"), "0", "settings fallback");
        checkEqual(model.get("draft:extensions"), "zip", "draft initial value");
        model.set("draft:extensions", "7z");
        model.setDefault("draft:extensions", "zip");
        checkEqual(model.get("draft:extensions"), "7z", "default doesn't overwrite input");
    });

    run("model reports where overridden settings come from", [&] {
        std::string root = makeRoot("model-source");
        writeFile(root + "/settings.conf", "layout = A\nhideMouse = no\n");
        writeFile(root + "/settings2.conf", "layout = B\n");
        Configuration::absolutePath = root;
        Configuration config;
        ManagerModel model(config);
        checkEqual(model.get("info:source:settings:layout"), "settings2.conf", "override file named");
        checkEqual(model.get("info:source:settings:hideMouse"), "", "settings.conf isn't mentioned");
    });

    std::cout << std::endl << checks - failures << "/" << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
