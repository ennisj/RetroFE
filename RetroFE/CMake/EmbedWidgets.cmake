cmake_minimum_required(VERSION 3.12)

# Compiles the theme editor's stock widget files (*.widget in a folder) into a
# C++ source file, so RetroFE has them with no files beside it.
#
#   cmake -DINPUT_DIR=<folder> -DOUTPUT=<file.cpp> -P EmbedWidgets.cmake
#
# The generated file defines (global namespace):
#   extern const char* const          themeEditorWidgetNames[];
#   extern const unsigned char* const themeEditorWidgetData[];
#   extern const std::size_t          themeEditorWidgetSizes[];
#   extern const std::size_t          themeEditorWidgetCount;

if(NOT INPUT_DIR OR NOT OUTPUT)
    message(FATAL_ERROR "EmbedWidgets.cmake needs INPUT_DIR and OUTPUT")
endif()

file(GLOB WIDGET_FILES "${INPUT_DIR}/*.widget")
list(SORT WIDGET_FILES)
list(LENGTH WIDGET_FILES WIDGET_COUNT)
if(WIDGET_COUNT EQUAL 0)
    message(FATAL_ERROR "EmbedWidgets.cmake: no .widget files in ${INPUT_DIR}")
endif()

set(ARRAYS "")
set(NAMES "")
set(DATA "")
set(SIZES "")
set(INDEX 0)
foreach(WIDGET_FILE ${WIDGET_FILES})
    get_filename_component(WIDGET_NAME "${WIDGET_FILE}" NAME)
    file(READ "${WIDGET_FILE}" HEX_CONTENT HEX)
    # Carriage returns are dropped so a CRLF checkout embeds the same bytes
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\1;" HEX_BYTES "${HEX_CONTENT}")
    list(REMOVE_ITEM HEX_BYTES "0d")
    string(REPLACE ";" "" HEX_CONTENT "${HEX_BYTES}")
    string(LENGTH "${HEX_CONTENT}" HEX_LENGTH)
    math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")
    string(REGEX REPLACE "([0-9a-f]{32})" "\\1\n" HEX_CONTENT "${HEX_CONTENT}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")
    string(APPEND ARRAYS "static const unsigned char widget${INDEX}[] = {\n${BYTES}\n};\n")
    string(APPEND NAMES "    \"${WIDGET_NAME}\",\n")
    string(APPEND DATA "    widget${INDEX},\n")
    string(APPEND SIZES "    ${BYTE_COUNT},\n")
    math(EXPR INDEX "${INDEX} + 1")
endforeach()

set(CONTENT "// Auto-generated from Source/ThemeEditor/widgets by EmbedWidgets.cmake - do not edit\n#include <cstddef>\n\n")
string(APPEND CONTENT "${ARRAYS}\n")
string(APPEND CONTENT "extern const char* const themeEditorWidgetNames[] = {\n${NAMES}};\n")
string(APPEND CONTENT "extern const unsigned char* const themeEditorWidgetData[] = {\n${DATA}};\n")
string(APPEND CONTENT "extern const std::size_t themeEditorWidgetSizes[] = {\n${SIZES}};\n")
string(APPEND CONTENT "extern const std::size_t themeEditorWidgetCount = ${WIDGET_COUNT};\n")
file(WRITE "${OUTPUT}" "${CONTENT}")
