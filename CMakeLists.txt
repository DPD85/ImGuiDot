cmake_minimum_required(VERSION 3.25)

# ----- -----

project("ImGui-Dot" LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
elseif(CMAKE_CXX_STANDARD LESS 11 OR CMAKE_CXX_STANDARD EQUAL 98)
    message(FATAL_ERROR "[${PROJECT_NAME}] The C++${CMAKE_CXX_STANDARD} is to low, at least the C++11 is required.")
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----- -----

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ----- -----

include(CMake/CPM.cmake)

# If the cache directory where CPM download the dependencies is not specified then set a default to avoid errors due to
# the apply of the patches (see https://github.com/cpm-cmake/CPM.cmake/issues/577).
if(NOT CPM_SOURCE_CACHE)
    set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_BINARY_DIR}/.cpm-cache" CACHE PATH "Directory to download CPM dependencies" FORCE)
endif()

# ----- -----
# If you provide ImGui by yourself then set BUILD_IMGUI to OFF and the IMGUI_TARGET to the name of the library to link.

option(BUILD_IMGUI "Build the ImGui static library internally." ON)
set(IMGUI_TARGET "ImGui" CACHE STRING "Name of the ImGui target to link against.")

# ----- Graphviz -----

set(CMAKE_FOLDER_OLD "${CMAKE_FOLDER}")
set(CMAKE_FOLDER "${CMAKE_FOLDER}/Graphviz")

CPMAddPackage(
    NAME Graphviz
    VERSION 15.1.0
    GIT_TAG 15.1.0
    GITLAB_REPOSITORY graphviz/graphviz
    PATCHES Graphviz-FlexBison.patch
    EXCLUDE_FROM_ALL ON
    SYSTEM ON
    OPTIONS
        # Graphviz need the GNU C language extensions to build properly on Linux.
        "CMAKE_C_EXTENSIONS ON"
        "BUILD_SHARED_LIBS 0"
        "BUILD_TESTING 0"
        "ENABLE_D OFF"
        "ENABLE_GO OFF"
        "ENABLE_GUILE OFF"
        "ENABLE_JAVA OFF"
        "ENABLE_JAVASCRIPT OFF"
        "ENABLE_LTDL OFF"
        "ENABLE_LUA OFF"
        "ENABLE_PERL OFF"
        "ENABLE_PHP OFF"
        "ENABLE_PYTHON OFF"
        "ENABLE_R OFF"
        "ENABLE_RUBY OFF"
        "ENABLE_SHARP OFF"
        "ENABLE_SWIG OFF"
        "ENABLE_TCL OFF"
        "WITH_EXPAT OFF"
        "WITH_GDK OFF"
        "WITH_GHOSTSCRIPT OFF"
        "WITH_GTK OFF"
        "WITH_GVEDIT OFF"
        "WITH_POPPLER OFF"
        "WITH_QUARTZ OFF"
        "WITH_RSVG OFF"
        "WITH_SMYRNA OFF"
        "WITH_WEBP OFF"
        "WITH_X OFF"
        "WITH_ZLIB OFF"
        "CMAKE_DISABLE_FIND_PACKAGE_AA ON"
        "CMAKE_DISABLE_FIND_PACKAGE_ANN ON"
        "CMAKE_DISABLE_FIND_PACKAGE_CAIRO ON"
        "CMAKE_DISABLE_FIND_PACKAGE_DevIL ON"
        "CMAKE_DISABLE_FIND_PACKAGE_GD ON"
        "CMAKE_DISABLE_FIND_PACKAGE_GTS ON"
        "CMAKE_DISABLE_FIND_PACKAGE_PANGOCAIRO ON"
        "install_win_dependency_dlls 0"
)

set(CMAKE_FOLDER "${CMAKE_FOLDER_OLD}")

# ----- ImGui -----

if(BUILD_IMGUI)
    CPMAddPackage(
        NAME ImGui
        VERSION 1.92.6
        GITHUB_REPOSITORY ocornut/imgui
        DOWNLOAD_ONLY ON
    )

    add_library(ImGui STATIC)

    target_sources(ImGui
        PRIVATE
            ${ImGui_SOURCE_DIR}/imgui.cpp
            ${ImGui_SOURCE_DIR}/imgui.h
            ${ImGui_SOURCE_DIR}/imgui_demo.cpp
            ${ImGui_SOURCE_DIR}/imgui_draw.cpp
            ${ImGui_SOURCE_DIR}/imgui_internal.h
            ${ImGui_SOURCE_DIR}/imgui_tables.cpp
            ${ImGui_SOURCE_DIR}/imgui_widgets.cpp
            ${ImGui_SOURCE_DIR}/imstb_rectpack.h
            ${ImGui_SOURCE_DIR}/imstb_textedit.h
            ${ImGui_SOURCE_DIR}/imstb_truetype.h

            ${CMAKE_CURRENT_SOURCE_DIR}/ImGui-Config.h
    )

    target_compile_definitions(ImGui
        PUBLIC
            IMGUI_USER_CONFIG=<ImGui-Config.h>
    )

    target_include_directories(ImGui
        PUBLIC
            ${ImGui_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    set_target_properties(ImGui
        PROPERTIES
            EXCLUDE_FROM_ALL ON
            SYSTEM ON
    )

    if(MSVC)
        target_compile_options(ImGui PRIVATE "/utf-8")
    endif()
endif()

# ----- -----

add_library(ImGui-Dot STATIC)
add_library(ImGui-Dot::ImGui-Dot ALIAS ImGui-Dot)

target_sources(ImGui-Dot
    PRIVATE
        src/ImGuiDot.cpp
        src/ImGuiDot.h
        src/ImGuiDot_Structs.h
        LICENSE
        Readme.md
)

target_include_directories(ImGui-Dot
    PRIVATE
        ${Graphviz_BINARY_DIR}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ImGui-Dot
    PUBLIC
        ${IMGUI_TARGET}
        gvc
        cgraph
        gvplugin_core
        gvplugin_dot_layout
)

if(MSVC)
    target_compile_options(ImGui-Dot PRIVATE "/utf-8")
endif()
