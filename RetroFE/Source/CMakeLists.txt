cmake_minimum_required(VERSION 3.12)

project (retrofe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(RETROFE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RETROFE_THIRD_PARTY_DIR "${RETROFE_DIR}/ThirdParty")
list(APPEND CMAKE_MODULE_PATH "${RETROFE_DIR}/CMake")

##############################################################
# Versioning
# Ensure versioning.h is generated before compiling
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/autogen/versioning.h"
    COMMAND ${CMAKE_COMMAND} "-DRETROFE_RELEASE_VERSION=${RETROFE_RELEASE_VERSION}" -P "${RETROFE_DIR}/CMake/Versioning.cmake"
    COMMENT "Generating versioning.h"
)
# Create a custom target to generate versioning.h
add_custom_target(GenerateVersioningHeader ALL DEPENDS "${CMAKE_BINARY_DIR}/autogen/versioning.h")
include_directories("${CMAKE_BINARY_DIR}/autogen")

##############################################################
# Built-in font
# Compiled into the executable so RetroFE can render text with no layout installed
set(BUILTIN_FONT_FILE "${RETROFE_DIR}/ThirdParty/fonts/Roboto-Regular.ttf")
set(BUILTIN_FONT_SOURCE "${CMAKE_BINARY_DIR}/autogen/BuiltinFont.cpp")
add_custom_command(
    OUTPUT "${BUILTIN_FONT_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${BUILTIN_FONT_FILE} -DOUTPUT=${BUILTIN_FONT_SOURCE} -DSYMBOL=retrofe_builtin_font -P "${RETROFE_DIR}/CMake/EmbedFile.cmake"
    DEPENDS "${BUILTIN_FONT_FILE}" "${RETROFE_DIR}/CMake/EmbedFile.cmake"
    COMMENT "Embedding built-in font"
)

##############################################################
# Setup some variables to help find external libraries
##############################################################

set(SQLITE3_ROOT "${RETROFE_THIRD_PARTY_DIR}/sqlite3")
set(RAPIDXML_ROOT "${RETROFE_THIRD_PARTY_DIR}/rapidxml-1.13")
set(IMGUI_ROOT "${RETROFE_THIRD_PARTY_DIR}/imgui-1.91.5")
set(NLOHMANN_JSON_ROOT "${RETROFE_THIRD_PARTY_DIR}/nlohmann-json-3.11.3")

if(WIN32)
    if(MSVC)
        set(SDL2_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2-2.30.0")
        set(SDL2_IMAGE_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_image-2.8.2")
        set(SDL2_MIXER_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_mixer-2.6.3")
        set(SDL2_TTF_ROOT "${RETROFE_THIRD_PARTY_DIR}/SDL2_ttf-2.20.2")
    endif()

    set(ZLIB_ROOT "${RETROFE_THIRD_PARTY_DIR}/zlib13-dll")
    set(GSTREAMER_ROOT "C:/gstreamer/1.0/msvc_x64" CACHE STRING "location of where your gstreamer include and lib folders reside")
    set(GLIB2_ROOT "${GSTREAMER_ROOT}")

    # Set PacDrive paths
    set(PACDRIVE_ROOT "${RETROFE_THIRD_PARTY_DIR}/PacDrive")
    include_directories("${PACDRIVE_ROOT}")
    
    # Set paths for libwebp on Windows
    set(WEBP_ROOT "${RETROFE_THIRD_PARTY_DIR}/libwebp")
    set(WEBP_LIB "${WEBP_ROOT}/lib/libwebp_dll.lib")
    include_directories("${WEBP_ROOT}/include")

endif()

# macOS builds either RetroFE.app against the frameworks in ThirdPartyMac (what
# build-macos.yml packages; the default once SDL2.framework is there) or a plain
# retrofe executable against Homebrew's libraries
set(RETROFE_MAC_THIRD_PARTY_DIR "${RETROFE_DIR}/ThirdPartyMac")
if(APPLE AND EXISTS "${RETROFE_MAC_THIRD_PARTY_DIR}/SDL2.framework")
    set(RETROFE_MACOS_BUNDLE_DEFAULT ON)
else()
    set(RETROFE_MACOS_BUNDLE_DEFAULT OFF)
endif()
option(RETROFE_MACOS_BUNDLE "macOS: build RetroFE.app with the frameworks in ThirdPartyMac" ${RETROFE_MACOS_BUNDLE_DEFAULT})

if(WIN32)
    find_package(Glib2 REQUIRED)
    find_package(GStreamer REQUIRED)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_mixer REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(ZLIB REQUIRED)

elseif(APPLE AND RETROFE_MACOS_BUNDLE)
    # RetroFE.app, built against the frameworks in ThirdPartyMac: SDL2, SDL2_image (and its
    # webp), SDL2_mixer and SDL2_ttf from their release DMGs, GStreamer.framework from its
    # installer, and the universal libminizip and libusb there, all of which are copied
    # into the bundle (.github/workflows/build-macos.yml sets them up). minizip's and
    # libusb's headers come from Homebrew (brew install minizip libusb).
    set(MAC_FRAMEWORKS_DIR "${RETROFE_MAC_THIRD_PARTY_DIR}")
    foreach(item SDL2.framework SDL2_image.framework SDL2_mixer.framework SDL2_ttf.framework
                 webp.framework GStreamer.framework libminizip.dylib libusb-1.0.0.dylib)
        if(NOT EXISTS "${MAC_FRAMEWORKS_DIR}/${item}")
            message(FATAL_ERROR "RETROFE_MACOS_BUNDLE needs ${MAC_FRAMEWORKS_DIR}/${item}")
        endif()
    endforeach()
    find_package(ZLIB REQUIRED)
    find_path(MINIZIP_INCLUDE_DIR minizip/unzip.h HINTS /opt/homebrew/include /usr/local/include)
    find_path(LIBUSB_INCLUDE_DIR libusb-1.0/libusb.h HINTS /opt/homebrew/include /usr/local/include)
    if(NOT MINIZIP_INCLUDE_DIR OR NOT LIBUSB_INCLUDE_DIR)
        message(FATAL_ERROR "RETROFE_MACOS_BUNDLE needs minizip's and libusb's headers: brew install minizip libusb")
    endif()

    # <SDL2/SDL.h>, <SDL2_ttf/SDL_ttf.h>, <webp/decode.h> and so on resolve through -F; the
    # frameworks' own headers and ImGui's SDL backends include "SDL.h" and need
    # SDL2.framework/Headers, the rest of the code needs GStreamer's (gst/, glib.h)
    add_compile_options("-F${MAC_FRAMEWORKS_DIR}")
    set(SDL2_INCLUDE_DIRS "${MAC_FRAMEWORKS_DIR}/SDL2.framework/Headers")
    set(GSTREAMER_INCLUDE_DIRS "${MAC_FRAMEWORKS_DIR}/GStreamer.framework/Headers")
    set(RETROFE_PLATFORM_INCLUDE_DIRS ${MINIZIP_INCLUDE_DIR} ${LIBUSB_INCLUDE_DIR})
    set(MINIZIP_LIBRARIES "${MAC_FRAMEWORKS_DIR}/libminizip.dylib")
    # GStreamer's libraries directly: GStreamer.framework's own binary only re-exports
    # them by @rpath/lib/..., which the linker can't follow. RetroFE finds them in the
    # bundle through the @executable_path/../Frameworks/GStreamer.framework/Libraries rpath.
    set(MAC_GSTREAMER_LIBRARIES)
    foreach(library gstreamer-1.0 gstbase-1.0 gstvideo-1.0 gstaudio-1.0 gstapp-1.0 gstpbutils-1.0 gobject-2.0 glib-2.0)
        string(MAKE_C_IDENTIFIER "MAC_GSTREAMER_${library}" variable)
        find_library(${variable} ${library}
            PATHS "${MAC_FRAMEWORKS_DIR}/GStreamer.framework/Versions/1.0/lib" NO_DEFAULT_PATH)
        if(NOT ${variable})
            message(FATAL_ERROR "RETROFE_MACOS_BUNDLE: no lib${library} in GStreamer.framework")
        endif()
        list(APPEND MAC_GSTREAMER_LIBRARIES "${${variable}}")
    endforeach()
    # What RetroFE.app carries in Contents/Frameworks
    set(RETROFE_MACOS_EMBEDDED
        SDL2.framework SDL2_image.framework SDL2_mixer.framework SDL2_ttf.framework
        webp.framework GStreamer.framework libminizip.dylib libusb-1.0.0.dylib)
    set(RETROFE_LIBRARIES
        "${MAC_FRAMEWORKS_DIR}/SDL2.framework"
        "${MAC_FRAMEWORKS_DIR}/SDL2_image.framework"
        "${MAC_FRAMEWORKS_DIR}/SDL2_mixer.framework"
        "${MAC_FRAMEWORKS_DIR}/SDL2_ttf.framework"
        "${MAC_FRAMEWORKS_DIR}/webp.framework"
        ${MAC_GSTREAMER_LIBRARIES}
        "${MAC_FRAMEWORKS_DIR}/libminizip.dylib"
        "${MAC_FRAMEWORKS_DIR}/libusb-1.0.0.dylib"
        ${ZLIB_LIBRARIES}
        "-framework CoreFoundation"
        "-framework IOKit"
    )

else()
    # Linux, and macOS with Homebrew's libraries (brew install pkg-config sdl2 sdl2_image
    # sdl2_mixer sdl2_ttf gstreamer minizip webp libusb)
    include(FindPkgConfig)

    # Find necessary libraries using pkg-config
    pkg_search_module(SDL2 REQUIRED sdl2)
    pkg_search_module(SDL2_IMAGE REQUIRED SDL2_image)
    pkg_search_module(SDL2_MIXER REQUIRED SDL2_mixer)
    pkg_search_module(SDL2_TTF REQUIRED SDL2_ttf)
    if(APPLE)
        # macOS's own zlib, which has no pkg-config file of its own
        find_package(ZLIB REQUIRED)
    else()
        pkg_search_module(ZLIB REQUIRED zlib)
    endif()
    pkg_search_module(MINIZIP REQUIRED minizip)
    pkg_search_module(WEBP REQUIRED libwebp)
    pkg_check_modules(WEBP_DEMUX REQUIRED libwebpdemux)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-audio-1.0 gstreamer-app-1.0)
    pkg_check_modules(Glib2 REQUIRED glib-2.0 gobject-2.0 gthread-2.0 gmodule-2.0)
    pkg_search_module(LIBUSB REQUIRED libusb-1.0)
    if(NOT APPLE)
        pkg_check_modules(LIBEVDEV REQUIRED libevdev)
    endif()
    find_package(Threads REQUIRED)
    if(APPLE)
        # <libusb-1.0/libusb.h>, <gstreamer-1.0/gst/gst.h> and <SDL2/SDL.h> are under
        # Homebrew's include folder, beside the pkg-config that found the rest
        get_filename_component(HOMEBREW_BIN_DIR "${PKG_CONFIG_EXECUTABLE}" DIRECTORY)
        get_filename_component(HOMEBREW_PREFIX_DIR "${HOMEBREW_BIN_DIR}" DIRECTORY)
        set(RETROFE_PLATFORM_INCLUDE_DIRS "${HOMEBREW_PREFIX_DIR}/include")
    endif()
    
    # Include directories for all found libraries
    include_directories(
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SDL2_MIXER_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
        ${MINIZIP_INCLUDE_DIRS}
        ${WEBP_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${Threads_INCLUDE_DIRS}
        ${LIBUSB_INCLUDE_DIRS}
        ${LIBUSB_INCLUDE_DIR}
        ${LIBEVDEV_INCLUDE_DIRS}
    )
    
    # Link directories (optional, mostly handled by pkg-config)
    link_directories(
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_IMAGE_LIBRARY_DIRS}
        ${SDL2_MIXER_LIBRARY_DIRS}
        ${SDL2_TTF_LIBRARY_DIRS}
        ${ZLIB_LIBRARY_DIRS}
        ${MINIZIP_LIBRARY_DIRS}
        ${WEBP_LIBRARY_DIRS}
        ${GSTREAMER_LIBRARY_DIRS}
        ${GLIB2_LIBRARY_DIRS}
        ${LIBUSB_LIBRARY_DIRS}
        ${LIBEVDEV_LIBRARY_DIRS}
    )
    
    # Append the necessary libraries to be linked
    list(APPEND RETROFE_LIBRARIES
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
        ${SDL2_MIXER_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${MINIZIP_LIBRARIES}
        ${WEBP_LIBRARIES}
        ${WEBP_DEMUX_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GLIB2_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${CMAKE_THREAD_LIBS_INIT}
        ${LIBUSB_LIBRARIES}
        ${LIBEVDEV_LIBRARIES}
    )
    if(APPLE)
        list(APPEND RETROFE_LIBRARIES "-framework CoreFoundation" "-framework IOKit")
    else()
        add_definitions(${LIBEVDEV_CFLAGS_OTHER})
    endif()
endif()

set(RETROFE_INCLUDE_DIRS
    "${GLIB2_INCLUDE_DIRS}"
    "${GSTREAMER_INCLUDE_DIRS}"
    "${SDL2_INCLUDE_DIRS}"
    "${SDL2_IMAGE_INCLUDE_DIRS}"
    "${SDL2_MIXER_INCLUDE_DIRS}"
    "${SDL2_TTF_INCLUDE_DIRS}"
    "${ZLIB_INCLUDE_DIRS}"
    "${WEBP_INCLUDE_DIRS}"
    "${SQLITE3_ROOT}"
    "${RAPIDXML_ROOT}"
    "${IMGUI_ROOT}"
    "${IMGUI_ROOT}/backends"
    "${NLOHMANN_JSON_ROOT}"
    "${PACDRIVE_ROOT}"
    ${RETROFE_PLATFORM_INCLUDE_DIRS}
)

# Link PacDrive library if on Windows
if(WIN32)
    set(RETROFE_LIBRARIES
    ${GLIB2_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_MIXER_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${ZLIB_LIBRARIES}
)
    list(APPEND RETROFE_LIBRARIES "${PACDRIVE_ROOT}/PacDrive64.lib")
	    # Add libwebp, libwebpdecoder, and libwebpdemux to the libraries to be linked on Windows
    list(APPEND RETROFE_LIBRARIES 
        "${WEBP_LIB}"
        "${WEBP_ROOT}/lib/libwebpdecoder_dll.lib"
        "${WEBP_ROOT}/lib/libwebpdemux_dll.lib"
        "${ZLIB_ROOT}/lib/minizip.lib"
        "dxgi.lib"
        "winhttp.lib"
    )
endif()

if(NOT WIN32)
    LIST(APPEND RETROFE_LIBRARIES ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
endif()

set(RETROFE_HEADERS
	"${RETROFE_DIR}/Source/Builtin/Builtin.h"
	"${RETROFE_DIR}/Source/Collection/CollectionInfo.h"
	"${RETROFE_DIR}/Source/Collection/CollectionInfoBuilder.h"
	"${RETROFE_DIR}/Source/Collection/Item.h"
	"${RETROFE_DIR}/Source/Collection/MenuParser.h"
	"${RETROFE_DIR}/Source/Control/UserInput.h"
	"${RETROFE_DIR}/Source/Control/InputHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyAxisHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyButtonHandler.h"
	"${RETROFE_DIR}/Source/Control/JoyHatHandler.h"
	"${RETROFE_DIR}/Source/Control/KeyboardHandler.h"
	"${RETROFE_DIR}/Source/Control/MouseButtonHandler.h"
	"${RETROFE_DIR}/Source/Database/Configuration.h"
	"${RETROFE_DIR}/Source/Database/DB.h"
	"${RETROFE_DIR}/Source/Database/GlobalOpts.h"
    "${RETROFE_DIR}/Source/Database/HiScores.h"
	"${RETROFE_DIR}/Source/Execute/AttractMode.h"
	"${RETROFE_DIR}/Source/Execute/Launcher.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/Tween.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenTypes.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenSet.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/Animation.h"
	"${RETROFE_DIR}/Source/Graphics/Animate/AnimationEvents.h"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBinding.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Container.h"
	"${RETROFE_DIR}/Source/Graphics/Component/FormWidget.h"
	"${RETROFE_DIR}/Source/Graphics/Component/TextureCache.h"
	"${RETROFE_DIR}/Source/Graphics/Component/KeyboardWidget.h"
	"${RETROFE_DIR}/Source/Graphics/Component/WidgetPainter.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Component.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Image.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ImageBuilder.h"
    "${RETROFE_DIR}/Source/Graphics/Component/ReloadableHiscores.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableMedia.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableText.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableScrollingText.h"
	"${RETROFE_DIR}/Source/Graphics/Component/ScrollingList.h"
	"${RETROFE_DIR}/Source/Graphics/Component/Text.h"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoComponent.h"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/Font.h"
	"${RETROFE_DIR}/Source/Graphics/FontCache.h"
	"${RETROFE_DIR}/Source/Graphics/LayoutPages.h"
	"${RETROFE_DIR}/Source/Graphics/VideoPlayback.h"
	"${RETROFE_DIR}/Source/Collection/VideoCollections.h"
	"${RETROFE_DIR}/Source/Graphics/PageBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/Page.h"
	"${RETROFE_DIR}/Source/Graphics/ThreadPool.h"
	"${RETROFE_DIR}/Source/Menu/Menu.h"
	"${RETROFE_DIR}/Source/Sound/Sound.h"
	"${RETROFE_DIR}/Source/Utility/FileStamp.h"
	"${RETROFE_DIR}/Source/Utility/Log.h"
	"${RETROFE_DIR}/Source/Utility/Utils.h"
	"${RETROFE_DIR}/Source/Video/IVideo.h"
	"${RETROFE_DIR}/Source/Video/GStreamerVideo.h"
	"${RETROFE_DIR}/Source/Video/VideoFactory.h"
    "${RETROFE_DIR}/Source/Video/VideoPool.h"
   	"${RETROFE_DIR}/Source/Graphics/ComponentItemBindingBuilder.h"
	"${RETROFE_DIR}/Source/Graphics/ViewInfo.h"
	"${RETROFE_DIR}/Source/Forms/BuiltinForms.h"
	"${RETROFE_DIR}/Source/Forms/Form.h"
	"${RETROFE_DIR}/Source/Forms/SettingsFile.h"
	"${RETROFE_DIR}/Source/Library/Backups.h"
	"${RETROFE_DIR}/Source/Library/ConfFile.h"
	"${RETROFE_DIR}/Source/Library/Edits.h"
	"${RETROFE_DIR}/Source/Library/Files.h"
	"${RETROFE_DIR}/Source/Library/Index.h"
	"${RETROFE_DIR}/Source/Library/Indexer.h"
	"${RETROFE_DIR}/Source/Library/LineList.h"
	"${RETROFE_DIR}/Source/Library/Media.h"
	"${RETROFE_DIR}/Source/Library/MetadataOverrides.h"
	"${RETROFE_DIR}/Source/Library/Properties.h"
	"${RETROFE_DIR}/Source/Library/RetroArch.h"
	"${RETROFE_DIR}/Source/Library/Search.h"
	"${RETROFE_DIR}/Source/Manager/EditBuffer.h"
	"${RETROFE_DIR}/Source/Manager/FileDialog.h"
	"${RETROFE_DIR}/Source/Manager/FormView.h"
	"${RETROFE_DIR}/Source/Manager/Home.h"
	"${RETROFE_DIR}/Source/Manager/ImGuiScreen.h"
	"${RETROFE_DIR}/Source/Manager/ManagerModel.h"
	"${RETROFE_DIR}/Source/Manager/ManagerUI.h"
	"${RETROFE_DIR}/Source/Manager/Navigator.h"
	"${RETROFE_DIR}/Source/Manager/Provider.h"
	"${RETROFE_DIR}/Source/Manager/Setup.h"
	"${RETROFE_DIR}/Source/RetroFE.h"
	"${RETROFE_DIR}/Source/SDL.h"
    "${SQLITE3_ROOT}/sqlite3.h"
)

set(RETROFE_SOURCES
	"${RETROFE_DIR}/Source/Builtin/Builtin.cpp"
	"${BUILTIN_FONT_SOURCE}"
	"${RETROFE_DIR}/Source/Collection/CollectionInfo.cpp"
	"${RETROFE_DIR}/Source/Collection/CollectionInfoBuilder.cpp"
	"${RETROFE_DIR}/Source/Collection/Item.cpp"
	"${RETROFE_DIR}/Source/Collection/MenuParser.cpp"
	"${RETROFE_DIR}/Source/Collection/VideoCollections.cpp"
	"${RETROFE_DIR}/Source/Control/UserInput.cpp"
	"${RETROFE_DIR}/Source/Control/JoyAxisHandler.cpp"
	"${RETROFE_DIR}/Source/Control/JoyButtonHandler.cpp"
	"${RETROFE_DIR}/Source/Control/JoyHatHandler.cpp"
	"${RETROFE_DIR}/Source/Control/KeyboardHandler.cpp"
	"${RETROFE_DIR}/Source/Control/MouseButtonHandler.cpp"
	"${RETROFE_DIR}/Source/Database/Configuration.cpp"
	"${RETROFE_DIR}/Source/Database/DB.cpp"
	"${RETROFE_DIR}/Source/Database/GlobalOpts.cpp"
    "${RETROFE_DIR}/Source/Database/HiScores.cpp"
	"${RETROFE_DIR}/Source/Database/MetadataDatabase.cpp"
	"${RETROFE_DIR}/Source/Execute/AttractMode.cpp"
	"${RETROFE_DIR}/Source/Execute/Launcher.cpp"
	"${RETROFE_DIR}/Source/Graphics/Font.cpp"
	"${RETROFE_DIR}/Source/Graphics/FontCache.cpp"
	"${RETROFE_DIR}/Source/Graphics/LayoutPages.cpp"
	"${RETROFE_DIR}/Source/Graphics/VideoPlayback.cpp"
	"${RETROFE_DIR}/Source/Graphics/PageBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/Page.cpp"
	"${RETROFE_DIR}/Source/Graphics/ThreadPool.cpp"
	"${RETROFE_DIR}/Source/Graphics/ViewInfo.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/Animation.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/AnimationEvents.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/Tween.cpp"
	"${RETROFE_DIR}/Source/Graphics/Animate/TweenSet.cpp"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBindingBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/ComponentItemBinding.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Container.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/FormWidget.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/TextureCache.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/KeyboardWidget.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/WidgetPainter.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Component.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/Image.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ImageBuilder.cpp"
    "${RETROFE_DIR}/Source/Graphics/Component/ReloadableHiscores.cpp"
  	"${RETROFE_DIR}/Source/Graphics/Component/Text.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableMedia.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableText.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ReloadableScrollingText.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/ScrollingList.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoBuilder.cpp"
	"${RETROFE_DIR}/Source/Graphics/Component/VideoComponent.cpp"
	"${RETROFE_DIR}/Source/Menu/Menu.cpp"
	"${RETROFE_DIR}/Source/Sound/Sound.cpp"
	"${RETROFE_DIR}/Source/Utility/Log.cpp"
	"${RETROFE_DIR}/Source/Utility/Utils.cpp"
	"${RETROFE_DIR}/Source/Video/GStreamerVideo.cpp"
	"${RETROFE_DIR}/Source/Video/VideoFactory.cpp"
    "${RETROFE_DIR}/Source/Video/VideoPool.cpp"
	"${RETROFE_DIR}/Source/Main.cpp"
	"${RETROFE_DIR}/Source/Catalog/Download.cpp"
	"${RETROFE_DIR}/Source/Catalog/GitHub.cpp"
	"${RETROFE_DIR}/Source/Catalog/Hash.cpp"
	"${RETROFE_DIR}/Source/Catalog/Http.cpp"
	"${RETROFE_DIR}/Source/Catalog/Index.cpp"
	"${RETROFE_DIR}/Source/Catalog/Installer.cpp"
	"${RETROFE_DIR}/Source/Catalog/Package.cpp"
	"${RETROFE_DIR}/Source/Catalog/SelfUpdate.cpp"
	"${RETROFE_DIR}/Source/Catalog/Service.cpp"
	"${RETROFE_DIR}/Source/Catalog/Sources.cpp"
	"${RETROFE_DIR}/Source/Forms/BuiltinForms.cpp"
	"${RETROFE_DIR}/Source/Forms/Form.cpp"
	"${RETROFE_DIR}/Source/Forms/SettingsFile.cpp"
	"${RETROFE_DIR}/Source/Library/Backups.cpp"
	"${RETROFE_DIR}/Source/Library/ConfFile.cpp"
	"${RETROFE_DIR}/Source/Library/Edits.cpp"
	"${RETROFE_DIR}/Source/Library/Files.cpp"
	"${RETROFE_DIR}/Source/Library/Index.cpp"
	"${RETROFE_DIR}/Source/Library/Indexer.cpp"
	"${RETROFE_DIR}/Source/Library/LineList.cpp"
	"${RETROFE_DIR}/Source/Library/Media.cpp"
	"${RETROFE_DIR}/Source/Library/MetadataOverrides.cpp"
	"${RETROFE_DIR}/Source/Library/Properties.cpp"
	"${RETROFE_DIR}/Source/Library/RetroArch.cpp"
	"${RETROFE_DIR}/Source/Library/Search.cpp"
	"${RETROFE_DIR}/Source/Manager/FileDialog.cpp"
	"${RETROFE_DIR}/Source/Manager/FormView.cpp"
	"${RETROFE_DIR}/Source/Manager/ManagerModel.cpp"
	"${RETROFE_DIR}/Source/Manager/ManagerUI.cpp"
	"${RETROFE_DIR}/Source/Manager/Navigator.cpp"
	"${RETROFE_DIR}/Source/Manager/Providers.cpp"
	"${RETROFE_DIR}/Source/Manager/Setup.cpp"
	"${RETROFE_DIR}/Source/RetroFE.cpp"
	"${RETROFE_DIR}/Source/SDL.cpp"
	"${SQLITE3_ROOT}/sqlite3.c"
)

# The Manage sections (Manager/Games, Manager/Systems...), one folder each, so
# a section can add files without touching this list. Files in a section's
# folder are built into retrofe_tests too, so they mustn't need SDL or
# graphics code; those go in its app/ subfolder, which only RetroFE builds.
# A section's tests.cmake, if any, adds what its files need to the tests
# (list(APPEND RETROFE_TEST_EXTRA_SOURCES ...)).
file(GLOB RETROFE_MANAGER_SECTION_SOURCES CONFIGURE_DEPENDS "${RETROFE_DIR}/Source/Manager/*/*.cpp")
file(GLOB RETROFE_MANAGER_SECTION_APP_SOURCES CONFIGURE_DEPENDS "${RETROFE_DIR}/Source/Manager/*/app/*.cpp")
file(GLOB RETROFE_MANAGER_SECTION_HEADERS CONFIGURE_DEPENDS "${RETROFE_DIR}/Source/Manager/*/*.h" "${RETROFE_DIR}/Source/Manager/*/app/*.h")
list(APPEND RETROFE_SOURCES ${RETROFE_MANAGER_SECTION_SOURCES} ${RETROFE_MANAGER_SECTION_APP_SOURCES})
list(APPEND RETROFE_HEADERS ${RETROFE_MANAGER_SECTION_HEADERS})

# Dear ImGui, used by the management UI
set(IMGUI_BACKEND_SOURCES
	"${IMGUI_ROOT}/backends/imgui_impl_sdl2.cpp"
	"${IMGUI_ROOT}/backends/imgui_impl_sdlrenderer2.cpp"
)
list(APPEND RETROFE_SOURCES
	"${IMGUI_ROOT}/imgui.cpp"
	"${IMGUI_ROOT}/imgui_demo.cpp"
	"${IMGUI_ROOT}/imgui_draw.cpp"
	"${IMGUI_ROOT}/imgui_tables.cpp"
	"${IMGUI_ROOT}/imgui_widgets.cpp"
	"${IMGUI_ROOT}/misc/cpp/imgui_stdlib.cpp"
	${IMGUI_BACKEND_SOURCES}
)
# The backends include <SDL.h> rather than <SDL2/SDL.h>
if(MSVC)
	set_source_files_properties(${IMGUI_BACKEND_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${SDL2_ROOT}/include/SDL2")
endif()

##############################################################
# Theme editor core (Source/ThemeEditor): no SDL, no ImGui windows. A static
# library so retrofe and retrofe_tests share one build of it (and of its
# generated sources).
set(THEME_EDITOR_WIDGETS_DIR "${RETROFE_DIR}/Source/ThemeEditor/widgets")
file(GLOB THEME_EDITOR_WIDGET_FILES CONFIGURE_DEPENDS "${THEME_EDITOR_WIDGETS_DIR}/*.widget")
set(THEME_EDITOR_WIDGETS_SOURCE "${CMAKE_BINARY_DIR}/autogen/ThemeEditorWidgets.cpp")
add_custom_command(
    OUTPUT "${THEME_EDITOR_WIDGETS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -DINPUT_DIR=${THEME_EDITOR_WIDGETS_DIR} -DOUTPUT=${THEME_EDITOR_WIDGETS_SOURCE} -P "${RETROFE_DIR}/CMake/EmbedWidgets.cmake"
    DEPENDS ${THEME_EDITOR_WIDGET_FILES} "${RETROFE_DIR}/CMake/EmbedWidgets.cmake"
    COMMENT "Embedding the theme editor's widgets"
)
set(THEME_EDITOR_FONT_LICENSE_FILE "${RETROFE_DIR}/ThirdParty/fonts/LICENSE-Apache-2.0.txt")
set(THEME_EDITOR_FONT_LICENSE_SOURCE "${CMAKE_BINARY_DIR}/autogen/ThemeEditorFontLicense.cpp")
add_custom_command(
    OUTPUT "${THEME_EDITOR_FONT_LICENSE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${THEME_EDITOR_FONT_LICENSE_FILE} -DOUTPUT=${THEME_EDITOR_FONT_LICENSE_SOURCE} -DSYMBOL=theme_editor_font_license -P "${RETROFE_DIR}/CMake/EmbedFile.cmake"
    DEPENDS "${THEME_EDITOR_FONT_LICENSE_FILE}" "${RETROFE_DIR}/CMake/EmbedFile.cmake"
    COMMENT "Embedding the bundled font's licence"
)
add_library(retrofe_theme_editor STATIC
	"${RETROFE_DIR}/Source/ThemeEditor/EditorCanvas.h"
	"${RETROFE_DIR}/Source/ThemeEditor/EditorNav.h"
	"${RETROFE_DIR}/Source/ThemeEditor/EditorScript.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/EditorScript.h"
	"${RETROFE_DIR}/Source/ThemeEditor/EditorSession.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/EditorSession.h"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutCheck.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutCheck.h"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutPalette.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutPalette.h"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutSession.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutSession.h"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutText.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/LayoutText.h"
	"${RETROFE_DIR}/Source/ThemeEditor/LetterTiles.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/LetterTiles.h"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeExport.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeExport.h"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeGenerate.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeGenerate.h"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeProject.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeProject.h"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeStore.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/ThemeStore.h"
	"${RETROFE_DIR}/Source/ThemeEditor/WidgetDefs.cpp"
	"${RETROFE_DIR}/Source/ThemeEditor/WidgetDefs.h"
	"${THEME_EDITOR_WIDGETS_SOURCE}"
	"${THEME_EDITOR_FONT_LICENSE_SOURCE}"
)
target_link_libraries(retrofe_theme_editor ${ZLIB_LIBRARIES})

##############################################################
# Unit tests for code that doesn't need SDL: cmake -DRETROFE_BUILD_TESTS=ON, then run retrofe_tests
option(RETROFE_BUILD_TESTS "Build the retrofe_tests unit tests" OFF)
if(RETROFE_BUILD_TESTS)
	# Every Tests/*.cpp, so a new test file needs no edit here (see Test::Suite in TestSupport.h)
	file(GLOB RETROFE_TEST_SOURCES CONFIGURE_DEPENDS "${RETROFE_DIR}/Tests/*.cpp")
	set(RETROFE_TEST_EXTRA_SOURCES)
	file(GLOB RETROFE_MANAGER_SECTION_TEST_LISTS CONFIGURE_DEPENDS "${RETROFE_DIR}/Source/Manager/*/tests.cmake")
	foreach(list_file ${RETROFE_MANAGER_SECTION_TEST_LISTS})
		include(${list_file})
	endforeach()
	set(RETROFE_TEST_ALL_SOURCES
		${RETROFE_TEST_SOURCES}
		"${RETROFE_DIR}/Source/Catalog/Download.cpp"
		"${RETROFE_DIR}/Source/Catalog/GitHub.cpp"
		"${RETROFE_DIR}/Source/Catalog/Hash.cpp"
		"${RETROFE_DIR}/Source/Catalog/Http.cpp"
		"${RETROFE_DIR}/Source/Catalog/Index.cpp"
		"${RETROFE_DIR}/Source/Catalog/Installer.cpp"
		"${RETROFE_DIR}/Source/Catalog/Package.cpp"
		"${RETROFE_DIR}/Source/Catalog/SelfUpdate.cpp"
		"${RETROFE_DIR}/Source/Catalog/Service.cpp"
		"${RETROFE_DIR}/Source/Catalog/Sources.cpp"
		"${RETROFE_DIR}/Source/Builtin/Builtin.cpp"
		"${RETROFE_DIR}/Source/Database/Configuration.cpp"
		"${RETROFE_DIR}/Source/Database/GlobalOpts.cpp"
		"${RETROFE_DIR}/Source/Forms/BuiltinForms.cpp"
		"${RETROFE_DIR}/Source/Forms/Form.cpp"
		"${RETROFE_DIR}/Source/Forms/SettingsFile.cpp"
		"${RETROFE_DIR}/Source/Library/Backups.cpp"
		"${RETROFE_DIR}/Source/Library/ConfFile.cpp"
		"${RETROFE_DIR}/Source/Library/Edits.cpp"
		"${RETROFE_DIR}/Source/Library/Files.cpp"
		"${RETROFE_DIR}/Source/Library/Index.cpp"
		"${RETROFE_DIR}/Source/Library/Indexer.cpp"
		"${RETROFE_DIR}/Source/Library/LineList.cpp"
		"${RETROFE_DIR}/Source/Library/Media.cpp"
		"${RETROFE_DIR}/Source/Library/MetadataOverrides.cpp"
		"${RETROFE_DIR}/Source/Library/Properties.cpp"
		"${RETROFE_DIR}/Source/Library/RetroArch.cpp"
		"${RETROFE_DIR}/Source/Library/Search.cpp"
		"${RETROFE_DIR}/Source/Collection/CollectionInfo.cpp"
		"${RETROFE_DIR}/Source/Collection/CollectionInfoBuilder.cpp"
		"${RETROFE_DIR}/Source/Collection/Item.cpp"
		"${RETROFE_DIR}/Source/Collection/VideoCollections.cpp"
		"${RETROFE_DIR}/Source/Database/DB.cpp"
		"${RETROFE_DIR}/Source/Database/MetadataDatabase.cpp"
		"${SQLITE3_ROOT}/sqlite3.c"
		"${RETROFE_DIR}/Source/Graphics/LayoutPages.cpp"
		"${RETROFE_DIR}/Source/Manager/ManagerModel.cpp"
		"${RETROFE_DIR}/Source/Manager/Navigator.cpp"
		"${RETROFE_DIR}/Source/Manager/Setup.cpp"
		${RETROFE_MANAGER_SECTION_SOURCES}
		${RETROFE_TEST_EXTRA_SOURCES}
		"${RETROFE_DIR}/Source/Utility/Log.cpp"
		"${RETROFE_DIR}/Source/Utility/Utils.cpp"
	)
	list(REMOVE_DUPLICATES RETROFE_TEST_ALL_SOURCES)
	add_executable(retrofe_tests ${RETROFE_TEST_ALL_SOURCES})
	add_dependencies(retrofe_tests GenerateVersioningHeader)
	# The theme editor tests read the bundled font and check the layout
	# vocabulary against PageBuilder.cpp and Tween.cpp in the source tree
	target_link_libraries(retrofe_tests retrofe_theme_editor)
	target_compile_definitions(retrofe_tests PRIVATE
		RETROFE_SOURCE_DIR="${RETROFE_DIR}/Source"
		RETROFE_THIRD_PARTY_DIR="${RETROFE_THIRD_PARTY_DIR}")
	# The Catalog tests extract and write zip archives
	if(WIN32)
		target_link_libraries(retrofe_tests "${ZLIB_ROOT}/lib/minizip.lib" ${ZLIB_LIBRARIES} winhttp)
		add_custom_command(TARGET retrofe_tests POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ZLIB_ROOT}/lib/zlib.dll" "$<TARGET_FILE_DIR:retrofe_tests>")
	else()
		# sqlite3 needs libdl and threads; the library index builds on a thread
		target_link_libraries(retrofe_tests ${MINIZIP_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_DL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
	endif()
	enable_testing()
	add_test(NAME forms COMMAND retrofe_tests)
endif()

set(EXECUTABLE_OUTPUT_PATH "${RETROFE_DIR}/Build" CACHE PATH "Build directory" FORCE)
set(LIBRARY_OUTPUT_PATH "${RETROFE_DIR}/Build" CACHE PATH "Build directory" FORCE)

include_directories(${RETROFE_INCLUDE_DIRS})
add_executable(retrofe  ${RETROFE_SOURCES} ${RETROFE_HEADERS})
add_dependencies(retrofe GenerateVersioningHeader)
target_link_libraries(retrofe retrofe_theme_editor)

# macOS links what its branch above found, frameworks or Homebrew's
if(NOT APPLE)
find_library(WEBPDEMUX_LIBRARY
    NAMES webpdemux
    HINTS /opt/homebrew/lib /usr/lib /usr/local/lib
)

find_library(WEBP_LIBRARY
    NAMES webp
    HINTS /opt/homebrew/lib /usr/lib /usr/local/lib
)

find_library(MINIZIP_LIBRARY
    NAMES minizip
    HINTS /opt/homebrew/lib /usr/lib /usr/local/lib
)

if(WEBPDEMUX_LIBRARY)
    message(STATUS "Found webpdemux: ${WEBPDEMUX_LIBRARY}")
    set(HAVE_WEBPDEMUX TRUE)
else()
    message(WARNING "webpdemux not found; assuming it's bundled in libwebp")
    set(HAVE_WEBPDEMUX FALSE)
endif()

if(WEBP_LIBRARY)
    message(STATUS "Found webp: ${WEBP_LIBRARY}")
    set(HAVE_WEBP TRUE)
else()
    message(WARNING "webp not found; assuming it's bundled in libwebp")
    set(HAVE_WEBP FALSE)
endif()

if(MINIZIP_LIBRARY)
    message(STATUS "Found minizip: ${MINIZIP_LIBRARY}")
    set(HAVE_MINIZIP TRUE)
else()
    message(WARNING "minizip not found")
    set(HAVE_MINIZIP FALSE)
endif()

if(HAVE_WEBPDEMUX)
    target_link_libraries(retrofe ${WEBPDEMUX_LIBRARY})
endif()

if(HAVE_WEBP)
    target_link_libraries(retrofe ${WEBP_LIBRARY})
endif()

if(HAVE_MINIZIP)
    target_link_libraries(retrofe ${MINIZIP_LIBRARY})
endif()
endif()

target_link_libraries(retrofe ${RETROFE_LIBRARIES})

if(APPLE AND RETROFE_MACOS_BUNDLE)
    # RetroFE.app, as the Xcode project used to make it: Configuration finds the
    # RetroFE folder by the executable's path ending in /RetroFE.app/Contents/MacOS
    file(READ "${RETROFE_DIR}/CMake/Versioning.cmake" RETROFE_VERSIONING_SCRIPT)
    string(REGEX MATCH "RETROFE_VERSION_MAJOR \"([0-9]+)\"" _ "${RETROFE_VERSIONING_SCRIPT}")
    set(RETROFE_BUNDLE_VERSION "${CMAKE_MATCH_1}")
    string(REGEX MATCH "RETROFE_VERSION_MINOR \"([0-9]+)\"" _ "${RETROFE_VERSIONING_SCRIPT}")
    set(RETROFE_BUNDLE_VERSION "${RETROFE_BUNDLE_VERSION}.${CMAKE_MATCH_1}")
    if(DEFINED RETROFE_RELEASE_VERSION AND NOT "${RETROFE_RELEASE_VERSION}" STREQUAL "")
        set(RETROFE_BUNDLE_VERSION "${RETROFE_RELEASE_VERSION}")
    endif()
    # build-macos.yml builds for macOS 11 and later (-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(RETROFE_BUNDLE_MINIMUM_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(RETROFE_BUNDLE_MINIMUM_MACOS "11.0")
    endif()

    set(RETROFE_BUNDLE_ICON "${RETROFE_DIR}/../Package/Environment/Common/RetroFE.png")
    target_sources(retrofe PRIVATE "${RETROFE_BUNDLE_ICON}")
    set_source_files_properties("${RETROFE_BUNDLE_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(retrofe PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "RetroFE"
        MACOSX_BUNDLE_INFO_PLIST "${RETROFE_DIR}/CMake/MacOSXBundleInfo.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.coinops.RetroFE"
        MACOSX_BUNDLE_BUNDLE_NAME "RetroFE"
        MACOSX_BUNDLE_BUNDLE_VERSION "${RETROFE_BUNDLE_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "RetroFE.png"
        # The frameworks are found in the bundle, never where it was built
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks;@executable_path/../Frameworks/GStreamer.framework/Libraries"
    )
    # Copy the frameworks and libraries in (without their headers, or GStreamer's static
    # libraries), then sign the bundle ad hoc, as Xcode did
    add_custom_command(TARGET retrofe POST_BUILD
        COMMAND /bin/sh "${RETROFE_DIR}/CMake/EmbedMacFrameworks.sh"
            "$<TARGET_BUNDLE_DIR:retrofe>" "${MAC_FRAMEWORKS_DIR}" "${RETROFE_DIR}/Source/retrofe.entitlements"
            ${RETROFE_MACOS_EMBEDDED}
        COMMENT "Embedding the frameworks in RetroFE.app"
        VERBATIM
    )
endif()

if(MSVC)
    set(CMAKE_DEBUG_POSTFIX "d")
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE)
    add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)
    # Change /MT to /MD for dynamic runtime linking
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /WX- /MD")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP /WX- /MD")
    set_target_properties(retrofe PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
else()
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0 -Wall -ggdb")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -Wall -ggdb")
    # The same warnings on every architecture
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-reorder")
    # x86-64 tuning, unless this is a universal (arm64 and x86_64) macOS build
    list(LENGTH CMAKE_OSX_ARCHITECTURES RETROFE_OSX_ARCHITECTURE_COUNT)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" AND RETROFE_OSX_ARCHITECTURE_COUNT LESS 2)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -march=x86-64-v2 -msse4.2 -mavx -funroll-loops")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=x86-64-v2 -msse4.2 -mavx -funroll-loops")
    endif()
endif()
