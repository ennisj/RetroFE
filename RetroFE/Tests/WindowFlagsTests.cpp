#include "TestSupport.h"
#include "../Source/WindowFlags.h"

static Test::Suite windowFlagsSuite("window-flags", [] {
    Test::run("OpenGL window flag follows the renderer on Windows", [] {
        for (const std::string& driver : { "direct3d11", "direct3d", "direct3d12", "software", "" })
            Test::check(!windowNeedsOpenGL(driver, true), driver + " does not need OpenGL");
        for (const std::string& driver : { "opengl", "OpenGL", "opengles", "opengles2" })
            Test::check(windowNeedsOpenGL(driver, true), driver + " needs OpenGL");
        Test::check(windowNeedsOpenGL("direct3d11", false), "non-Windows always needs OpenGL");
    });
});
