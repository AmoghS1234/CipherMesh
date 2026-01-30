#pragma once

#include <string>
#include <vector>

namespace CipherMesh {
namespace Services {

/**
 * Platform-independent theme definition.
 * Contains theme metadata that can be adapted for Qt (desktop) or Material Design (Android).
 */
struct Theme {
    std::string id;          // e.g., "professional", "light", "ocean"
    std::string name;        // e.g., "Professional Slate", "Modern Light"
    bool isDark;             // true for dark themes, false for light themes
    
    // Primary colors (RGB 0-255)
    struct {
        int r, g, b;
    } primaryColor;          // Main accent color
    
    struct {
        int r, g, b;
    } backgroundColor;       // Main background color
    
    struct {
        int r, g, b;
    } textColor;             // Primary text color
    
    struct {
        int r, g, b;
    } selectedColor;         // Selected item background
};

/**
 * Platform-independent theme service.
 * Provides theme definitions that can be adapted for each platform.
 */
class ThemeService {
public:
    /**
     * Get all available themes.
     */
    static std::vector<Theme> getAllThemes();
    
    /**
     * Get a theme by ID.
     * @param id Theme identifier (e.g., "professional", "light")
     * @return Theme definition, or default theme if not found
     */
    static Theme getThemeById(const std::string& id);
    
    /**
     * Get the default theme.
     */
    static Theme getDefaultTheme();
    
    /**
     * Check if a theme ID is valid.
     */
    static bool isValidThemeId(const std::string& id);
};

} // namespace Services
} // namespace CipherMesh
