#pragma once

#include <imgui.h>
#include <string>
#include <string_view>

using Agraph_t = struct Agraph_s;

namespace ImGuiDot
{
    /// @brief Initialize the library.
    /// @retval True In case of success.
    /// @retval False In case of failure.
    /// @remark This function must be call one time (for example at initialization time) before any other function of
    /// the library.
    bool Initialize();

    /// @brief Clean up the global resources used.
    /// @remark This function must be call one time before the end of the program when the library is not more
    ///         necessary.
    void CleanUp();

    // ----- -----
    // Style.

    /// @brief Special colour value: the colour follows the ImGui style currently in use, see GetStyleColourVec4().
#define IMGUIDOT_AUTO_COLOUR ImVec4(0.0f, 0.0f, 0.0f, -1.0f)

    /// @brief The items of a diagram with a colour in the style.
    enum StyleColour_ : int
    {
        StyleColour_Label,             ///< Labels of the nodes and of the arcs. Auto: ImGuiCol_Text.
        StyleColour_ShapeBorder,       ///< Border of the shapes. Auto: ImGuiCol_Border.
        StyleColour_Arc,               ///< Arcs and their arrowheads. Auto: ImGuiCol_Border.
        StyleColour_ShapeBackground,   ///< Background of the shapes. Default: transparent.
        StyleColour_DiagramBackground, ///< Background of the diagram. Default: transparent.
        StyleColour_DiagramBorder,     ///< Border around the diagram. Default: transparent.
        StyleColour_Count
    };
    using StyleColour = int;

    /// @brief The style of the diagrams, modelled on ImGuiStyle.
    /// @remark The colours set in the DOT source code (color, fillcolor, fontcolor, bgcolor) take precedence: the style
    ///         gives the colours of the items that the source code leaves unspecified.
    struct Style
    {
        /// @brief The colours of the items, use IMGUIDOT_AUTO_COLOUR to follow the ImGui style.
        ImVec4 colours[StyleColour_Count];

        Style();
    };

    /// @brief Get the style in use, modify it to change the look of all the diagrams drawn after.
    Style &GetStyle();

    /// @brief Get a colour of the style, with IMGUIDOT_AUTO_COLOUR resolved from the ImGui style in use.
    /// @param index The item.
    /// @return The colour, without the global alpha of the ImGui style applied.
    ImVec4 GetStyleColourVec4(StyleColour index);

    /// @brief Get a colour of the style, with IMGUIDOT_AUTO_COLOUR resolved from the ImGui style in use.
    /// @param index The item.
    /// @return The colour, with the global alpha of the ImGui style applied (as ImGui::GetColorU32() does).
    ImU32 GetStyleColourU32(StyleColour index);

    /// @brief Temporarily change a colour of the style, restore it with PopStyleColour().
    /// @param index The item.
    /// @param colour The new colour.
    void PushStyleColour(StyleColour index, ImU32 colour);

    /// @copydoc void PushStyleColour(StyleColour, ImU32)
    void PushStyleColour(StyleColour index, const ImVec4 &colour);

    /// @brief Restore the colours changed by the last calls to PushStyleColour().
    /// @param count The number of colours to restore.
    void PopStyleColour(int count = 1);

    // ----- -----
    // Full immediate mode functions.

    /// @brief Draw a diagram from the provided source code in DOT language.
    /// @param code The pointer to the begin of the buffer with the source code.
    /// @param endCode The pointer to the end of the buffer with the source code, if null then the string pointed by
    ///                code is assumed to be null terminated.
    /// @param zoom The zoom of the drawn diagram.
    /// @param pivot Centre on this point, for example use (0.5, 0.5) to centre vertically and horizontally. The default
    ///              value mean top-left alignment.
    void Diagram(
        const char *code, const char *endCode = nullptr, float zoom = 1.0f, const ImVec2 &pivot = ImVec2(0, 0));

    /// @brief Draw a diagram from the provided source code in DOT language.
    /// @param code The string with the source code.
    /// @param zoom The zoom of the drawn diagram.
    /// @param pivot Centre on this point, for example use (0.5, 0.5) to centre vertically and horizontally. The default
    ///              value mean top-left alignment.
    void Diagram(const std::string &code, float zoom = 1.0f, const ImVec2 &pivot = ImVec2(0, 0));

    /// @copydoc void Diagram(const std::string&, const float)
    void Diagram(const std::string_view &code, float zoom = 1.0f, const ImVec2 &pivot = ImVec2(0, 0));

    // ----- -----

    // Partial immediate mode: parsing and layout of the diagram are cached and reused.
    // Use the Update() functions to update the diagram state when it change.
    // Use CleanUp(diagram) function to release the used resources.
    // Use the Draw(diagram) function to draw the diagram from its state.

    /// @brief Structure of the memory where to store the internal stare of a diagram.
    struct DiagramState
    {
        Agraph_t *graph;

        DiagramState(): graph(nullptr) {}
    };

    /// @brief Draw a diagram from the provided source code in DOT language.
    /// @param diagram The state of the diagram to draw.
    /// @param zoom The zoom of the drawn diagram.
    /// @param pivot Centre on this point, for example use (0.5, 0.5) to centre vertically and horizontally. The default
    ///              value mean top-left alignment.
    void Draw(const DiagramState &diagram, float zoom = 1.0f, const ImVec2 &pivot = ImVec2(0, 0));

    /// @brief Create or update the state of a diagram preparing it to be draw later.
    /// @param diagram The state to be create or updated.
    /// @param code The pointer to the begin of the buffer with the source code.
    /// @param endCode The pointer to the end of the buffer with the source code, if null then the string pointed by
    ///                code is assumed to be null terminated.
    void Update(DiagramState &diagram, const char *code, const char *endCode = nullptr);

    /// @brief Create or update the state of a diagram preparing it to be draw later.
    /// @param diagram The state to be create or updated.
    /// @param code The string with the source code.
    void Update(DiagramState &diagram, const std::string &code);

    /// @copydoc void Update(DiagramState&, const std::string&)
    void Update(DiagramState &diagram, const std::string_view &code);

    /// @brief Clean up the state of a diagram freeing all the used resources.
    /// @param diagram The state to be cleaned up.
    void CleanUp(DiagramState &diagram);
}
