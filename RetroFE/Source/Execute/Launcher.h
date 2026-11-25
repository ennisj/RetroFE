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

#include <string>
#include "../Graphics/Page.h"
#include <atomic>

class Configuration;
class Item;
class RetroFE;

class Launcher
{
public:
    explicit Launcher(Configuration &c, RetroFE &retroFe);
    bool run(std::string collection, Item *collectionItem, Page *currentPage = NULL, bool isAttractMode = false);
    // Why the last run() didn't start the game (a sentence), or "" when it did
    const std::string& failure() const { return failure_; }
    // Whether the last run() started its program at all (it may still have
    // ended with an error): what counts as a play
    bool started() const { return started_; }
    void startScript();
    void exitScript();
	void LEDBlinky( int command, std::string collection = "", Item *collectionItem = NULL);
    void keepRendering(std::atomic<bool>& stop_thread, Page& currentPage) const;

private:
    // What run() works out before execute() is LaunchCommand's (LaunchCommand.h)
    bool simpleExecute(std::string executable, std::string args, std::string currentDirectory, bool wait = true, Page* currentPage = nullptr);
    bool execute(std::string executable, std::string args, std::string currentDirectory, bool wait = true, Page* currentPage = nullptr, bool isAttractMode = false, Item* collectionItem = nullptr);
    Configuration &config_;
    RetroFE &retroFeInstance_;
    std::string failure_;
    bool started_ = false;
};
