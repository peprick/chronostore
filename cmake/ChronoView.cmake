include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        chronostore_glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        chronostore_imgui_source
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.8
        GIT_SHALLOW TRUE
)

FetchContent_Declare(
        chronostore_implot_source
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG v1.0
        GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
        chronostore_glfw
        chronostore_imgui_source
        chronostore_implot_source
)

find_package(OpenGL REQUIRED)

add_library(
        chronostore_gui_dependencies
        STATIC
        ${chronostore_imgui_source_SOURCE_DIR}/imgui.cpp
        ${chronostore_imgui_source_SOURCE_DIR}/imgui_draw.cpp
        ${chronostore_imgui_source_SOURCE_DIR}/imgui_tables.cpp
        ${chronostore_imgui_source_SOURCE_DIR}/imgui_widgets.cpp
        ${chronostore_imgui_source_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${chronostore_imgui_source_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${chronostore_implot_source_SOURCE_DIR}/implot.cpp
        ${chronostore_implot_source_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(
        chronostore_gui_dependencies
        PUBLIC
        ${chronostore_imgui_source_SOURCE_DIR}
        ${chronostore_imgui_source_SOURCE_DIR}/backends
        ${chronostore_implot_source_SOURCE_DIR}
)

target_link_libraries(
        chronostore_gui_dependencies
        PUBLIC
        glfw
        OpenGL::GL
)

target_compile_features(chronostore_gui_dependencies PUBLIC cxx_std_20)
set_target_properties(chronostore_gui_dependencies PROPERTIES CXX_EXTENSIONS OFF)

add_executable(chronoview tools/chronoview/main.cpp)
target_link_libraries(
        chronoview
        PRIVATE
        ChronoStore::chronostore
        chronostore_gui_dependencies
)

chronostore_enable_warnings(chronoview)
chronostore_enable_sanitizers(chronoview)
set_target_properties(chronoview PROPERTIES CXX_EXTENSIONS OFF)
