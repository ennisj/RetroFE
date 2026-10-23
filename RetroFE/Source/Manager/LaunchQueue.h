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

#include <functional>
#include <optional>
#include <string>

namespace Manager
{
    // Manage's "Start this game", from the action to RetroFE starting it
    // (ManagerUI keeps one; RetroFE polls it). Kept free of SDL so the rules
    // are tested:
    //
    //   want()      the action asked for a game (Services::requestLaunch)
    //   handle()    its result: on success the manager closes, and the game
    //               waits (pending) for RetroFE; if a screen with unsaved
    //               changes stops the close, asking again for the same game
    //               leaves them (the warning isn't repeated forever)
    //   take()      RetroFE, when idle: the game to start now. While the
    //               manager is open, or a reload is due, it waits; a reload
    //               that can't happen (a Catalog install is running) drops it,
    //               so a game never starts minutes later out of nowhere.
    //   reloading() the reload it waited for is starting: it's kept for the
    //               RetroFE that comes back up
    //   shutdown()  RetroFE is going away: dropped unless reloading()
    class LaunchQueue
    {
    public:
        struct Request
        {
            std::string collection;   // the game's own system
            std::string game;
            bool operator==(const Request& o) const { return collection == o.collection && game == o.game; }
        };

        // Why a launch can't be asked for now, or "": RetroFE reloads before
        // starting the game when changes are waiting, and can't while it installs
        static std::string problem(bool reloadPending, bool catalogBusy)
        {
            return reloadPending && catalogBusy
                ? "Wait for the Catalog to finish installing: RetroFE reloads your changes before starting the game, "
                  "and can't while it installs."
                : "";
        }

        void want(const Request& request) { wanted_ = request; }

        // An action's result. False when it wasn't a launch (handle it as usual).
        // close(discard) closes the manager, leaving unsaved changes when
        // discard is set, and says whether it closed.
        bool handle(bool ok, const std::function<bool(bool discard)>& close)
        {
            if (!wanted_) {
                return false;
            }
            Request wanted = *wanted_;
            wanted_.reset();
            if (!ok) {
                return false;
            }
            bool again = refused_ && *refused_ == wanted;
            if (close(again)) {
                pending_ = wanted;
                refused_.reset();
            }
            else {
                refused_ = wanted;   // it said why; the same request again leaves anyway
            }
            return true;
        }

        // Another screen: a refused request isn't "again" any more
        void screenChanged() { refused_.reset(); }

        enum class Take { None, Start, Dropped };
        Take take(bool managerOpen, bool reloadDue, bool catalogBusy, Request& request)
        {
            if (!pending_ || managerOpen) {
                return Take::None;
            }
            if (reloadDue) {
                if (catalogBusy) {
                    pending_.reset();
                    return Take::Dropped;
                }
                return Take::None;   // the reload happens first; the request outlives it
            }
            request = *pending_;
            pending_.reset();
            reloading_ = false;
            return Take::Start;
        }

        void reloading()
        {
            reloading_ = pending_.has_value();
        }

        void shutdown()
        {
            if (!reloading_) {
                pending_.reset();
            }
            reloading_ = false;
            wanted_.reset();
            refused_.reset();
        }

        bool hasPending() const { return pending_.has_value(); }

    private:
        std::optional<Request> wanted_;
        std::optional<Request> refused_;
        std::optional<Request> pending_;
        bool reloading_ = false;
    };
}
