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

#include "Component/Image.h"
#include "FontCache.h"
#include <SDL2/SDL.h>
#if __has_include(<SDL2/SDL_mixer.h>)
#include <SDL2/SDL_mixer.h>
#elif __has_include(<SDL2_mixer/SDL_mixer.h>)
#include <SDL2_mixer/SDL_mixer.h>
#else
#error "Cannot find SDL_mixer header"
#endif
#include <rapidxml.hpp>
#include <vector>

static const int MENU_INDEX_HIGH = 16;

class FormWidget;
class KeyboardWidget;
class ScrollingList;
class Page;
class ViewInfo;
class Configuration;
class FontManager;

class PageBuilder
{
public:
    PageBuilder(const std::string& layoutKey, const std::string& layoutPage, Configuration &c, FontCache *fc, bool isMenu = false);
    virtual ~PageBuilder();
    Page *buildPage( const std::string& collectionName = "", bool defaultToCurrentLayout = false);

    // Build from exactly this file rather than the layout's own files:
    // no "layout - N" variants and no falling back to the layout's main page
    void setLayoutFile(const std::string& file) { fixedFile_ = file; }

    // The same, from XML text; relative asset paths resolve against assetPath
    void setLayoutXml(const char* xml, const std::string& assetPath) { fixedXml_ = xml; fixedAssetPath_ = assetPath; }

    // The collection is a menu of collections: without a page of its own it uses
    // the layout's collections/_menu/layout pages, when there are some
    void setMenuCollection(bool isMenu) { menuCollection_ = isMenu; }

    // The <form> and <keyboard> components created by buildPage
    const std::vector<FormWidget*>& formWidgets() const { return formWidgets_; }
    const std::vector<KeyboardWidget*>& keyboardWidgets() const { return keyboardWidgets_; }

private:
    std::string fixedFile_;
    const char* fixedXml_{ nullptr };
    std::string fixedAssetPath_;
    std::vector<FormWidget*> formWidgets_;
    std::vector<KeyboardWidget*> keyboardWidgets_;
    bool menuCollection_{ false };

    std::string layoutKey;
    std::string layoutPage;
    std::string layoutPath;
    Configuration &config_;
    int screenHeight_{ 0 };
    int screenWidth_{ 0 };
    int layoutHeight_{ 0 };
    int layoutWidth_{ 0 };
    int monitor_{ 0 };
    SDL_Color fontColor_;
    std::string fontName_;
    int fontSize_{ 24 };
    FontCache *fontCache_;
    bool isMenu_;

    FontManager *addFont(const rapidxml::xml_node<> *component, const rapidxml::xml_node<> *defaults, int monitor, const SDL_Color *colorOverride = nullptr);
    void buildForms(rapidxml::xml_node<> *layout, Page *page, int monitor);
    void loadReloadableImages(const rapidxml::xml_node<> *layout, const std::string& tagName, Page *page);
    float getVerticalAlignment(const rapidxml::xml_attribute<> *attribute, float valueIfNull) const;
    float getHorizontalAlignment(const rapidxml::xml_attribute<> *attribute, float valueIfNull) const;
    void buildViewInfo(rapidxml::xml_node<> *componentXml, ViewInfo &info, rapidxml::xml_node<> *defaultXml = nullptr);
    bool buildComponents(rapidxml::xml_node<> *layout, Page *page, const std::string&);
    void loadTweens(Component *c, rapidxml::xml_node<> *componentXml);
    std::shared_ptr<AnimationEvents> createTweenInstance(rapidxml::xml_node<>* componentXml);
    void buildTweenSet(AnimationEvents *tweens, rapidxml::xml_node<> *componentXml, const std::string& tagName, const std::string& tweenName);
    ScrollingList * buildMenu(rapidxml::xml_node<> *menuXml, Page &p, int monitor);
    void buildCustomMenu(ScrollingList *menu, const rapidxml::xml_node<> *menuXml, rapidxml::xml_node<> *itemDefaults);
    void buildVerticalMenu(ScrollingList *menu, const rapidxml::xml_node<> *menuXml, rapidxml::xml_node<> *itemDefaults);
    int parseMenuPosition(const std::string& strIndex);
    rapidxml::xml_attribute<> *findAttribute(const rapidxml::xml_node<> *componentXml, const std::string& attribute, const rapidxml::xml_node<> *defaultXml);
    void getTweenSet(const rapidxml::xml_node<> *node, Animation *animation);
    void getAnimationEvents(const rapidxml::xml_node<> *node, TweenSet &tweens);
    ViewInfo * createMenuItemInfo(rapidxml::xml_node<> *component, rapidxml::xml_node<> *defaults, const ViewInfo& info);
};
