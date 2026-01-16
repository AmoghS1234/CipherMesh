#include "theme_service.hpp"
#include <algorithm>

namespace CipherMesh {
namespace Services {

static const std::vector<Theme> AVAILABLE_THEMES = {
    {
        "professional",
        "Professional Slate",
        true,  // isDark
        {86, 156, 214},    // primaryColor (blue)
        {24, 24, 24},      // backgroundColor (dark grey)
        {228, 228, 228},   // textColor (light grey)
        {38, 79, 120}      // selectedColor (dark blue)
    },
    {
        "light",
        "Modern Light",
        false,  // isDark
        {0, 120, 212},     // primaryColor (blue)
        {250, 250, 250},   // backgroundColor (off-white)
        {26, 26, 26},      // textColor (almost black)
        {0, 120, 212}      // selectedColor (blue)
    },
    {
        "ocean",
        "Ocean Dark",
        true,  // isDark
        {14, 165, 233},    // primaryColor (cyan)
        {10, 25, 41},      // backgroundColor (very dark blue)
        {178, 186, 194},   // textColor (light blue-grey)
        {12, 74, 110}      // selectedColor (dark cyan)
    },
    {
        "warm",
        "Warm Light",
        false,  // isDark
        {249, 115, 22},    // primaryColor (orange)
        {254, 249, 243},   // backgroundColor (warm off-white)
        {41, 37, 36},      // textColor (dark brown)
        {253, 215, 170}    // selectedColor (light orange)
    },
    {
        "vibrant",
        "Vibrant Colors",
        false,  // isDark
        {139, 92, 246},    // primaryColor (purple)
        {255, 248, 240},   // backgroundColor (very light warm)
        {31, 41, 55},      // textColor (dark grey)
        {236, 72, 153}     // selectedColor (pink)
    }
};

std::vector<Theme> ThemeService::getAllThemes() {
    return AVAILABLE_THEMES;
}

Theme ThemeService::getThemeById(const std::string& id) {
    for (const auto& theme : AVAILABLE_THEMES) {
        if (theme.id == id) {
            return theme;
        }
    }
    return getDefaultTheme();
}

Theme ThemeService::getDefaultTheme() {
    return AVAILABLE_THEMES[0];  // "professional"
}

bool ThemeService::isValidThemeId(const std::string& id) {
    for (const auto& theme : AVAILABLE_THEMES) {
        if (theme.id == id) {
            return true;
        }
    }
    return false;
}

} // namespace Services
} // namespace CipherMesh
