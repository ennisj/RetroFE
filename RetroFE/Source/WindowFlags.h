#pragma once

#include <cctype>
#include <string>

inline bool windowNeedsOpenGL(const std::string& renderDriver, bool windows)
{
    if (!windows)
        return true;
    const std::string prefix = "opengl";
    if (renderDriver.size() < prefix.size())
        return false;
    for (std::string::size_type i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(renderDriver[i])) != prefix[i])
            return false;
    return true;
}
