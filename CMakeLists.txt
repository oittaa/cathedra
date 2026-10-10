cmake_minimum_required(VERSION 3.16)
project(cathedra C)

# Two targets from the same source and the same embedded SPIR-V:
#   cathedra      - the release intro (no command line, no dev tools). Size-optimised; the
#                   64k entry itself is produced by build_linux.sh (LZMA dropper).
#   cathedra_dev  - CATHEDRA_DEV: switches, self-test, sweep, logs, PNG shots, YouTube export.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Vulkan is loaded at runtime (vulkan-1.dll / libvulkan.so.1); only the vendored headers are needed.
add_executable(cathedra src/main.c)
add_executable(cathedra_dev src/main.c)
target_compile_definitions(cathedra_dev PRIVATE CATHEDRA_DEV)

foreach(t cathedra cathedra_dev)
  target_include_directories(${t} PRIVATE src src/third_party)
  if (WIN32)
    target_link_libraries(${t} PRIVATE gdi32 winmm user32)
    if (MSVC)
      target_link_options(${t} PRIVATE /ENTRY:mainCRTStartup)
    endif()
  else()
    find_package(X11 REQUIRED)
    find_library(ALSA_LIB asound REQUIRED)
    target_link_libraries(${t} PRIVATE X11::X11 ${ALSA_LIB} m pthread ${CMAKE_DL_LIBS})
  endif()
endforeach()

# the release build is a GUI app on Windows; the dev build keeps its console for logs
if (WIN32)
  set_target_properties(cathedra PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if (MSVC)
  target_compile_options(cathedra PRIVATE /O1 /Oi /fp:fast /GS-)
  target_compile_options(cathedra_dev PRIVATE /O2 /fp:fast)
else()
  target_compile_options(cathedra PRIVATE -Oz -fno-schedule-insns2 -fno-peephole2 -ffast-math -fno-asynchronous-unwind-tables -fno-ident -ffunction-sections -fdata-sections)
  target_link_options(cathedra PRIVATE -Wl,--gc-sections -s)
  target_compile_options(cathedra_dev PRIVATE -O2 -ffast-math)
endif()

install(TARGETS cathedra cathedra_dev RUNTIME DESTINATION bin)
