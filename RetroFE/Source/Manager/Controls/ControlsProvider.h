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

#include "../EditBuffer.h"
#include "../Provider.h"
#include "../../Control/InputToken.h"
#include <map>
#include <string>
#include <vector>

namespace Manager
{
    // Settings > Controls (prefix "control"): every control RetroFE reads
    // from controls*.conf (ControlList) with its inputs; a control's screen
    // (param:id) adds one by pressing it ("press it now", Result::capture)
    // or puts back RetroFE's default; an input's screen (control-input,
    // param:input, the token) removes it. Changes to every control
    // are kept together until Save (control.save on either screen), which
    // refuses a state where Up, Down, Select, Back or Open Settings has no
    // input that works, and writes through Library::Editor::setControls;
    // RetroFE then re-reads its controls straight away.
    //
    // Values are what RetroFE uses: the file that sets the control last
    // (controls.conf, then controls1..9.conf), else the built-in controls when
    // there's no controls.conf, else the fallbacks UserInput applies.
    class ControlsProvider : public Provider
    {
    public:
        explicit ControlsProvider(const Services& services) : services_(services) {}

        std::string prefix() const override { return "control"; }
        void refresh() override;
        void enter(const Screen& screen) override;
        std::string leaving(const Screen& screen) override;
        void discard(const Screen& screen) override;

        std::string get(const std::string& key, const Screen& screen) override;
        void set(const std::string& key, const std::string& value, const Screen& screen) override;
        Forms::Rows rows(const std::string& key, size_t limit, const Screen& screen) override;
        std::string actionProblem(const std::string& action, const Screen& screen) override;
        Forms::Model::Result runAction(const std::string& action, const Screen& screen) override;

        // The inputs of a value, described for the screen ("No input" when none)
        static std::string describeValue(const std::string& value);
        // The same in short words, for the Controls list where several share a
        // row: "Key Return, Pad A, Joy button 0 (unmapped)"
        static std::string describeValueShort(const std::string& value);
        static std::string describeShort(const InputToken::Input& input);

    private:
        struct Source
        {
            std::string file;   // controls.conf, controls1.conf..., or "" when no file sets it
            std::string note;   // where the value comes from, as a sentence
        };

        void load();
        std::vector<std::string> tokens(const std::string& control) const;
        void setTokens(const std::string& control, const std::vector<std::string>& tokens);
        // Other controls that use input (by label), for the duplicate warning
        std::vector<std::string> usedBy(const std::string& token, const std::string& except) const;
        std::string notes(const std::string& control) const;
        std::string problems() const;
        Forms::Model::Result addCaptured(const std::string& control);

        Services services_;
        EditBuffer edits_;
        std::map<std::string, Source> sources_;
        std::map<std::string, std::string> saved_;   // as loaded, to tell a real change
        bool loaded_ = false;
        std::string captured_;
    };
}
