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
#include "TestSupport.h"
#include "../Source/Manager/LaunchNotice.h"

static Test::Suite launchNoticeSuite("launch notice", [] {
    Test::run("notice expires and can be replaced or cleared", [] {
        Manager::LaunchNotice notice;
        Test::check(!notice.visible(), "hidden initially");
        notice.update(10.0f);
        Test::check(!notice.visible() && notice.text().empty(), "updating while hidden does nothing");
        notice.show("first failure");
        Test::check(notice.visible() && notice.text() == "first failure", "shows text");
        notice.update(7.9f);
        Test::check(notice.visible(), "visible after 7.9 seconds");
        notice.update(0.1f);
        Test::check(!notice.visible(), "hidden after 8 seconds");
        notice.show("old failure");
        notice.update(7.9f);
        notice.show("new failure");
        notice.update(7.9f);
        Test::check(notice.visible() && notice.text() == "new failure", "show restarts countdown with new text");
        notice.update(0.1f);
        Test::check(!notice.visible(), "replacement expires");
        notice.show("failure");
        notice.clear();
        Test::check(!notice.visible() && notice.text().empty(), "clear hides notice");
        notice.show("");
        Test::check(!notice.visible(), "empty text stays hidden");
    });
});
