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

#include <map>
#include <string>

namespace Manager
{
    // The record an edit screen changes, and whether it has unsaved changes.
    // The convention for Save/Reset screens, with prefix "game":
    //
    //   enter():           if (screen.param("id") != edits.id()) edits.load(id, read(id));
    //   get("title"):      edits.get("title");     set("title", v): edits.set("title", v);
    //   get("isDirty"):    edits.isDirty() ? "yes" : "no"   (for visibleWhen / enabledWhen)
    //   actionProblem("save") / ("reset"): edits.isDirty() ? "" : "No changes to save."
    //   runAction("save"): write(edits.changes()); edits.commit();
    //   runAction("reset"): edits.reset();
    //   leaving():         edits.isDirty() ? "You have unsaved changes. Save them, or leave again to discard them." : ""
    //   discard():         edits.reset();
    class EditBuffer
    {
    public:
        // Starts editing a record with its saved values
        void load(const std::string& id, const std::map<std::string, std::string>& values)
        {
            id_ = id;
            saved_ = values;
            values_ = values;
        }

        // The record being edited, or "" before load()
        const std::string& id() const { return id_; }

        std::string get(const std::string& key) const
        {
            auto found = values_.find(key);
            return found != values_.end() ? found->second : "";
        }

        void set(const std::string& key, const std::string& value) { values_[key] = value; }

        bool isDirty() const { return !changes().empty(); }

        // The values that differ from the saved ones
        std::map<std::string, std::string> changes() const
        {
            std::map<std::string, std::string> changed;
            for (const auto& [key, value] : values_) {
                auto saved = saved_.find(key);
                if (saved == saved_.end() ? !value.empty() : saved->second != value) {
                    changed[key] = value;
                }
            }
            return changed;
        }

        // Back to the saved values
        void reset() { values_ = saved_; }

        // The changes were saved
        void commit() { saved_ = values_; }

    private:
        std::string id_;
        std::map<std::string, std::string> saved_;
        std::map<std::string, std::string> values_;
    };
}
