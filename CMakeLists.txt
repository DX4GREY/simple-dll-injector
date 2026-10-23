cmake_minimum_required(VERSION 3.20)
project(GuiInjector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Target arsitektur harus 64-bit (x64)!")
endif()

# WIN32 menyetel subsystem ke Windows GUI (tidak memunculkan jendela console hitam)
add_executable(injector WIN32 injector.cpp)

target_compile_definitions(injector PRIVATE UNICODE _UNICODE)

# Link library standar Win32
target_link_libraries(injector PRIVATE comctl32)

# Link library standar Win32 (tambahkan comdlg32)
target_link_libraries(injector PRIVATE comctl32 comdlg32)

if(MSVC)
    target_link_options(injector PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()
