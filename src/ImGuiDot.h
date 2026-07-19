#pragma once

#include <string>
#include <version>

typedef struct Agraph_s Agraph_t;

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
    // Full immediate mode functions.

    /// @brief Draw a diagram from the provided source code in DOT language.
    /// @param code The pointer to the begin of the buffer with the source code.
    /// @param endCode The pointer to the end of the buffer with the source code, if null then the string pointed by
    ///                code is assumed to be null terminated.
    /// @param zoom The zoom of the drawn diagram.
    void Diagram(const char *code, const char *endCode = nullptr, float zoom = 1.0f);

    /// @brief Draw a diagram from the provided source code in DOT language.
    /// @param code The string with the source code.
    /// @param zoom The zoom of the drawn diagram.
    void Diagram(const std::string &code, float zoom = 1.0f);

#if __cpp_lib_string_view
    /// @copydoc void Diagram(const std::string&, const float)
    void Diagram(const std::string_view &code, float zoom = 1.0f);
#endif

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
    void Draw(const DiagramState &diagram, float zoom);

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

#if __cpp_lib_string_view
    /// @copydoc void Update(DiagramState&, const std::string&)
    void Update(DiagramState &diagram, const std::string_view &code);
#endif

    /// @brief Clean up the state of a diagram freeing all the used resources.
    /// @param diagram The state to be cleaned up.
    void CleanUp(DiagramState &diagram);
}
