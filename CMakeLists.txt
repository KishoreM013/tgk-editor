cmake_minimum_required(VERSION 3.16)
project(cppide CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(APP_SOURCES
    ide.h ide.cpp
    codeeditor.h codeeditor.cpp
    highlighter.h highlighter.cpp
    clangdclient.h clangdclient.cpp
    languages.h languages.cpp
    welcomepage.h welcomepage.cpp
)

add_executable(cppide main.cpp ${APP_SOURCES})
target_link_libraries(cppide PRIVATE Qt6::Widgets)
set_target_properties(cppide PROPERTIES OUTPUT_NAME tgkeditor)

# Headless test of the clangd LSP client (no GUI)
add_executable(clangd_test
    test_clangd.cpp
    clangdclient.h clangdclient.cpp
)
target_link_libraries(clangd_test PRIVATE Qt6::Core)

# Headless test of multi-tab handling
add_executable(tabs_test tabs_test.cpp ${APP_SOURCES})
target_link_libraries(tabs_test PRIVATE Qt6::Widgets)

# Headless icon generator (renders the app icon PNG)
add_executable(icon_gen icon_gen.cpp)
target_link_libraries(icon_gen PRIVATE Qt6::Gui)
