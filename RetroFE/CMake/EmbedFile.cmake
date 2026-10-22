cmake_minimum_required(VERSION 3.12)

# Turns a binary file into a C++ source file holding its bytes, so it can be
# compiled into the executable.
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P EmbedFile.cmake
#
# The generated file defines:
#   extern const unsigned char <SYMBOL>[];
#   extern const std::size_t   <SYMBOL>_size;

if(NOT INPUT OR NOT OUTPUT OR NOT SYMBOL)
    message(FATAL_ERROR "EmbedFile.cmake needs INPUT, OUTPUT and SYMBOL")
endif()

file(READ "${INPUT}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")

# Break into lines of 16 bytes, then turn each byte pair into 0xNN,
string(REGEX REPLACE "([0-9a-f]{32})" "\\1\n" HEX_CONTENT "${HEX_CONTENT}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")

get_filename_component(INPUT_NAME "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
    "// Auto-generated from ${INPUT_NAME} by EmbedFile.cmake - do not edit\n"
    "#include <cstddef>\n\n"
    "extern const unsigned char ${SYMBOL}[] = {\n${BYTES}\n};\n"
    "extern const std::size_t ${SYMBOL}_size = ${BYTE_COUNT};\n")
