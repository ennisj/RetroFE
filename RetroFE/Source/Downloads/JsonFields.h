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

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>

// Reading fields of JSON the Catalog didn't write itself (or that may have been
// edited): a field of the wrong type reads as missing instead of throwing, as
// nlohmann's value() does.
namespace Catalog::JsonFields
{
    inline std::string text(const nlohmann::json& object, const char* name, const std::string& fallback = "")
    {
        if (!object.is_object()) return fallback;
        auto it = object.find(name);
        return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
    }

    inline bool flag(const nlohmann::json& object, const char* name, bool fallback = false)
    {
        if (!object.is_object()) return fallback;
        auto it = object.find(name);
        return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    inline int64_t integer(const nlohmann::json& object, const char* name, int64_t fallback = 0)
    {
        if (!object.is_object()) return fallback;
        auto it = object.find(name);
        if (it == object.end()) return fallback;
        if (it->is_number_unsigned()) {
            uint64_t value = it->get<uint64_t>();
            return value > static_cast<uint64_t>(INT64_MAX) ? fallback : static_cast<int64_t>(value);
        }
        return it->is_number_integer() ? it->get<int64_t>() : fallback;
    }

    // A size or count: a whole number that isn't negative
    inline bool count(const nlohmann::json& object, const char* name, uint64_t& value)
    {
        if (!object.is_object()) return false;
        auto it = object.find(name);
        if (it == object.end()) return false;
        if (it->is_number_unsigned()) {
            value = it->get<uint64_t>();
            return true;
        }
        if (it->is_number_integer() && it->get<int64_t>() >= 0) {
            value = static_cast<uint64_t>(it->get<int64_t>());
            return true;
        }
        return false;
    }

    // The array, or an empty one if the field is missing or isn't one
    inline const nlohmann::json& list(const nlohmann::json& object, const char* name)
    {
        static const nlohmann::json empty = nlohmann::json::array();
        if (!object.is_object()) return empty;
        auto it = object.find(name);
        return it != object.end() && it->is_array() ? *it : empty;
    }
}
