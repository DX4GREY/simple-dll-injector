cmake_minimum_required(VERSION 3.20)
project(GuiInjector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Architecture Detection: x64 or x86
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_NAME "x64")
    set(ARCH_BIT "64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH_NAME "x86")
    set(ARCH_BIT "32")
else()
    message(FATAL_ERROR "Unsupported architecture: pointer size is ${CMAKE_SIZEOF_VOID_P}")
endif()

message(STATUS "Configuring GuiInjector for ${ARCH_NAME} (${ARCH_BIT}-bit)")

add_executable(injector WIN32
    src/main.cpp
    src/core/injector.cpp
    src/core/auto_inject.cpp
    src/utils/pe_parser.cpp
    src/utils/process_utils.cpp
    src/config/config.cpp
    src/ui/main_window.cpp
)

target_include_directories(injector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(injector PRIVATE
    UNICODE
    _UNICODE
    INJECTOR_ARCH_NAME="${ARCH_NAME}"
    INJECTOR_ARCH_BIT=${ARCH_BIT}
)

# Produce named binary: injector-x64.exe or injector-x86.exe
set_target_properties(injector PROPERTIES
    OUTPUT_NAME "injector-${ARCH_NAME}"
)

# Link required standard Win32 libraries
target_link_libraries(injector PRIVATE comctl32 comdlg32 shell32)

if(MSVC)
    target_link_options(injector PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
