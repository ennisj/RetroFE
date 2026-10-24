# Launchers
[Back](README.md)


A launcher config file describes how to launch a program (i.e. emulator,
application, or game) when a launchable menu item is selected.

See below for a list of supported configuration properties. Launcher
options

| Property   | Description                                                   |
|------------|---------------------------------------------------------------|
| executable | Path of where the executable exists                           |
| arguments  | Arguments to pass when executing the launcher (i.e. ROM name) |
| currentDirectory | The folder the program runs in; by default the executable's own folder, or RetroFE's root folder for a bare name such as `cmd.exe` |

    executable = D:/Emulators/Nestopia/nestopia.exe
    arguments  = "%ITEM_FILEPATH%"

%ITEM_FILEPATH% is a reserved variable name. See the variables table
below for other variables that may be used. Also note the quotes around
"%ITEM_FILEPATH%" to help not confuse the executable from thinking that
an item with spaces as multiple arguments.

Assuming that "Super Mario Bros" was the selected item, the frontend
will attempt to execute:

    "D:/Emulators/Nestopia/nestopia.exe" "D:/ROMs/Nintendo/Super Mario Bros.nes".

**PS**: You can also use relative paths (relative to the root folder of
RetroFE)

    executable = ../Emulators/Nestopia/nestopia.exe
    arguments  = "%ITEM_FILEPATH%"

On Windows a bare program name such as `cmd.exe` that isn't in RetroFE's
root folder is looked up the way Windows finds programs (RetroFE's program
folder, the system folders, then `PATH`), as a command prompt would; Manage's
Emulators screen checks it the same way. For `cmd.exe`, a `/c` (or `/k`)
command with more than two quotes, such as
`/c "%RETROFE_PATH%/scripts/switch.bat" "%ITEM_FILEPATH%"`, is wrapped in one
more pair of quotes, because cmd drops the first and the last quote of such a
command.

Launcher variables

| Variable               | Description                 | Translated Example                    |
|------------------------|-----------------------------|---------------------------------------|
| %ITEM_FILEPATH%        | Full item path              | D:/ROMs/Nintendo/Super Mario Bros.nes |
| %ITEM_NAME%            | The item name               | Super Mario Bros                      |
| %ITEM_FILENAME%        | Filename without path       | Super Mario Bros.nes                  |
| %ITEM_DIRECTORY%       | Folder where file exists    | D:/ROMs/Nintendo                      |
| %ITEM_COLLECTION_NAME% | Name of collection for item | Nintendo Entertainment System         |
| %RETROFE_PATH%         | Folder location of Frontend | D:/Frontends/RetroFE                  |
| %RETROFE_EXEC_PATH%    | Location of RetroFE         | D:/Frontends/RetroFE/RetroFE.exe      |
| %COLLECTION_PATH%      | Full path to collection     | D:/Frontends/collections/Genesis      |
| %CMD% (Windows only)   | Path to system cmd.exe      | C:/Windows/system32/cmd.exe           |

More elaborate example:

    # Have fceux load a save state automatically for the ROM when started
    executable = D:/Emulators/fceux/fceux.exe
    arguments  = "%ITEM_FILEPATH%" -loadstate "%ITEM_DIRECTORY%/%ITEM_NAME%.fcs"

[Back](README.md)
