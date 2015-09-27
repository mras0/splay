cmake_minimum_required(VERSION 3.3)
project(splay)

if (NOT MSVC)
    message(FATAL_ERROR "MSVC required")
endif()


set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported build configurations" FORCE)

add_definitions("/W4")
#add_definitions("/wd4267") # C4267: 'argument': conversion from 'X' to 'Y', possible loss of data
#add_definitions("/wd4244") # C4244: 'initializing': conversion from 'X' to 'Y', possible loss of data
#add_definitions("/wd4319") # C4319: '~': zero extending 'X' to 'Y' of greater size
#add_definitions("/wd4193") # C4193: #pragma warning(pop): no matching '#pragma warning(push)'

add_definitions("-D_SCL_SECURE_NO_WARNINGS")
add_definitions("-DUNICODE -D_UNICODE")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /Zo")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")

add_executable(splay main.cpp constants.h wavedev.cpp wavedev.h note.cpp note.h midi.cpp midi.h gui.cpp gui.h job_queue.cpp job_queue.h vis.cpp vis.h filter.cpp filter.h)
