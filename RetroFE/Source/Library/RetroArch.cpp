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
#include "RetroArch.h"
#include "Backups.h"
#include "Files.h"
#include "../Utility/Utils.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace Library
{
namespace RetroArch
{

namespace
{
    // ---- The core table ---------------------------------------------------------------
    // A core's library_name is the folder RetroArch keeps its settings in. It is
    // the name the core itself reports, which is not always its .info file's
    // corename (cap32's info says Caprice32). Stems as the libretro buildbot names
    // the files (genesis_plus_gx_libretro.dll), plus the stems OnesaUCE's cabinet
    // cores use (genesisplusgx), which One Saucier read off those binaries.
    // A wrong row would be taken as certain, so a core whose name isn't sure
    // (gw, fuse, blastem, bsnes, duckstation, kronos...) is left to its .info file.
    const char* const coreTable[][2] = {
        // libretro buildbot (Windows, Linux, macOS)
        { "genesis_plus_gx", "Genesis Plus GX" }, { "genesis_plus_gx_wide", "Genesis Plus GX Wide" },
        { "picodrive", "PicoDrive" },
        { "fceumm", "FCEUmm" }, { "nestopia", "Nestopia" }, { "mesen", "Mesen" }, { "quicknes", "QuickNES" },
        { "snes9x", "Snes9x" }, { "snes9x2010", "Snes9x 2010" }, { "snes9x2005", "Snes9x 2005" },
        { "snes9x2005_plus", "Snes9x 2005 Plus" }, { "snes9x2002", "Snes9x 2002" },
        { "mesen-s", "Mesen-S" },
        { "gambatte", "Gambatte" }, { "sameboy", "SameBoy" }, { "gearboy", "Gearboy" },
        { "mgba", "mGBA" }, { "vba_next", "VBA Next" }, { "vbam", "VBA-M" }, { "gpsp", "gpSP" },
        { "mupen64plus_next", "Mupen64Plus-Next" }, { "parallel_n64", "ParaLLEl N64" },
        { "mednafen_psx", "Beetle PSX" }, { "mednafen_psx_hw", "Beetle PSX HW" }, { "pcsx_rearmed", "PCSX-ReARMed" },
        { "swanstation", "SwanStation" },
        { "mednafen_saturn", "Beetle Saturn" }, { "yabause", "Yabause" },
        { "flycast", "Flycast" },
        { "mednafen_pce_fast", "Beetle PCE Fast" }, { "mednafen_pce", "Beetle PCE" },
        { "mednafen_supergrafx", "Beetle SuperGrafx" }, { "mednafen_ngp", "Beetle NeoPop" },
        { "mednafen_wswan", "Beetle WonderSwan" }, { "mednafen_vb", "Beetle VB" }, { "mednafen_lynx", "Beetle Lynx" },
        { "handy", "Handy" }, { "stella", "Stella" }, { "stella2014", "Stella 2014" }, { "prosystem", "ProSystem" },
        { "a5200", "a5200" }, { "atari800", "Atari800" }, { "virtualjaguar", "Virtual Jaguar" },
        { "fbneo", "FinalBurn Neo" }, { "fbalpha2012", "FB Alpha 2012" },
        { "mame", "MAME" }, { "mame2000", "MAME 2000" }, { "mame2003", "MAME 2003 (0.78)" },
        { "mame2003_plus", "MAME 2003-Plus" }, { "mame2010", "MAME 2010" }, { "mame2015", "MAME 2015" },
        { "mame2016", "MAME 2016" },
        { "dosbox_pure", "DOSBox-pure" }, { "puae", "PUAE" }, { "puae2021", "PUAE 2021" },
        { "vice_x64", "VICE x64" }, { "vice_x64sc", "VICE x64sc" }, { "vice_x128", "VICE x128" },
        { "vice_xpet", "VICE xpet" }, { "vice_xplus4", "VICE xplus4" }, { "vice_xscpu64", "VICE xscpu64" },
        { "vice_xvic", "VICE xvic" },
        { "cap32", "cap32" }, { "bluemsx", "blueMSX" }, { "fmsx", "fMSX" }, { "opera", "Opera" }, { "o2em", "O2EM" },
        { "freeintv", "FreeIntv" }, { "freechaf", "FreeChaF" }, { "gearcoleco", "Gearcoleco" }, { "vecx", "VecX" },
        { "pokemini", "PokeMini" }, { "ppsspp", "PPSSPP" }, { "desmume", "DeSmuME" }, { "melonds", "melonDS" },
        { "dolphin", "dolphin-emu" }, { "citra", "Citra" }, { "neocd", "NeoCD" }, { "hatari", "Hatari" },
        { "px68k", "PX68K" }, { "np2kai", "Neko Project II kai" }, { "81", "EightyOne" },
        { "tic80", "TIC-80" }, { "prboom", "PrBoom" }, { "2048", "2048" },
        { "theodore", "theodore" }, { "quasi88", "QUASI88" }, { "minivmac", "Mini vMac" }, { "xmil", "X millennium" },
        { "fake08", "fake-08" },
        // OnesaUCE's cabinet cores (as One Saucier read them off the binaries)
        { "genesisplusgx", "Genesis Plus GX" }, { "advmame", "AdvanceMAME" }, { "coolcv", "CoolCV" },
        { "fbneo_neogeo", "FinalBurn Neo Light" }, { "flycastvl", "Flycast 2021" },
        { "km_ludicrousn64_xtreme_amped", "LudicrousN64 Xtreme Amped" }, { "km_potator", "Potator" },
        { "mjolnir", "Mjolnir" }, { "px68k_musashi", "PX68K" }, { "supafaust", "Supafaust" },
        { "superbroswar", "Super Bros War" }, { "vb", "Beetle VB" }, { "uae4arm", "uae4arm" },
    };

    // ---- The curated core options -----------------------------------------------------
    // Keys, values and defaults are the cores' own (One Saucier's generated table,
    // read off OnesaUCE's cores, plus a few for desktop cores); labels are ours.
    struct OptionDef
    {
        const char* library;
        const char* key;
        const char* label;
        const char* def;   // the core's own default
        std::vector<std::pair<const char*, const char*>> values;   // value, label
    };

    const std::vector<OptionDef>& optionTable()
    {
        static const std::vector<OptionDef> table = {
            { "Genesis Plus GX", "genesis_plus_gx_aspect_ratio", "Aspect ratio", "auto", { { "auto", "Auto" }, { "NTSC PAR", "NTSC PAR" }, { "PAL PAR", "PAL PAR" }, { "4:3", "4:3" }, { "Uncorrected", "Uncorrected" } } },
            { "Genesis Plus GX", "genesis_plus_gx_no_sprite_limit", "Remove sprite limit (less flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Genesis Plus GX", "genesis_plus_gx_region_detect", "Region", "auto", { { "auto", "Auto" }, { "ntsc-u", "USA" }, { "pal", "Europe" }, { "ntsc-j", "Japan" } } },
            { "Genesis Plus GX", "genesis_plus_gx_overclock", "CPU speed (less slowdown)", "100", { { "100", "100%" }, { "125", "125%" }, { "150", "150%" }, { "175", "175%" }, { "200", "200%" } } },
            { "PicoDrive", "picodrive_region", "Region", "Auto", { { "Auto", "Auto" }, { "Japan NTSC", "Japan NTSC" }, { "Japan PAL", "Japan PAL" }, { "US", "US" }, { "Europe", "Europe" } } },
            { "PicoDrive", "picodrive_input1", "Player 1 controller", "3 button pad", { { "3 button pad", "3 Button Pad" }, { "6 button pad", "6 Button Pad" } } },
            { "PicoDrive", "picodrive_sprlim", "Remove sprite limit (less flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "FCEUmm", "fceumm_region", "Region", "Auto", { { "Auto", "Auto" }, { "NTSC", "NTSC" }, { "PAL", "PAL" }, { "Dendy", "Dendy" } } },
            { "FCEUmm", "fceumm_nospritelimit", "Remove sprite limit (less flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "FCEUmm", "fceumm_overclocking", "Overclock (less slowdown)", "disabled", { { "disabled", "Off" }, { "2x-Postrender", "2x-Postrender" }, { "2x-VBlank", "2x-VBlank" } } },
            { "Nestopia", "nestopia_favored_system", "Region", "auto", { { "auto", "Auto" }, { "ntsc", "NTSC" }, { "pal", "PAL" }, { "famicom", "Famicom" }, { "dendy", "Dendy" } } },
            { "Nestopia", "nestopia_nospritelimit", "Remove sprite limit (less flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Nestopia", "nestopia_overclock", "CPU speed (less slowdown)", "1x", { { "1x", "1x" }, { "2x", "2x" } } },
            { "Snes9x", "snes9x_region", "Region", "auto", { { "auto", "Auto" }, { "ntsc", "NTSC" }, { "pal", "PAL" } } },
            { "Snes9x", "snes9x_aspect", "Aspect ratio", "4:3", { { "4:3", "4:3" }, { "uncorrected", "Uncorrected" }, { "auto", "Auto" }, { "ntsc", "NTSC" }, { "pal", "PAL" } } },
            { "Gambatte", "gambatte_gb_colorization", "Game Boy colors", "disabled", { { "disabled", "Off" }, { "auto", "Auto" }, { "GBC", "GBC" }, { "SGB", "SGB" }, { "internal", "Internal" }, { "custom", "Custom" } } },
            { "gpSP", "gpsp_frameskip", "Frameskip", "disabled", { { "disabled", "Off" }, { "auto", "Auto" } } },
            { "Beetle SuperGrafx", "sgx_nospritelimit", "Remove sprite limit (less flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "PCSX-ReARMed", "pcsx_rearmed_frameskip_type", "Frameskip", "disabled", { { "disabled", "Off" }, { "auto", "Auto" } } },
            { "PCSX-ReARMed", "pcsx_rearmed_neon_enhancement_enable", "Enhanced resolution (slower)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "PCSX-ReARMed", "pcsx_rearmed_multitap", "Multitap (more players)", "disabled", { { "disabled", "Off" }, { "port 1", "Port 1" }, { "port 2", "Port 2" }, { "ports 1 and 2", "Ports 1 and 2" } } },
            { "SwanStation", "swanstation_Display_AspectRatio", "Aspect ratio", "Auto", { { "Auto", "Corrected (NTSC)" }, { "4:3", "4:3" }, { "16:9", "16:9" } } },
            { "SwanStation", "swanstation_GPU_WidescreenHack", "Widescreen hack", "false", { { "false", "Off" }, { "true", "On" } } },
            { "SwanStation", "swanstation_GPU_PGXPEnable", "Geometry correction (PGXP)", "false", { { "false", "Off" }, { "true", "On" } } },
            { "Beetle PSX HW", "beetle_psx_hw_widescreen_hack", "Widescreen hack", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Beetle PSX", "beetle_psx_widescreen_hack", "Widescreen hack", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Mupen64Plus-Next", "mupen64plus-aspect", "Aspect ratio", "4:3", { { "4:3", "4:3" }, { "16:9", "16:9" }, { "16:9 adjusted", "16:9 adjusted" } } },
            { "LudicrousN64 Xtreme Amped", "LudicrousN64-aspect", "Aspect ratio", "4:3", { { "4:3", "4:3" }, { "16:9", "16:9" }, { "16:9 adjusted", "16:9 adjusted" } } },
            { "LudicrousN64 Xtreme Amped", "LudicrousN64-43screensize", "Resolution", "640x480", { { "320x240", "320x240" }, { "640x480", "640x480" } } },
            { "LudicrousN64 Xtreme Amped", "LudicrousN64-pak1", "Player 1 controller pak", "memory", { { "memory", "Memory pak" }, { "rumble", "Rumble pak" }, { "none", "None" } } },
            { "Stella 2014", "stella2014_controller_type_p0", "Player 1 controller", "auto", { { "auto", "Auto" }, { "joystick", "Joystick" }, { "paddles", "Paddles" }, { "driving", "Driving" } } },
            { "Stella", "stella_console", "TV standard", "auto", { { "auto", "Auto" }, { "ntsc", "Ntsc" }, { "pal", "Pal" } } },
            { "FinalBurn Neo", "fbneo-cpu-speed-adjust", "CPU speed", "100%", { { "80%", "80%" }, { "90%", "90%" }, { "100%", "100%" }, { "110%", "110%" }, { "120%", "120%" }, { "130%", "130%" }, { "150%", "150%" } } },
            { "FinalBurn Neo", "fbneo-frameskip-type", "Frameskip", "disabled", { { "disabled", "Off" }, { "Auto", "Auto" } } },
            { "FinalBurn Neo", "fbneo-neogeo-mode", "Neo Geo mode", "DIPSWITCH", { { "DIPSWITCH", "Use bios set in BIOS dipswitch" }, { "MVS_USA", "MVS USA (English - Censored)" }, { "MVS_EUR", "MVS Europe/Asia (English)" }, { "MVS_JAP", "MVS Japan (Japanese)" }, { "AES_EUR", "AES Europe/Asia (English)" }, { "AES_JAP", "AES Japan (Japanese)" } } },
            { "FinalBurn Neo", "fbneo-lightgun-crosshair-emulation", "Light gun crosshair", "hide with lightgun device", { { "hide with lightgun device", "Hide with lightgun device" }, { "always hide", "Always hide" }, { "always show", "Always show" } } },
            { "FinalBurn Neo Light", "fbneo-neogeo-mode", "Neo Geo mode", "DIPSWITCH", { { "DIPSWITCH", "Use bios set in BIOS dipswitch" }, { "MVS_USA", "MVS USA (English - Censored)" }, { "MVS_EUR", "MVS Europe/Asia (English)" }, { "MVS_JAP", "MVS Japan (Japanese)" }, { "AES_EUR", "AES Europe/Asia (English)" }, { "AES_JAP", "AES Japan (Japanese)" } } },
            { "FinalBurn Neo Light", "fbneo-cpu-speed-adjust", "CPU speed", "100%", { { "80%", "80%" }, { "90%", "90%" }, { "100%", "100%" }, { "110%", "110%" }, { "120%", "120%" }, { "130%", "130%" }, { "150%", "150%" } } },
            { "FinalBurn Neo Light", "fbneo-frameskip-type", "Frameskip", "disabled", { { "disabled", "Off" }, { "Auto", "Auto" } } },
            { "MAME 2003-Plus", "mame2003-plus_frameskip", "Frameskip", "disabled", { { "disabled", "Off" }, { "1", "1" }, { "2", "2" }, { "3", "3" } } },
            { "MAME 2003-Plus", "mame2003-plus_four_way_emulation", "4-way joystick (maze games)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "MAME 2003-Plus", "mame2003-plus_crosshair_enabled", "Light gun crosshair", "enabled", { { "enabled", "On" }, { "disabled", "Off" } } },
            { "MAME 2010", "mame_current_frame_skip", "Frameskip", "0", { { "0", "Off" }, { "1", "1" }, { "2", "2" }, { "automatic", "Automatic" } } },
            { "Flycast", "reicast_widescreen_hack", "Widescreen hack", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Flycast 2021", "reicast_widescreen_hack", "Widescreen hack", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Flycast 2021", "reicast_delay_frame_swapping", "Delay frame swapping (fixes flicker)", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "Flycast 2021", "reicast_fog", "Fog effects", "enabled", { { "enabled", "On" }, { "disabled", "Off" } } },
            { "Flycast 2021", "reicast_lightgun1_crosshair", "Light gun crosshair", "disabled", { { "disabled", "Off" }, { "White", "White" }, { "Red", "Red" }, { "Green", "Green" }, { "Blue", "Blue" } } },
            { "DOSBox-pure", "dosbox_pure_cycles", "Emulated speed", "auto", { { "auto", "Auto (DOSBox detects what's needed)" }, { "max", "Max (as fast as possible)" }, { "315", "8086/8088, 4.77 MHz (1980)" }, { "1320", "286, 6 MHz (1982)" }, { "2750", "286, 12.5 MHz (1985)" }, { "4720", "386, 20 MHz (1987)" }, { "7800", "386DX, 33 MHz (1989)" }, { "13400", "486DX, 33 MHz (1990)" }, { "26800", "486DX2, 66 MHz (1992)" }, { "77000", "Pentium, 100 MHz (1995)" }, { "200000", "Pentium II, 300 MHz (1997)" }, { "500000", "Pentium III, 600 MHz (1999)" }, { "1000000", "AMD Athlon, 1.2 GHz (2000)" } } },
            { "cap32", "cap32_retrojoy0", "Player 1 controls", "auto", { { "auto", "Auto" }, { "qaop", "Qaop" }, { "incentive", "Incentive" }, { "joystick_port1", "Joystick_port1" } } },
            { "cap32", "cap32_scr_tube", "Monitor", "color", { { "color", "Color" }, { "green", "Green" }, { "white", "White" } } },
            { "VICE x64", "vice_joyport", "Joystick port", "2", { { "1", "Port 1" }, { "2", "Port 2" } } },
            { "VICE x64", "vice_autoloadwarp", "Fast loading", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "VICE x64", "vice_crop", "Crop border", "disabled", { { "disabled", "Off" }, { "auto", "Automatic" }, { "small", "Small" }, { "medium", "Medium" }, { "maximum", "Maximum" } } },
            { "VICE x64sc", "vice_joyport", "Joystick port", "2", { { "1", "Port 1" }, { "2", "Port 2" } } },
            { "VICE x64sc", "vice_autoloadwarp", "Fast loading", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "VICE x64sc", "vice_crop", "Crop border", "disabled", { { "disabled", "Off" }, { "auto", "Automatic" }, { "small", "Small" }, { "medium", "Medium" }, { "maximum", "Maximum" } } },
            { "VICE xvic", "vice_autoloadwarp", "Fast loading", "disabled", { { "disabled", "Off" }, { "enabled", "On" } } },
            { "VICE xvic", "vice_crop", "Crop border", "disabled", { { "disabled", "Off" }, { "small", "Small" }, { "medium", "Medium" }, { "maximum", "Maximum" } } },
            { "PUAE 2021", "puae_video_standard", "TV standard", "PAL auto", { { "PAL auto", "Automatic PAL" }, { "NTSC auto", "Automatic NTSC" }, { "PAL", "PAL" }, { "NTSC", "NTSC" } } },
            { "PUAE 2021", "puae_crop", "Crop border", "auto", { { "auto", "Automatic" }, { "disabled", "Off" }, { "small", "Small" }, { "medium", "Medium" }, { "large", "Large" } } },
            { "PUAE 2021", "puae_joyport", "Controller acts as", "joystick", { { "joystick", "Joystick" }, { "mouse", "Mouse" } } },
            { "PX68K", "px68k_joytype1", "Player 1 controller", "Default (2 Buttons)", { { "Default (2 Buttons)", "2 buttons" }, { "CPSF-MD (8 Buttons)", "CPSF-MD (8 buttons)" }, { "CPSF-SFC (8 Buttons)", "CPSF-SFC (8 buttons)" } } },
            { "Gearcoleco", "gearcoleco_no_sprite_limit", "Remove sprite limit (less flicker)", "Disabled", { { "Disabled", "Off" }, { "Enabled", "On" } } },
            { "Mjolnir", "mjolnir_frameskip", "Frameskip", "0", { { "0", "Off" }, { "1", "1" }, { "2", "2" } } },
        };
        return table;
    }

    // The prefix a core's option keys share, for taking one core's options out of
    // RetroArch's all-cores options file. A core may have a key without it, which
    // is then left to the core's default; "" for a core the table doesn't cover.
    std::string optionPrefix(const std::string& library)
    {
        static const std::map<std::string, std::string> prefixes = {
            { "Genesis Plus GX", "genesis_plus_gx_" }, { "PicoDrive", "picodrive_" }, { "FCEUmm", "fceumm_" },
            { "Nestopia", "nestopia_" }, { "Snes9x", "snes9x_" }, { "Gambatte", "gambatte_" }, { "gpSP", "gpsp_" },
            { "Beetle SuperGrafx", "sgx_" }, { "PCSX-ReARMed", "pcsx_rearmed_" }, { "SwanStation", "swanstation_" },
            { "Beetle PSX HW", "beetle_psx_hw_" }, { "Beetle PSX", "beetle_psx_" }, { "Mupen64Plus-Next", "mupen64plus-" },
            { "LudicrousN64 Xtreme Amped", "LudicrousN64-" }, { "Stella 2014", "stella2014_" }, { "Stella", "stella_" },
            { "FinalBurn Neo", "fbneo-" }, { "FinalBurn Neo Light", "fbneo-" }, { "MAME 2003-Plus", "mame2003-plus_" },
            { "MAME 2010", "mame_" }, { "Flycast", "reicast_" }, { "Flycast 2021", "reicast_" },
            { "DOSBox-pure", "dosbox_pure_" }, { "cap32", "cap32_" }, { "VICE x64", "vice_" }, { "VICE x64sc", "vice_" },
            { "VICE xvic", "vice_" }, { "PUAE 2021", "puae_" }, { "PX68K", "px68k_" }, { "Gearcoleco", "gearcoleco_" },
            { "Mjolnir", "mjolnir_" },
        };
        auto found = prefixes.find(library);
        return found == prefixes.end() ? "" : found->second;
    }

    // One core's entries of the all-cores options file
    std::map<std::string, std::string> coreEntries(const std::map<std::string, std::string>& all, const std::string& library)
    {
        std::map<std::string, std::string> out;
        std::string prefix = optionPrefix(library);
        if (prefix.empty()) return out;
        for (const auto& [key, value] : all) {
            if (key.compare(0, prefix.size(), prefix) == 0) out[key] = value;
        }
        return out;
    }

    // ---- RetroArch settings and controls ----------------------------------------------
    // Player 1's RetroPad buttons and RetroArch's ids for them in a remap file
    struct PadButton
    {
        const char* name;
        const char* key;
        const char* id;
    };
    const PadButton padButtons[] = {
        { "A", "a", "8" }, { "B", "b", "0" }, { "X", "x", "9" }, { "Y", "y", "1" }, { "L", "l", "10" }, { "R", "r", "11" },
        { "L2", "l2", "12" }, { "R2", "r2", "13" }, { "Select", "select", "2" }, { "Start", "start", "3" },
    };

    // What RetroArch uses when no file names a key (retroarch.cfg normally has them all)
    const std::map<std::string, std::string>& settingsDefaults()
    {
        static const std::map<std::string, std::string> defaults = {
            { "aspect_ratio_index", "22" }, { "video_scale_integer", "false" },
            { "run_ahead_enabled", "false" }, { "run_ahead_frames", "1" }, { "audio_volume", "0.000000" },
            { "savestate_auto_load", "false" }, { "savestate_auto_save", "false" },
        };
        return defaults;
    }

    // A remap file without a key leaves that button as it is
    std::map<std::string, std::string> controlDefaults()
    {
        std::map<std::string, std::string> defaults = { { "input_player1_analog_dpad_mode", "0" } };
        for (const PadButton& b : padButtons) {
            defaults[std::string("input_player1_btn_") + b.key] = b.id;
        }
        return defaults;
    }

    std::map<std::string, std::string> builtinValues(const Target& target, Tile tile)
    {
        if (tile == Tile::Settings) {
            return settingsDefaults();
        }
        if (tile == Tile::Controls) {
            return controlDefaults();
        }
        std::map<std::string, std::string> defaults;
        for (const OptionDef& o : optionTable()) {
            if (target.setup.library == o.library) {
                defaults[o.key] = o.def;
            }
        }
        return defaults;
    }

    // ---- Small helpers ----------------------------------------------------------------
    std::string trim(const std::string& s)
    {
        size_t a = 0;
        size_t b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
        return s.substr(a, b - a);
    }

    std::string unquote(std::string s)
    {
        s = trim(s);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    }

    bool startsWith(const std::string& s, const std::string& prefix)
    {
        return s.compare(0, prefix.size(), prefix) == 0;
    }

    bool endsWith(const std::string& s, const std::string& suffix)
    {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool isFile(const std::string& path)
    {
        std::error_code ec;
        return !path.empty() && fs::is_regular_file(fs::path(path), ec);
    }

    bool isDir(const std::string& path)
    {
        std::error_code ec;
        return !path.empty() && fs::is_directory(fs::path(path), ec);
    }

    bool isAbsolute(const std::string& path)
    {
        return !path.empty() && (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
    }

    std::string join(const std::string& folder, const std::string& name)
    {
        if (folder.empty()) return normalPath(name);
        if (name.empty()) return normalPath(folder);
        return normalPath(narrow(fs::path(folder) / fs::path(name)));
    }

    std::string parentOf(const std::string& path)
    {
        return normalPath(narrow(fs::path(path).parent_path()));
    }

    std::string fileNameOf(const std::string& path)
    {
        std::string p = path;
        while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
        size_t slash = p.find_last_of("/\\");
        return slash == std::string::npos ? p : p.substr(slash + 1);
    }

    std::string stemOf(const std::string& path)
    {
        std::string name = fileNameOf(path);
        size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }

    // The file named by an `#include "file"` line ("" when the line isn't one)
    std::string includeOf(std::string line)
    {
        line = trim(line);
        if (!startsWith(line, "#include")) return "";
        std::string rest = trim(line.substr(8));
        if (rest.size() < 2 || rest.front() != '"') return "";
        size_t close = rest.find('"', 1);
        return close == std::string::npos ? "" : rest.substr(1, close - 1);
    }

    // A file's values as RetroArch reads them, following `#include "file"`
    // (relative to the including file) at the point it appears, at most 16
    // deep (RetroArch's limit), never the same file twice. First one wins.
    void readConfigInto(const std::string& path, int depth, std::set<std::string>& seen,
                        std::map<std::string, std::string>& out, bool* exists)
    {
        std::string text;
        bool ok = !path.empty() && depth < 16 && !seen.count(normalPath(path)) && readFile(path, text);
        if (exists) *exists = ok;
        if (!ok) return;
        seen.insert(normalPath(path));
        for (const Line& line : Lines::parse(text).lines) {
            std::string include = includeOf(line.text);
            if (!include.empty()) {
                readConfigInto(isAbsolute(include) ? normalPath(include) : join(parentOf(path), include), depth + 1, seen, out, nullptr);
                continue;
            }
            for (const auto& [key, value] : parseConfig(line.text)) {
                out.emplace(key, value);   // emplace keeps the first
            }
        }
    }

    std::map<std::string, std::string> readConfig(const std::string& path, bool* exists = nullptr)
    {
        std::map<std::string, std::string> out;
        std::set<std::string> seen;
        readConfigInto(path, 0, seen, out, exists);
        return out;
    }

    // A name used as a file name in RetroArch's folders: one plain path segment
    bool isPlainName(const std::string& name)
    {
        if (name.empty() || name == "." || name == "..") return false;
        if (name.find_first_of("/\\:*?\"<>|") != std::string::npos) return false;
        for (unsigned char c : name) {
            if (c < 32) return false;
        }
        return name.find_first_not_of('.') != std::string::npos;   // not ".", "..", "..."
    }

    // A RetroArch program by its file name: retroarch, retroarch.exe, retroarch32...
    bool isRetroArchProgram(const std::string& executable)
    {
        // retroarch, retroarch.exe, retroarch32, retroarch_debug.exe; not retroarch.cfg
        std::string name = toLower(fileNameOf(unquote(executable)));
        if (endsWith(name, ".exe")) name = name.substr(0, name.size() - 4);
        return startsWith(name, "retroarch") && name.find('.') == std::string::npos;
    }

    bool isCoreFileName(const std::string& token)
    {
        std::string lower = toLower(token);
        return endsWith(lower, "_libretro.dll") || endsWith(lower, "_libretro.so") || endsWith(lower, "_libretro.dylib")
            || endsWith(lower, "_libretro_android.so");
    }

    bool isFlatpakRetroArch(const LauncherDef& launcher, const std::vector<std::string>& args)
    {
        std::string name = toLower(fileNameOf(unquote(launcher.executable)));
        if (name != "flatpak") return false;
        for (const std::string& a : args) {
            if (a == "org.libretro.RetroArch") return true;
        }
        return false;
    }

    // The core a launcher loads: -L <core>, --libretro <core>, --libretro=<core>,
    // -L<core>, or a *_libretro file handed over by another program
    std::string coreArgumentOf(const std::vector<std::string>& args)
    {
        for (size_t i = 0; i < args.size(); i++) {
            const std::string& a = args[i];
            if ((a == "-L" || a == "--libretro") && i + 1 < args.size()) return args[i + 1];
            if (startsWith(a, "--libretro=")) return a.substr(11);
            if (startsWith(a, "-L") && a.size() > 2 && a[2] != '-') return a.substr(2);
        }
        for (const std::string& a : args) {
            if (isCoreFileName(a)) return a;
        }
        return "";
    }

    std::string configArgumentOf(const std::vector<std::string>& args)
    {
        for (size_t i = 0; i < args.size(); i++) {
            const std::string& a = args[i];
            if ((a == "-c" || a == "--config") && i + 1 < args.size()) return args[i + 1];
            if (startsWith(a, "--config=")) return a.substr(9);
        }
        return "";
    }

    // A RetroArch program handed to another one (cmd /c start "" "...\retroarch.exe" -L ...)
    std::string programArgumentOf(const std::vector<std::string>& args)
    {
        for (const std::string& a : args) {
            if (isRetroArchProgram(a)) return a;
        }
        return "";
    }

    // The value after -L looks like a core: a *_libretro file, or a core the table knows
    bool looksLikeCore(const std::string& value)
    {
        return toLower(value).find("_libretro") != std::string::npos || !libraryForStem(coreStem(value)).empty();
    }

    // A folder RetroArch made: it holds a file Manage didn't create (Manage's
    // own new files have an ".absent" original in the backups)
    bool madeByRetroArch(const Backups& backups, const std::string& folder)
    {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(folder), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) continue;
            std::string file;
            if (!narrowName(it->path(), file)) continue;
            if (!isFile(backups.backupPath(file) + ".absent")) return true;
        }
        return false;
    }

    bool isTrue(const std::map<std::string, std::string>& cfg, const std::string& key, bool fallback)
    {
        auto found = cfg.find(key);
        if (found == cfg.end()) return fallback;
        std::string v = toLower(found->second);
        if (v == "true") return true;
        if (v == "false") return false;
        return fallback;
    }

    // Whether a file differs from the original kept before Manage first changed it
    bool differsFromOriginal(const Backups& backups, const std::string& path)
    {
        if (!backups.hasOriginal(path)) return false;
        std::string backup = backups.backupPath(path);
        if (isFile(backup)) {
            std::string a;
            std::string b;
            if (!readFile(path, a)) return true;
            return !readFile(backup, b) || a != b;
        }
        return isFile(path);   // it didn't exist before
    }

    std::vector<std::string> keysOf(const RowDef& def)
    {
        std::vector<std::string> keys;
        for (const Choice& c : def.choices) {
            for (const auto& [key, value] : c.assign) {
                (void)value;
                if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
            }
        }
        return keys;
    }

    std::string valueIn(const std::map<std::string, std::string>& map, const std::string& key)
    {
        auto found = map.find(key);
        return found == map.end() ? "" : found->second;
    }

    // What each tile's file is called in messages
    std::string tileNoun(Tile tile)
    {
        switch (tile) {
        case Tile::Settings: return "RetroArch settings";
        case Tile::CoreOptions: return "core options";
        case Tile::Controls: return "controls";
        }
        return "";
    }

    // The values a game's or core's files give, layer by layer
    struct Layers
    {
        std::map<std::string, std::string> own;
        bool ownExists = false;
        std::string ownText;
        std::map<std::string, std::string> base;    // Settings: what stacks under own, merged
        std::map<std::string, std::string> below;   // one-file tiles: the file that applies without own
        std::map<std::string, std::string> builtin;
        bool oneFile = false;
    };

    Layers readLayers(const Target& target, Tile tile, const TileFiles& f)
    {
        Layers l;
        l.ownExists = !f.own.empty() && readFile(f.own, l.ownText);
        if (l.ownExists) l.own = readConfig(f.own);
        // One file wins for a game's options and controls, and for a core's
        // options while RetroArch still takes them from the all-cores file
        l.oneFile = tile != Tile::Settings && (target.game() || !f.below.empty());
        if (tile == Tile::Settings) {
            for (const std::string& layer : f.layers) {
                for (const auto& [key, value] : readConfig(layer)) {
                    l.base[key] = value;   // a later layer overrides
                }
            }
        }
        else if (!f.below.empty()) {
            l.below = readConfig(f.below);
            if (f.belowShared) l.below = coreEntries(l.below, target.setup.library);
        }
        l.builtin = builtinValues(target, tile);
        return l;
    }

    // A key's value with or without the tile's own file
    std::string valueOf(const Layers& l, Tile tile, const std::string& key, bool withOwn)
    {
        if (withOwn) {
            auto found = l.own.find(key);
            if (found != l.own.end()) return found->second;
            // One file wins: a key it lacks is the core's own default, not the file under it
            if (l.oneFile && l.ownExists) return valueIn(l.builtin, key);
        }
        const auto& under = tile == Tile::Settings ? l.base : l.below;
        auto found = under.find(key);
        if (found != under.end()) return found->second;
        return valueIn(l.builtin, key);
    }

    bool matches(const Layers& l, Tile tile, const Choice& choice, bool withOwn)
    {
        for (const auto& [key, value] : choice.assign) {
            if (!sameValue(valueOf(l, tile, key, withOwn), value)) return false;
        }
        return !choice.assign.empty();
    }
}

// ---- Public: parsing ---------------------------------------------------------------------

Environment Environment::current()
{
    Environment env;
    env.os = Utils::getOSType();
    auto get = [](const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    };
    env.appData = get("APPDATA");
    env.home = env.os == "windows" ? get("USERPROFILE") : get("HOME");
    if (env.home.empty()) env.home = get("HOME");
    env.xdgConfig = get("XDG_CONFIG_HOME");
    return env;
}

std::vector<std::string> splitArguments(const std::string& arguments)
{
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    bool any = false;
    for (char c : arguments) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        }
        else if (!quoted && (c == ' ' || c == '\t')) {
            if (any) out.push_back(current);
            current.clear();
            any = false;
        }
        else {
            current += c;
            any = true;
        }
    }
    if (any) out.push_back(current);
    return out;
}

bool runsRetroArch(const LauncherDef& launcher)
{
    if (isRetroArchProgram(launcher.executable)) return true;
    std::vector<std::string> args = splitArguments(launcher.arguments);
    if (isFlatpakRetroArch(launcher, args)) return true;
    // Another program handed RetroArch itself (cmd /c start "" "...\retroarch.exe" -L ...)
    if (!programArgumentOf(args).empty()) return true;
    // -L means "load a core" only to RetroArch: elsewhere the value must look like one
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        if ((a == "-L" || a == "--libretro") && i + 1 < args.size() && looksLikeCore(args[i + 1])) return true;
        if (startsWith(a, "--libretro=") && looksLikeCore(a.substr(11))) return true;
        if (startsWith(a, "-L") && a.size() > 2 && a[2] != '-' && looksLikeCore(a.substr(2))) return true;
        if (isCoreFileName(a)) return true;
    }
    return false;
}

std::string coreStem(const std::string& coreArgument)
{
    std::string name = fileNameOf(unquote(coreArgument));
    std::string lower = toLower(name);
    for (const char* ext : { ".dll", ".so", ".dylib" }) {
        if (endsWith(lower, ext)) {
            name = name.substr(0, name.size() - std::string(ext).size());
            lower = toLower(name);
            break;
        }
    }
    for (const char* suffix : { "_libretro_android", "_libretro" }) {
        if (endsWith(lower, suffix)) {
            name = name.substr(0, name.size() - std::string(suffix).size());
            break;
        }
    }
    return name;
}

std::string libraryForStem(const std::string& stem)
{
    for (const auto& entry : coreTable) {
        if (stem == entry[0]) return entry[1];
    }
    return "";
}

std::map<std::string, std::string> parseConfig(const std::string& text)
{
    std::map<std::string, std::string> out;
    Lines lines = Lines::parse(text);
    bool first = true;
    for (const Line& raw : lines.lines) {
        std::string line = raw.text;
        if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
        first = false;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (!value.empty() && value[0] == '"') {
            size_t close = value.find('"', 1);
            value = close == std::string::npos ? value.substr(1) : value.substr(1, close - 1);
        }
        else {
            size_t end = value.find_first_of(" \t#");
            if (end != std::string::npos) value = value.substr(0, end);
        }
        if (!key.empty() && !out.count(key)) out[key] = value;   // the first one wins, as in RetroArch
    }
    return out;
}

std::map<std::string, std::string> readConfigFile(const std::string& path)
{
    return readConfig(path);
}

std::string editConfig(const std::string& text, const std::map<std::string, std::optional<std::string>>& edits)
{
    Lines lines = Lines::parse(text);
    lines.fallbackNewline = "\n";   // RetroArch writes its files with \n everywhere
    std::set<std::string> done;
    for (size_t i = 0; i < lines.lines.size();) {
        std::string line = trim(lines.lines[i].text);
        if (i == 0 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line = line.substr(3);
        size_t eq = line.find('=');
        std::string key = line.empty() || line[0] == '#' || eq == std::string::npos ? "" : trim(line.substr(0, eq));
        auto edit = key.empty() ? edits.end() : edits.find(key);
        if (edit == edits.end()) {
            i++;
            continue;
        }
        if (!edit->second) {
            lines.erase(i);   // every line for a removed key goes
            continue;
        }
        if (!done.count(key)) {
            lines.lines[i].text = key + " = \"" + *edit->second + "\"";
            done.insert(key);
        }
        i++;
    }
    for (const auto& [key, value] : edits) {
        if (value && !done.count(key)) lines.append(key + " = \"" + *value + "\"");
    }
    return lines.join();
}

bool sameValue(const std::string& a, const std::string& b)
{
    if (a == b) return true;
    if (toLower(a) == toLower(b)) return true;
    if (a.empty() || b.empty()) return false;
    char* endA = nullptr;
    char* endB = nullptr;
    double x = std::strtod(a.c_str(), &endA);
    double y = std::strtod(b.c_str(), &endB);
    return endA && *endA == '\0' && endB && *endB == '\0' && x == y;
}

// ---- Public: detection ---------------------------------------------------------------------

Setup detect(const std::string& root, const LauncherDef& launcher, const Environment& env)
{
    Setup s;
    s.launcher = launcher.id;
    std::string program = unquote(launcher.executable);
    if (!runsRetroArch(launcher)) {
        s.state = State::NotRetroArch;
        s.why = "It runs outside RetroArch: its launcher " + launcher.id + " starts "
            + (program.empty() ? std::string("no program") : fileNameOf(program)) + ".";
        return s;
    }
    std::vector<std::string> args = splitArguments(launcher.arguments);
    auto expand = [&](std::string value) { return Utils::replace(value, "%RETROFE_PATH%", root); };

    // RetroArch's program: relative paths are relative to RetroFE's folder (or,
    // handed to another program, its working folder), a bare name may be on the path
    auto resolveProgram = [&](const std::string& written, const std::string& from) {
        std::string value = expand(written);
        if (isAbsolute(value)) return normalPath(value);
        std::string path = join(from, value);
        if (!isFile(path) && from != root) path = join(root, value);
        if (!isFile(path) && value.find_first_of("/\\") == std::string::npos) {
            std::string found = findProgram(value);
            if (!found.empty()) path = normalPath(found);
        }
        return path;
    };
    std::string configuredWorking = expand(trim(launcher.currentDirectory));
    if (!configuredWorking.empty() && !isAbsolute(configuredWorking)) configuredWorking = join(root, configuredWorking);
    if (isRetroArchProgram(program)) {
        std::string programPath = resolveProgram(program, root);
        if (!isFile(programPath)) {
            s.state = State::NotFound;
            s.why = "RetroArch wasn't found: its launcher " + launcher.id + " starts " + programPath + ", which isn't there.";
            return s;
        }
        s.program = programPath;
    }

    // Where RetroFE starts it: its working folder, else the folder of the
    // program as written (RetroFE's own folder for a bare name)
    std::string working = configuredWorking;
    if (working.empty()) {
        bool hasFolder = expand(program).find_first_of("/\\") != std::string::npos;
        working = !hasFolder ? root : !s.program.empty() ? parentOf(s.program) : parentOf(resolveProgram(program, root));
    }
    s.workingFolder = normalPath(working);

    // RetroArch started through another program: use it when it's there (its
    // retroarch.cfg is then beside it), else look in the platform's places
    if (s.program.empty()) {
        std::string token = programArgumentOf(args);
        if (!token.empty()) {
            std::string path = resolveProgram(token, s.workingFolder);
            if (isFile(path)) s.program = path;
        }
    }

    // retroarch.cfg: --config, else beside the program, else the platform's place
    std::vector<std::string> candidates;
    std::string configArg = expand(configArgumentOf(args));
    if (!configArg.empty()) {
        candidates.push_back(isAbsolute(configArg) ? normalPath(configArg) : join(s.workingFolder, configArg));
    }
    else {
        if (!s.program.empty()) candidates.push_back(join(parentOf(s.program), "retroarch.cfg"));
        if (isFlatpakRetroArch(launcher, args) && !env.home.empty()) {
            candidates.push_back(join(env.home, ".var/app/org.libretro.RetroArch/config/retroarch/retroarch.cfg"));
        }
        if (env.os == "windows") {
            if (!env.appData.empty()) {
                candidates.push_back(join(env.appData, "RetroArch/retroarch.cfg"));
                candidates.push_back(join(env.appData, "retroarch.cfg"));
            }
        }
        else if (env.os == "apple") {
            if (!env.home.empty()) candidates.push_back(join(env.home, "Library/Application Support/RetroArch/retroarch.cfg"));
        }
        else {
            if (!env.xdgConfig.empty()) candidates.push_back(join(env.xdgConfig, "retroarch/retroarch.cfg"));
            if (!env.home.empty()) {
                candidates.push_back(join(env.home, ".config/retroarch/retroarch.cfg"));
                candidates.push_back(join(env.home, ".retroarch.cfg"));
            }
        }
    }
    for (const std::string& c : candidates) {
        if (isFile(c)) {
            s.configFile = c;
            break;
        }
    }
    if (s.configFile.empty()) {
        s.state = State::NotFound;
        std::string looked;
        for (const std::string& c : candidates) looked += (looked.empty() ? "" : ", ") + c;
        s.why = "RetroArch's settings file (retroarch.cfg) wasn't found"
            + (looked.empty() ? std::string(".") : " (looked for " + looked + ").")
            + " Start RetroArch once so it writes one.";
        return s;
    }

    // The folders RetroArch keeps per-core files in, as retroarch.cfg sets them:
    // ":" is RetroArch's own folder, "~" the home folder, "default" (or nothing) its default
    std::map<std::string, std::string> cfg = readConfig(s.configFile);
    std::string configFolder = parentOf(s.configFile);
    std::string programFolder = s.program.empty() ? configFolder : parentOf(s.program);
    std::string base = env.os == "windows" ? programFolder : configFolder;
    auto rest = [](const std::string& v) {
        size_t start = v.find_first_not_of("/\\", 1);
        return start == std::string::npos ? std::string() : v.substr(start);
    };
    auto folder = [&](const std::string& key, const std::string& fallback) {
        std::string v = trim(valueIn(cfg, key));
        if (v.empty() || v == "default") return normalPath(fallback);
        if (v[0] == ':') return join(programFolder, rest(v));
        if (v[0] == '~') return join(env.home, rest(v));
        return isAbsolute(v) ? normalPath(v) : join(s.workingFolder, v);
    };
    s.configDir = folder("rgui_config_directory", join(base, "config"));
    s.remapDir = folder("input_remapping_directory", join(base, "config/remaps"));
    std::string coresDir = folder("libretro_directory", join(base, "cores"));
    std::string infoDir = folder("libretro_info_path", env.os == "windows" ? join(programFolder, "info") : join(configFolder, "cores"));
    s.globalOptionsFile = folder("core_options_path", join(configFolder, "retroarch-core-options.cfg"));
    s.globalCoreOptions = isTrue(cfg, "global_core_options", false);
    s.overrides = isTrue(cfg, "auto_overrides_enable", true);
    s.gameOptions = isTrue(cfg, "game_specific_options", true);
    s.remaps = isTrue(cfg, "auto_remaps_enable", true);

    // The core
    s.coreArgument = coreArgumentOf(args);
    if (s.coreArgument.empty()) {
        s.state = State::CoreUnknown;
        s.why = "Its launcher " + launcher.id + " doesn't name a core (-L), so RetroArch starts with whichever core it used last.";
        return s;
    }
    std::string core = expand(s.coreArgument);
    if (core.find('%') == std::string::npos) {
        std::vector<std::string> places;
        if (isAbsolute(core)) {
            places.push_back(normalPath(core));
        }
        else {
            places.push_back(join(s.workingFolder, core));
            if (!s.program.empty()) places.push_back(join(parentOf(s.program), core));
            places.push_back(join(root, core));
            places.push_back(join(coresDir, core));
            if (core.find_first_of("/\\") == std::string::npos && core.find('.') == std::string::npos) {
                // RetroArch also takes a bare core name and looks in its cores folder
                const char* ext = env.os == "windows" ? ".dll" : env.os == "apple" ? ".dylib" : ".so";
                places.push_back(join(coresDir, core + "_libretro" + ext));
                places.push_back(join(coresDir, core + ext));
            }
        }
        for (const std::string& p : places) {
            if (isFile(p)) {
                s.coreFile = p;
                break;
            }
        }
        if (s.coreFile.empty()) {
            s.notes.push_back("The core file (" + s.coreArgument + ") wasn't found, so its games won't start until it's there.");
        }
    }
    else {
        s.notes.push_back("The core depends on the game (" + s.coreArgument + "), so it isn't checked.");
    }
    s.coreStem = coreStem(core);

    s.library = libraryForStem(s.coreStem);
    if (!s.library.empty()) {
        s.libraryCertain = true;
        s.librarySource = "Manage's list of cores";
    }
    else {
        std::vector<std::string> infos;
        std::string infoName = s.coreStem + "_libretro.info";
        if (!s.coreFile.empty()) infos.push_back(join(parentOf(s.coreFile), infoName));
        infos.push_back(join(infoDir, infoName));
        infos.push_back(join(coresDir, infoName));
        for (const std::string& info : infos) {
            bool exists = false;
            std::map<std::string, std::string> fields = readConfig(info, &exists);
            std::string corename = trim(valueIn(fields, "corename"));
            if (exists && isPlainName(corename)) {
                s.library = corename;
                // RetroArch's own folder of that name confirms it, not one Manage's writes made
                Backups backups(root);
                s.libraryCertain = madeByRetroArch(backups, join(s.configDir, corename))
                    || madeByRetroArch(backups, join(s.remapDir, corename));
                s.librarySource = s.libraryCertain
                    ? "its info file, confirmed by RetroArch's own settings folder of that name"
                    : "its info file only";
                if (!s.libraryCertain) {
                    s.notes.push_back("The settings folder name (" + corename + ") comes from the core's info file and isn't confirmed yet: "
                                      "RetroArch makes that folder the first time it runs the core. If these settings don't take effect, "
                                      "the core uses another name.");
                }
                break;
            }
        }
    }
    if (s.library.empty()) {
        s.state = State::CoreUnknown;
        s.why = "Its core (" + s.coreStem + ") isn't known: Manage's list of cores doesn't have it and there's no "
            + s.coreStem + "_libretro.info, so the folder RetroArch keeps its settings in can't be worked out.";
        return s;
    }

    std::string args0 = launcher.arguments;
    s.passesName = args0.find("%ITEM_NAME%") != std::string::npos && args0.find("%ITEM_FILEPATH%") == std::string::npos
        && args0.find("%ITEM_FILENAME%") == std::string::npos;
    s.state = State::Ready;
    return s;
}

Target gameTarget(const Setup& setup, const GameRecord& game)
{
    Target t;
    t.setup = setup;
    t.rom = setup.passesName || game.file.empty() ? game.name : stemOf(game.file);
    if (t.rom.empty()) t.rom = game.name;
    t.contentFolder = fileNameOf(game.directory);
    if (!isPlainName(t.contentFolder)) t.contentFolder.clear();
    // The name becomes a file name in RetroArch's folders: one plain segment,
    // and never the core's own name (its files would be the core-wide ones)
    if (!isPlainName(t.rom)) {
        t.problem = "This game's name (" + t.rom + ") can't be a file name in RetroArch's folders, so it can't have settings of its own here.";
    }
    else if (setup.ready() && iequals(t.rom, setup.library)) {
        t.problem = "This game is named like its core (" + setup.library + "), so its own files would be the core's: "
                    "change these on its system's RetroArch screen instead.";
    }
    return t;
}

Target coreTarget(const Setup& setup)
{
    Target t;
    t.setup = setup;
    return t;
}

TileFiles files(const Target& target, Tile tile)
{
    TileFiles f;
    const Setup& s = target.setup;
    if (!s.ready() || !target.problem.empty()) return f;
    std::string configFolder = join(s.configDir, s.library);
    std::string remapFolder = join(s.remapDir, s.library);
    std::string name = target.game() ? target.rom : s.library;
    // A game's folder file, when its folder isn't named like the game or the core
    bool folder = target.game() && !target.contentFolder.empty() && !iequals(target.contentFolder, target.rom)
        && !iequals(target.contentFolder, s.library);
    switch (tile) {
    case Tile::Settings:
        f.own = join(configFolder, name + ".cfg");
        f.layers.push_back(s.configFile);
        if (target.game()) {
            std::string core = join(configFolder, s.library + ".cfg");
            if (isFile(core)) f.layers.push_back(core);
            if (folder && isFile(join(configFolder, target.contentFolder + ".cfg"))) {
                f.layers.push_back(join(configFolder, target.contentFolder + ".cfg"));
            }
        }
        break;
    case Tile::CoreOptions:
        // Without a <Core>.opt (per-core options, RetroArch's default), RetroArch
        // takes a core's values from the legacy all-cores file, and makes
        // <Core>.opt from it: so that file is what applies, and seeds a new one
        if (!target.game()) {
            f.own = s.globalCoreOptions ? s.globalOptionsFile : join(configFolder, s.library + ".opt");
            if (!s.globalCoreOptions && !isFile(f.own) && isFile(s.globalOptionsFile)) {
                f.below = s.globalOptionsFile;
                f.belowShared = true;
            }
        }
        else {
            f.own = join(configFolder, name + ".opt");
            std::string folderFile = folder ? join(configFolder, target.contentFolder + ".opt") : "";
            std::string core = join(configFolder, s.library + ".opt");
            if (!folderFile.empty() && isFile(folderFile)) {
                f.below = folderFile;
            }
            else if (!s.globalCoreOptions && isFile(core)) {
                f.below = core;
            }
            else if (isFile(s.globalOptionsFile)) {
                f.below = s.globalOptionsFile;
                f.belowShared = true;
            }
        }
        break;
    case Tile::Controls:
        f.own = join(remapFolder, name + ".rmp");
        if (target.game()) {
            std::string folderFile = folder ? join(remapFolder, target.contentFolder + ".rmp") : "";
            std::string core = join(remapFolder, s.library + ".rmp");
            if (!folderFile.empty() && isFile(folderFile)) f.below = folderFile;
            else if (isFile(core)) f.below = core;
        }
        break;
    }
    return f;
}

// ---- Public: the curated rows ---------------------------------------------------------------

const std::vector<RowDef>& settingsRows()
{
    static const std::vector<RowDef> rows = {
        { "aspect", "Aspect ratio", { { "Core provided", { { "aspect_ratio_index", "22" } } }, { "4:3", { { "aspect_ratio_index", "0" } } },
                                      { "16:9", { { "aspect_ratio_index", "1" } } }, { "Fill the screen", { { "aspect_ratio_index", "24" } } } } },
        { "smooth", "Smooth picture", { { "On", { { "video_smooth", "true" } } }, { "Off", { { "video_smooth", "false" } } } } },
        { "shader", "Shader", { { "On", { { "video_shader_enable", "true" } } }, { "Off", { { "video_shader_enable", "false" } } } } },
        { "overlay", "Bezel (overlay)", { { "Show", { { "input_overlay_enable", "true" } } }, { "Hide", { { "input_overlay_enable", "false" } } } } },
        { "integer", "Integer scaling", { { "On", { { "video_scale_integer", "true" } } }, { "Off", { { "video_scale_integer", "false" } } } } },
        { "runahead", "Run-ahead (less input lag)", { { "Off", { { "run_ahead_enabled", "false" } } },
                                                      { "1 frame", { { "run_ahead_enabled", "true" }, { "run_ahead_frames", "1" } } },
                                                      { "2 frames", { { "run_ahead_enabled", "true" }, { "run_ahead_frames", "2" } } } } },
        { "volume", "Volume", { { "-6 dB", { { "audio_volume", "-6.000000" } } }, { "-3 dB", { { "audio_volume", "-3.000000" } } },
                                { "Normal", { { "audio_volume", "0.000000" } } }, { "+3 dB", { { "audio_volume", "3.000000" } } },
                                { "+6 dB", { { "audio_volume", "6.000000" } } } } },
        { "resume", "Resume where you left off", { { "On", { { "savestate_auto_load", "true" }, { "savestate_auto_save", "true" } } },
                                                   { "Off", { { "savestate_auto_load", "false" }, { "savestate_auto_save", "false" } } } } },
    };
    return rows;
}

const std::vector<RowDef>& controlRows()
{
    static const std::vector<RowDef> rows = [] {
        std::vector<RowDef> out;
        out.push_back({ "dpad", "Stick moves the D-pad", { { "On", { { "input_player1_analog_dpad_mode", "1" } } },
                                                           { "Off", { { "input_player1_analog_dpad_mode", "0" } } } } });
        for (const PadButton& button : padButtons) {
            RowDef def;
            def.id = std::string("btn_") + button.key;
            def.label = std::string(button.name) + " button sends";
            std::string key = std::string("input_player1_btn_") + button.key;
            for (const PadButton& to : padButtons) {
                def.choices.push_back({ to.name, { { key, to.id } } });
            }
            def.choices.push_back({ "Nothing", { { key, "-1" } } });
            out.push_back(def);
        }
        return out;
    }();
    return rows;
}

std::vector<RowDef> coreOptionRows(const std::string& library)
{
    std::vector<RowDef> rows;
    for (const OptionDef& o : optionTable()) {
        if (library != o.library) continue;
        RowDef def;
        def.id = o.key;
        def.label = o.label;
        for (const auto& [value, label] : o.values) {
            def.choices.push_back({ label, { { o.key, value } } });
        }
        rows.push_back(def);
    }
    return rows;
}

std::string coreOptionPrefix(const std::string& library)
{
    return optionPrefix(library);
}

std::vector<std::string> coresWithOptions()
{
    std::vector<std::string> cores;
    for (const OptionDef& o : optionTable()) {
        if (std::find(cores.begin(), cores.end(), o.library) == cores.end()) cores.push_back(o.library);
    }
    return cores;
}

std::vector<RowDef> rowDefs(const Target& target, Tile tile)
{
    switch (tile) {
    case Tile::Settings: return settingsRows();
    case Tile::Controls: return controlRows();
    case Tile::CoreOptions: return coreOptionRows(target.setup.library);
    }
    return {};
}

TileState read(const Target& target, Tile tile, const Backups& backups)
{
    TileState state;
    if (!target.usable()) return state;
    state.files = files(target, tile);
    Layers l = readLayers(target, tile, state.files);
    state.ownExists = l.ownExists;
    state.canRestore = differsFromOriginal(backups, state.files.own);

    for (const RowDef& def : rowDefs(target, tile)) {
        Row row;
        row.id = def.id;
        row.label = def.label;
        std::vector<std::string> keys = keysOf(def);
        int inherited = -1;
        int current = -1;
        for (size_t i = 0; i < def.choices.size(); i++) {
            if (inherited < 0 && matches(l, tile, def.choices[i], false)) inherited = static_cast<int>(i);
            if (current < 0 && matches(l, tile, def.choices[i], true)) current = static_cast<int>(i);
        }
        std::string rawBase;
        std::string rawCurrent;
        for (const std::string& key : keys) {
            if (l.own.count(key)) row.own = true;
            rawBase += (rawBase.empty() ? "" : ", ") + valueOf(l, tile, key, false);
            rawCurrent += (rawCurrent.empty() ? "" : ", ") + valueOf(l, tile, key, true);
        }
        std::string inheritedText = inherited >= 0 ? def.choices[static_cast<size_t>(inherited)].label
            : (trim(rawBase) == "" || trim(rawBase) == ",") ? "not set" : rawBase;
        row.choices.push_back("Default (" + inheritedText + ")");
        for (const Choice& c : def.choices) row.choices.push_back(c.label);
        bool changed = row.own || (l.oneFile && l.ownExists && current != inherited);
        row.differs = changed && (current != inherited || current < 0);
        if (!changed) {
            row.current = 0;
            row.currentText = row.choices[0];
        }
        else if (current >= 0) {
            row.current = current + 1;
            row.currentText = def.choices[static_cast<size_t>(current)].label;
        }
        else {
            row.current = -1;
            row.currentText = rawCurrent + " (not one of these choices)";
        }
        state.rows.push_back(row);
    }

    const Setup& s = target.setup;
    if (tile == Tile::Settings && !s.overrides) {
        state.disabledNote = "RetroArch is set not to load these files (auto_overrides_enable = false in retroarch.cfg), "
                             "so they don't apply until that's turned back on.";
    }
    else if (tile == Tile::CoreOptions && target.game() && !s.gameOptions) {
        state.disabledNote = "RetroArch is set not to load a game's own options (game_specific_options = false in retroarch.cfg), "
                             "so they don't apply until that's turned back on.";
    }
    else if (tile == Tile::Controls && !s.remaps) {
        state.disabledNote = "RetroArch is set not to load remap files (auto_remaps_enable = false in retroarch.cfg), "
                             "so they don't apply until that's turned back on.";
    }
    return state;
}

// ---- Public: writing ------------------------------------------------------------------------

EditResult apply(Backups& backups, const Target& target, Tile tile, const std::map<std::string, int>& choices)
{
    if (!target.usable()) {
        std::string why = !target.problem.empty() ? target.problem : target.setup.why;
        return EditResult::fail(why.empty() ? "RetroArch isn't set up for this." : why);
    }
    std::vector<RowDef> defs = rowDefs(target, tile);
    TileFiles f = files(target, tile);
    if (f.own.empty()) {
        return EditResult::fail("There's no file for these settings.");
    }
    Layers l = readLayers(target, tile, f);

    // Only the curated keys, with the values the table gives
    std::map<std::string, std::optional<std::string>> edits;
    for (const auto& [id, choice] : choices) {
        auto def = std::find_if(defs.begin(), defs.end(), [&](const RowDef& d) { return d.id == id; });
        if (def == defs.end()) {
            return EditResult::fail("\"" + id + "\" isn't one of the settings this screen can change.");
        }
        if (choice < 0 || choice > static_cast<int>(def->choices.size())) {
            return EditResult::fail("That isn't one of the choices for " + def->label + ".");
        }
        if (choice == 0) {
            for (const std::string& key : keysOf(*def)) {
                // One file wins: "Default" is what the file under it says
                auto under = l.below.find(key);
                if (l.oneFile && under != l.below.end()) edits[key] = under->second;
                else edits[key] = std::nullopt;
            }
        }
        else {
            for (const auto& [key, value] : def->choices[static_cast<size_t>(choice - 1)].assign) {
                edits[key] = value;
            }
        }
    }
    if (edits.empty()) {
        return EditResult{ true, "No changes.", {} };
    }
    // RetroArch settings keep only what differs from the layers under them
    if (tile == Tile::Settings) {
        for (auto& [key, value] : edits) {
            auto under = l.base.find(key);
            if (value && under != l.base.end() && sameValue(under->second, *value)) value = std::nullopt;
        }
    }

    // A new options or controls file starts as a copy of the one under it (of
    // this core's entries only, when that's RetroArch's all-cores options file)
    std::string start = l.ownText;
    if (!l.ownExists && l.oneFile && !f.below.empty()) {
        if (f.belowShared) {
            for (const auto& [key, value] : l.below) start += key + " = \"" + value + "\"\n";
        }
        else {
            readFile(f.below, start);
        }
    }
    std::string text = editConfig(start, edits);
    std::map<std::string, std::string> result = parseConfig(text);
    // An #include line keeps the file: what it brings in isn't Manage's to judge
    bool includes = false;
    for (const Line& line : Lines::parse(text).lines) {
        if (!includeOf(line.text).empty()) includes = true;
    }

    // Nothing left, or nothing that differs from what it shadows: the file goes
    bool remove = result.empty() && !includes;
    if (includes) {
        remove = false;
    }
    else if (!remove && tile == Tile::Settings) {
        remove = std::all_of(result.begin(), result.end(), [&](const auto& kv) {
            auto under = l.base.find(kv.first);
            return under != l.base.end() && sameValue(under->second, kv.second);
        });
    }
    else if (!remove && l.oneFile) {
        std::set<std::string> keys;
        for (const auto& kv : result) keys.insert(kv.first);
        for (const auto& kv : l.below) keys.insert(kv.first);
        remove = std::all_of(keys.begin(), keys.end(), [&](const std::string& key) {
            auto effective = [&](const std::map<std::string, std::string>& map) {
                auto found = map.find(key);
                return found != map.end() ? found->second : valueIn(l.builtin, key);
            };
            return sameValue(effective(result), effective(l.below));
        });
    }

    std::string error;
    std::string fileName = fileNameOf(f.own);
    if (remove) {
        if (!l.ownExists) {
            return EditResult{ true, "No changes: that's what applies already.", {} };
        }
        if (!backups.removeKeepingOriginal(f.own, error)) {
            return EditResult::fail(error);
        }
        std::string what = target.game() ? "This game follows the core's " + tileNoun(tile) + " again"
                                         : "The core uses RetroArch's own " + tileNoun(tile) + " again";
        return EditResult{ true, what + " (" + fileName + " had nothing else, so it was removed; a copy is in manage/backups).", { f.own } };
    }
    if (l.ownExists && text == l.ownText) {
        return EditResult{ true, "No changes.", {} };
    }
    if (!backups.beforeChange(f.own, error) || !writeFileAtomic(f.own, text, error)) {
        return EditResult::fail(error.empty() ? "Could not write " + f.own : error);
    }
    return EditResult{ true, "Saved in " + fileName + ". RetroArch reads it the next time it starts this "
                             + std::string(target.game() ? "game." : "core."), { f.own } };
}

EditResult useCoreSettings(Backups& backups, const Target& target, Tile tile)
{
    if (!target.usable()) return EditResult::fail(target.problem.empty() ? target.setup.why : target.problem);
    if (!target.game()) return EditResult::fail("These are the core's own settings already.");
    TileFiles f = files(target, tile);
    if (!isFile(f.own)) return EditResult::fail("This game has no " + tileNoun(tile) + " file of its own.");
    std::string error;
    if (!backups.removeKeepingOriginal(f.own, error)) return EditResult::fail(error);
    return EditResult{ true, "This game follows the core's " + tileNoun(tile) + " again. Its own file (" + fileNameOf(f.own)
                                 + ") is kept in manage/backups.", { f.own } };
}

EditResult restoreOriginal(Backups& backups, const Target& target, Tile tile)
{
    if (!target.usable()) return EditResult::fail(target.problem.empty() ? target.setup.why : target.problem);
    TileFiles f = files(target, tile);
    if (!differsFromOriginal(backups, f.own)) {
        return EditResult::fail("Manage hasn't changed these " + tileNoun(tile) + ".");
    }
    std::string error;
    if (!backups.restore(f.own, error)) return EditResult::fail(error);
    return EditResult{ true, "Put back the " + tileNoun(tile) + " as they were before Manage changed them.", { f.own } };
}

}
}
