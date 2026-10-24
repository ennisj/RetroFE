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

#include <vector>

// Which joystick is joy0, joy1...: SDL instance ids in the order they were
// connected, a slot freed when its joystick goes. SDL-free (ids are ints).
// An invalid id (< 0) never has a slot, so a stale device index can't take one.
class JoystickSlots
{
public:
    explicit JoystickSlots(int count = 4) : ids_(static_cast<size_t>(count), -1) {}

    int count() const { return static_cast<int>(ids_.size()); }

    // The slot holding id, or -1
    int slotOf(int id) const
    {
        if (id < 0) {
            return -1;
        }
        for (size_t i = 0; i < ids_.size(); i++) {
            if (ids_[i] == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // id's slot: the one it has, else the first free one; -1 when id is
    // invalid or every slot is taken
    int add(int id)
    {
        if (id < 0) {
            return -1;
        }
        int slot = slotOf(id);
        if (slot >= 0) {
            return slot;
        }
        for (size_t i = 0; i < ids_.size(); i++) {
            if (ids_[i] == -1) {
                ids_[i] = id;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Frees id's slot; returns it, or -1 when id had none
    int remove(int id)
    {
        int slot = slotOf(id);
        if (slot >= 0) {
            ids_[static_cast<size_t>(slot)] = -1;
        }
        return slot;
    }

    void clear() { ids_.assign(ids_.size(), -1); }

private:
    std::vector<int> ids_;
};
