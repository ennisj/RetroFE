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

#include "Video/GStreamerVideo.h"
#include "Builtin/Builtin.h"
#include "Database/Configuration.h"
#include "Database/ConfigurationLoader.h"
#include "Database/DB.h"
#include "Database/GlobalOpts.h"
#include "Collection/CollectionInfoBuilder.h"
#include "Execute/Launcher.h"
#include "Graphics/LayoutCompat.h"
#include "Manager/Themes/app/ThemesApp.h"
#include "Utility/Log.h"
#include "Utility/Utils.h"
#include "RetroFE.h"
#include "SDL.h"
#include "versioning.h" // Created at build time

#include <cstdlib>
#include <fstream>
#include <time.h>
#include <locale>
#include <filesystem>
#include <string>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
static bool ImportConfiguration(Configuration* c);
std::vector<std::string> settingsFromCLI;

//
// CLI option functions
//

// Function to initialize the DB object
bool initializeDB(DB*& db, const std::string& dbPath) {
    db = new DB(dbPath);
    if (!db->initialize()) {
        LOG_ERROR("RetroFE", "Could not initialize database");
        return false;
    }
    return true;
}

// Function to initialize the MetadataDatabase object
bool initializeMetadataDatabase(MetadataDatabase*& metadb, DB* db, Configuration& config) {
    metadb = new MetadataDatabase(*db, config);
    if (!metadb->initialize()) {
        LOG_ERROR("RetroFE", "Could not initialize meta database");
        return false;
    }
    return true;
}


static void attachToConsoleIfAvailable() {
#ifdef _WIN32
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        // Attaching to the console failed, indicating there is no console to attach to.
        return;
    }

    // Redirect standard input, output, and error streams to the attached console.
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    // Clear any error states for the C++ standard streams.
    std::cin.clear();
    std::cout.clear();
    std::cerr.clear();

    std::wcin.clear();
    std::wcout.clear();
    std::wcerr.clear();
#endif
}

static void sendEnterKey() {
#ifdef _WIN32
    INPUT ip;
    // Set up a generic keyboard event.
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = 0; // hardware scan code for key
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;

    // Send the "Enter" key
    ip.ki.wVk = 0x0D; // virtual-key code for the "Enter" key
    ip.ki.dwFlags = 0; // 0 for key press
    SendInput(1, &ip, sizeof(INPUT));

    // Release the "Enter" key
    ip.ki.dwFlags = KEYEVENTF_KEYUP; // KEYEVENTF_KEYUP for key release
    SendInput(1, &ip, sizeof(INPUT));
#endif
}

//
// Main starts here
//

int main(int argc, char** argv)
{
    Configuration::initialize();
    Configuration config;
    std::string dbPath = Utils::combinePath(Configuration::absolutePath, "meta.db");

    // Check to see if an argument was passed
    if (argc > 1)
    {
        attachToConsoleIfAvailable();
        
        std::string program = argv[0];
        std::string param = argv[1];
        
        if ((param == "-createcollection" ||
            param == "--createcollection" ||
            param == "-cc") && argc > 2) {
            // Generate a default collection structure
            std::string param = argv[1];
            std::string value = argv[2];
            if (argc == 3) {
                CollectionInfoBuilder::createCollectionDirectory(value, "", Utils::getOSType());
                return 0;
            }
            else if (argc == 4 && std::string(argv[3]) == "local") {
                CollectionInfoBuilder::createCollectionDirectory(value, argv[3], Utils::getOSType());
                return 0;
            }
            else {
                std::cout << std::endl << "Expected at least 1 argument for -createcollection, got " << argc - 2 << std::endl;
                return 0;
            }
        }
        else if (param == "-version" ||
            param == "--version" ||
            param == "-v") {
            std::cout << std::endl << "RetroFE Version " << RETROFE_VERSION_DESCRIBE << "-" << RETROFE_WC_BRANCH << RETROFE_VERSION_PATCH << std::endl << std::flush;
            sendEnterKey();
            return 0;
        }
        else if (param == "-showusage" ||
            param == "--showusage" ||
            param == "-su") {
            // List all available global settings and there use
            std::cout << std::endl;
            showUsage(global_options::s_option_entries);
            std::cout.flush();
            sendEnterKey();
            return 0;
        }
        else if (param == "-rebuilddatabase" ||
            param == "--rebuilddatabase" ||
            param == "-rebuilddb" ||
            param == "-rbdb" ||
            param == "-rdb") {
            // Rebuild the database without doing a full init and bypasses metaLock
            DB* db = nullptr;
            MetadataDatabase* metadb = nullptr;
            if (!initializeDB(db, dbPath)) {
                // Handle initialization error, clean up if necessary
                return 0;
            }
            if (!initializeMetadataDatabase(metadb, db, config)) {
                // Handle initialization error, clean up if necessary
                delete db; // Clean up DB object
                return 0;
            }
            metadb->resetDatabase();
            return 0;
        }
        else if (param == "-gstdotdebug" ||
            param == "--gstdotdebug") {
            Utils::setEnvVar("GST_DEBUG_DUMP_DOT_DIR", Configuration::absolutePath);
            config.debugDotEnabled = true;
            std::string path =
#ifdef WIN32
                Utils::combinePath(Configuration::absolutePath, "retrofe");
#else
                Configuration::absolutePath;
#endif
            Utils::setEnvVar("GST_DEBUG_DUMP_DOT_DIR", path);
        }
        else if (param == "-showconfig" ||
            param == "--showconfig" ||
            param == "-sc") {
            // Prints all settingsX.conf to terminal
            ImportConfiguration(&config);
            fprintf(stdout, "\n");
            config.printProperties();
            fflush(stdout);
            sendEnterKey();
            return 0;
        }
        else if (param == "-dumpproperties" ||
                 param == "--dumpproperties" ||
                 param == "-dump") {
            // Equivalent to doing dumpProperties=true in settings.conf, we just don't do a full init
            if (argc == 2) {
                gst_debug_set_default_threshold(GST_LEVEL_ERROR);
                gst_init(nullptr, nullptr);
                ImportConfiguration(&config);
                config.dumpPropertiesToFile(Utils::combinePath(Configuration::absolutePath, "properties.txt"));
                
                fprintf(stdout, "\nDumping to: %s%cproperties.txt\n", Configuration::absolutePath.c_str(), Utils::pathSeparator);
                sendEnterKey();
                return 0;
            }
            else {
                std::cout << std::endl << "Expected 1 argument for -dump, got " << argc - 2 << std::endl;
                std::cout << "Usage [-dump]" << std::endl;
                sendEnterKey();
                return 0;
            }
        }
        else if ((param == "--theme-check" || param == "-theme-check") && argc > 2) {
            // Checks a theme against this RetroFE and exits: 0 fine, 1 problems, 2 couldn't check
#ifdef WIN32
            Utils::setEnvVar("GST_PLUGIN_PATH", Utils::combinePath(Configuration::absolutePath, "retrofe"));
#endif
            gst_debug_set_default_threshold(GST_LEVEL_ERROR);
            gst_init(nullptr, nullptr);
            if (!ImportConfiguration(&config)) {
                std::cout << std::endl << "The configuration doesn't load" << std::endl;
                return 2;
            }
            // What PageBuilder logs reaches the check whatever the log setting is (Logger::TapScope)
            int status = Themes::runThemeCheck(config, argv[2], argc > 3 ? argv[3] : "");
            Logger::deInitialize();
            return status;
        }
        else if (param == "-createconfig" ||
            param == "--createconfig" ||
            param == "-C") {
            // Generate a default settings.conf and readme
            makeSettings(global_options::s_option_entries);
            makeSettingsReadme(global_options::s_option_entries);
            sendEnterKey();
            return 0;
        }
        else if ((argc % 2 != 0 || argc % 2 == 0) && param != "-help" && param != "-h" && param != "--help") {
            // Pass global settings via CLI
            for (int i = 1; i <= argc - 1 ; i+=2) {
                // The odd argument should always be the key, and even will be value
                if (argv[i+1] == nullptr) {
                    // If you don't pass a value with a key we need catch that here
                    if (param == "-createcollection" ||
                        param == "--createcollection" ||
                        param == "-cc") {
                        std::cout << std::endl << "Usage [-createcollection] [collectionName] {local}" << std::endl;
                        sendEnterKey();
                    }
                    else {
                        std::cout << std::endl << "Expected 1 argument for " << argv[i] << " got " << 0 << std::endl;
                        sendEnterKey();
                    }
                    return 0;
                }
                std::string CLIkey = argv[i];
                std::string CLIvalue = argv[i+1];
                if (Utils::startsWithAndStrip(CLIkey, "-") && ! Utils::startsWith(CLIvalue,"-")) {
                    if (CLIkey == OPTION_LOG) {
                        config.setProperty(OPTION_LOG, CLIvalue);
                        config.StartLogging(&config);
                    }
                    settingsFromCLI.push_back(CLIkey + "=" + CLIvalue + "\n");
                }
                else if(Utils::startsWith(CLIvalue,"-")) {
                    std::cout << std::endl << "Expected 1 argument for -" << CLIkey << " got " << 0 << std::endl;
                    sendEnterKey();
                    return 0;
                }
                else {
                    std::cout << std::endl << "To pass settings via CLI pairs use [-key] [value] format" << std::endl;
                    sendEnterKey();
                    return 0;
                }
            }
        }
        else {
            // Display information about RetroFE
            std::cout << std::endl;
            std::cout << "Absolute Path: " << Configuration::absolutePath << std::endl;
            std::cout << "RetroFE Version: " << RETROFE_VERSION_DESCRIBE << "-" << RETROFE_WC_BRANCH << RETROFE_VERSION_PATCH << std::endl;
            std::cout << std::endl;
            std::cout << "RetroFE is a cross-platform desktop frontend designed for MAME cabinets and game centers, with a focus on simplicity and customization." << std::endl;
            std::cout << "Licensed under GNU General Public License, version 3 or later (GPLv3+)" << std::endl;
            std::cout << std::endl;

            // Display usage information
            std::cout << "Usage:" << std::endl;
            std::cout << "  -h   -help               Show this message" << std::endl;
            std::cout << "  -v   -version            Print the version of RetroFE" << std::endl;
            std::cout << std::endl;
            std::cout << "  -cc  -createcollection   Create a collection directory structure        [collectionName] {local}" << std::endl;
            std::cout << "  -rdb -rebuilddatabase    Rebuild the database from /meta subfolder" << std::endl;
            std::cout << "  -su  -showusage          Print a list of all global settings" << std::endl;
            std::cout << "  -sc  -showconfig         Print a list of current settings" << std::endl;
            std::cout << "  -C   -createconfig       Create a settings.conf with default values and a readme" << std::endl;
            std::cout << "       -dump               Dump current settings to properties.txt" << std::endl;
            std::cout << "       -theme-check        Check a theme against this RetroFE              [layoutName] {reportFile}" << std::endl;
            std::cout << std::endl;
            std::cout << "  -{key} {value}           RetroFE supports passing all settings as command line options for greater flexibility" << std::endl;
            std::cout << "                           i.e. -log DEBUG -vSync true would filter logging to just debug and enable vSync" << std::endl;
            std::cout << std::endl;

            // Provide additional information and references
            std::cout << "For more information, visit" << std::endl;
            std::cout << "https://github.com/CoinOPS-Official/RetroFE/" << std::endl;
            std::cout << "http://retrofe.nl/" << std::endl;
            sendEnterKey();
            return 0;
        }
    }

    // Initialize locale language
    setlocale(LC_ALL, "");

    // Initialize random seed
    srand(static_cast<unsigned int>(time(nullptr)));

#ifdef WIN32
    std::string gstPluginPath = Utils::combinePath(Configuration::absolutePath, "retrofe");
    Utils::setEnvVar("GST_PLUGIN_PATH", gstPluginPath);
    Utils::setEnvVar("FREI0R_PATH", gstPluginPath);
#endif

    gst_debug_set_default_threshold(GST_LEVEL_ERROR);


    gst_init(nullptr, nullptr);

    try {
        while (true) {
            if (!ImportConfiguration(&config)) {
                // Exit with a heads up...
                std::string logFile = Utils::combinePath(Configuration::absolutePath, "log.txt");
                if(Utils::isOutputATerminal()) {
                    fprintf(stderr, "RetroFE has failed to start due to a configuration error\nCheck the log for details: %s\n", logFile.c_str());
                    sendEnterKey();
                }
                else {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Configuration Error", ("RetroFE has failed to start due to a configuration error\nCheck the log for details: \n" + logFile).c_str(), NULL);
                }
                exit(EXIT_FAILURE);
            }
            {
                RetroFE p(config);
                if (!p.run()) // Check if we need to reboot after running
                    break;
                config.clearProperties();
            }
            // A reload shows media added or removed since (Manage's changes, a
            // media folder's files): forget the file listings, once nothing runs
            Utils::clearFileCache();
            LayoutCompat::clearMediaFolderCache();
        }
    }
    catch (std::exception& e) {
        LOG_ERROR("EXCEPTION", e.what());
    }

    Logger::deInitialize();

    return 0;
}

static bool ImportConfiguration(Configuration* c) {
    return ConfigurationLoader::load(*c, settingsFromCLI, []() {
        // log version
        LOG_INFO("RetroFE", "Version: " << RETROFE_VERSION_DESCRIBE << "-" << RETROFE_WC_BRANCH << RETROFE_VERSION_PATCH << " starting");
        LOG_INFO("RetroFE", "Built: " << RETROFE_BUILD_DATE);

        #ifdef WIN32
            LOG_INFO("RetroFE", "OS: Windows");
        #elif __APPLE__
            LOG_INFO("RetroFE", "OS: Mac");
        #else
            LOG_INFO("RetroFE", "OS: Linux");
        #endif
    
        // Check if GStreamer initialization was successful
        if (gst_is_initialized()) {
            LOG_INFO("RetroFE", "GStreamer successfully initialized");
        }
        else {
            LOG_ERROR("RetroFE", "Failed to initialize GStreamer");
            return false;
        }

        LOG_INFO("RetroFE", "Absolute path: " + Configuration::absolutePath);

        return true;
    });
}
