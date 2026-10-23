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

// A minimal test harness for retrofe_tests: checks count failures, run()
// names a test, and each test gets a fresh folder.

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace Test
{
    inline int failures = 0;
    inline int checks = 0;
    inline std::string current;

    inline void check(bool condition, const std::string& what)
    {
        checks++;
        if (!condition) {
            failures++;
            std::cout << "  FAIL [" << current << "] " << what << std::endl;
        }
    }

    inline void checkEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        check(actual == expected, what + ": expected \"" + expected + "\", got \"" + actual + "\"");
    }

    // A fresh, empty folder per test
    inline std::string makeRoot(const std::string& name)
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() / "retrofe_tests" / name;
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root.string();
    }

    inline void writeFile(const std::string& path, const std::string& content)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content;
    }

    inline std::string readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    inline void run(const std::string& name, const std::function<void()>& test)
    {
        current = name;
        int before = failures;
        test();
        std::cout << (failures == before ? "ok   " : "FAIL ") << name << std::endl;
    }

    // More tests, without editing TestsMain.cpp: a file in Tests/ declares
    //   static Test::Suite games("games", [] { Test::run("...", [] { ... }); });
    // and is built in by CMake's Tests/*.cpp glob. Suites run after the others, by name.
    inline std::vector<std::pair<std::string, std::function<void()>>>& suites()
    {
        static std::vector<std::pair<std::string, std::function<void()>>> all;
        return all;
    }

    struct Suite
    {
        Suite(const std::string& name, std::function<void()> tests) { suites().emplace_back(name, std::move(tests)); }
    };
}

void runFormsTests();
void runCatalogTests();
void runThemeEditorTests();
void runSelfUpdateTests();
