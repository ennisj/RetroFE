#include "TestSupport.h"
#include "../Source/Graphics/ScreenFontSize.h"

static Test::Suite screenFontSizeSuite("screen-font-size", [] {
    Test::run("font atlas size follows downscaling", [] {
        Test::check(screenFontSize(35, 1.0f) == 35, "scale 1 leaves size unchanged");
        Test::check(screenFontSize(35, 2.0f) == 35, "upscaling leaves size unchanged");
        Test::check(screenFontSize(35, 0.5f) == 18, "half scale rounds to nearest pixel");
        Test::check(screenFontSize(34, 0.5f) == 17, "half scale halves an even size");
        Test::check(screenFontSize(35, 0.01f) == 8, "small sizes clamp to eight");
        Test::check(screenFontSize(6, 0.5f) == 6, "small configured sizes are not enlarged");
        Test::check(screenFontSize(35, 0.0f) == 35, "zero scale leaves size unchanged");
        Test::check(screenFontSize(35, -1.0f) == 35, "negative scale leaves size unchanged");
    });
});
