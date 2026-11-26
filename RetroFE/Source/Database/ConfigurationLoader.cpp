#include "ConfigurationLoader.h"
#include "Configuration.h"
#include "GlobalOpts.h"
#include "../Builtin/Builtin.h"
#include "../Collection/CollectionInfoBuilder.h"
#include "../Utility/Log.h"
#include "../Utility/Utils.h"
#include <filesystem>
namespace fs = std::filesystem;
namespace ConfigurationLoader {
bool load(Configuration& config, const std::vector<std::string>& cliSettings,
          const std::function<bool()>& afterSettings, bool startLogging) {
    Configuration* c = &config;
    const bool previousLogging = c->loggingEnabled();
    c->setLoggingEnabled(startLogging);
    struct Restore { Configuration* c; bool value; ~Restore() { c->setLoggingEnabled(value); } } restore{c, previousLogging};
    std::string configPath = Configuration::absolutePath; // Gets working directory
    fs::path launchersPath = Utils::combinePath(Configuration::absolutePath, "launchers." + Utils::getOSType()); // Gets launchers directory
    fs::path collectionsPath = Utils::combinePath(Configuration::absolutePath, "collections"); // Gets collections directory
    std::string settingsConfPath = Utils::combinePath(configPath, "settings"); // Gets root settings

    if (!fs::exists(settingsConfPath + ".conf")) {
        // Nothing installed yet: start with the built-in defaults
        c->importString("", Builtin::settings);
        LOG_NOTICE("RetroFE", "No settings.conf found in \"" + Configuration::absolutePath + "\", using built-in defaults");
    }
    else if (!c->import("", settingsConfPath + ".conf")) {
        LOG_ERROR("RetroFE", "Could not import \"" + settingsConfPath + ".conf\"");
        return false;
    }
    for (int i = 1; i < 16; i++) {
        std::string settingsFile = settingsConfPath + std::to_string(i) + ".conf";
        if (fs::exists(settingsFile)) {
            c->import("", "", settingsFile, false);
        }
    }
    std::string savedSettingsFile = settingsConfPath + "_saved.conf";
    if (fs::exists(savedSettingsFile)) {
        c->import("", "", savedSettingsFile, false);
    }
    if (!cliSettings.empty()) {
        // If cliSettings isn't empty let's do something with it
        std::string result;
        for (const auto& str : cliSettings) {
            result += str;
        }
        c->import("", "CLI", result, false);
    }
    c->applyBaseLayoutPath();

    if (afterSettings && !afterSettings()) return false;
    // Process launchers
    if (fs::is_directory(launchersPath)) {
        for (const auto& entry : fs::directory_iterator(launchersPath)) {
            if (entry.is_regular_file()) {
                fs::path filePath = entry.path();
                if (filePath.extension() == ".conf") {
                    std::string basename = filePath.stem().string();
                    std::string prefix = "launchers." + basename;
                    std::string importFile = filePath.string();

                    if (!c->import(prefix, importFile)) {
                        LOG_ERROR("RetroFE", "Could not import \"" + importFile + "\"");
                    }
                }
            }
        }
    }
    else {
        LOG_NOTICE("RetroFE", "Launchers directory does not exist or is not a directory: " + launchersPath.string());
    }


    // Process collections
    if (!fs::is_directory(collectionsPath)) {
        LOG_NOTICE("RetroFE", "No collections directory found at \"" + collectionsPath.string() + "\", starting with no collections");
    }

    for (const auto& entry : fs::is_directory(collectionsPath) ? fs::directory_iterator(collectionsPath) : fs::directory_iterator()) {
        std::string collection = entry.path().filename().string();
        if (fs::is_directory(entry) && !collection.empty() && collection[0] != '_' && collection != "." && collection != "..") {
            std::string prefix = "collections." + collection;
            bool settingsImported = false;
            std::string settingsFile = Utils::combinePath(collectionsPath, collection, "settings.conf");
            if (fs::exists(settingsFile)) {
                settingsImported |= c->import(collection, prefix, settingsFile, false);
            }

            for (int i = 1; i < 16; i++) {
                std::string numberedSettingsFile = Utils::combinePath(collectionsPath, collection, "settings" + std::to_string(i) + ".conf");
                if (fs::exists(numberedSettingsFile)) {
                    settingsImported |= c->import(collection, prefix, numberedSettingsFile, false);
                }
            }

            std::string infoFile = Utils::combinePath(collectionsPath, collection, "info.conf");
            if (fs::exists(infoFile)) {
                c->import(collection, prefix, infoFile, false);
            }

            // Process collection-specific launcher overrides
            std::string osSpecificLauncherFile = Utils::combinePath(collectionsPath, collection, "launcher." + Utils::getOSType() + ".conf");
            std::string defaultLauncherFile = Utils::combinePath(collectionsPath, collection, "launcher.conf");
            std::string launcherKey = "collectionLaunchers." + collection; // Unique key for collection-specific launchers

            std::string importFile = fs::exists(osSpecificLauncherFile) ? osSpecificLauncherFile
                : fs::exists(defaultLauncherFile) ? defaultLauncherFile
                : "";

            // Import the launcher file if it exists under the unique key
            if (!importFile.empty()) {
                c->import(collection, launcherKey, importFile, false);
                LOG_INFO("RetroFE", "Imported collection-specific launcher for: " + collection);
                if (!c->propertyExists("collections." + collection + ".launcher"))
                {
                    c->setProperty("collections." + collection + ".launcher", collection);
                }
            }

            fs::path localLaunchersPath = Utils::combinePath(collectionsPath, collection, "launchers." + Utils::getOSType() + ".local");
            fs::path defaultLocalLaunchersPath = Utils::combinePath(collectionsPath, collection, "launchers.local");

            // Check if OS-specific launchers directory exists, otherwise use the default launchers directory
            if (!fs::is_directory(localLaunchersPath) && fs::is_directory(defaultLocalLaunchersPath)) {
                localLaunchersPath = defaultLocalLaunchersPath;
            }

            if (fs::is_directory(localLaunchersPath)) {
                for (const auto& launcherEntry : fs::directory_iterator(localLaunchersPath)) {
                    if (launcherEntry.is_regular_file()) {
                        fs::path filePath = launcherEntry.path();
                        if (filePath.extension() == ".conf") {
                            std::string basename = filePath.stem().string();
                            std::string prefix = "localLaunchers." + collection + "." + basename;
                            std::string importFile = filePath.string();

                            if (!c->import(collection, prefix, importFile)) {
                                LOG_ERROR("RetroFE", "Could not import local launcher \"" + importFile + "\" for collection \"" + collection + "\"");
                            }
                            else {
                                LOG_INFO("RetroFE", "Imported local launcher \"" + basename + "\" for collection \"" + collection + "\"");
                            }
                        }
                    }
                }
            }
            // Set the launcher property if it's not already set in settings.conf
            std::string launcherPropertyKey = "collections." + collection + ".launcher";
            if (!c->propertyExists(launcherPropertyKey) && !importFile.empty()) {
                c->setProperty(launcherPropertyKey, collection);
            }

            // Update collectionLaunchers property
            std::string collectionLaunchers = "collectionLaunchers";
            std::string launchers = "";
            c->getProperty(collectionLaunchers, launchers);
            if (!importFile.empty() && (collection.size() < 3 || collection.substr(collection.size() - 3) != "SUB")) {
                c->setProperty(collectionLaunchers, launchers + collection + ",");
            }

            if (!settingsImported) {
                // Menus and merged collections (OneSauce has dozens) need no settings of their own,
                // unless they have games of their own too (list.* keys, a roms folder)
                if (CollectionInfoBuilder::needsNoSettings(*c, collection)) {
                    LOG_INFO("RetroFE", "No settings for " + collection + " (a menu or merged collection); using the defaults");
                }
                else {
                    LOG_ERROR("RetroFE", "Could not import any collection settings for " + collection);
                }
            }
            else {
                LOG_INFO("RetroFE", "Imported settings for collection: " + collection);
            }

            if (!importFile.empty()) {
                LOG_INFO("RetroFE", "Imported launcher configuration for collection: " + collection);
            }
        }
    }

    LOG_INFO("RetroFE", "Imported configuration");
    bool dumpProperties = false;
    c->getProperty(OPTION_DUMPPROPERTIES, dumpProperties);
    if (dumpProperties) c->dumpPropertiesToFile(Utils::combinePath(Configuration::absolutePath, "properties.txt"));
    return true;
}
}
