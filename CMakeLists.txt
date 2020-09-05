cmake_minimum_required(VERSION 3.6)
project(unununium)
set(CMAKE_C_STANDARD 99)

add_executable(un-disas
        un-disas.c
        disas.c
)

option(USE_OPENGL ON)
option(USE_DEBUG OFF)

find_package(SDL REQUIRED)
set(LIBRARIES ${SDL_LIBRARY})

set(SOURCES
        audio.c
        board.c
        board-BAT.c
        board-dummy.c
        board-V_X.c
        board-VII.c
        board-W60.c
        board-WAL.c
        disas.c
        emu.c
        i2c-bus.c
        i2c-eeprom.c
        io.c
        render.c
        timer.c
        uuu-sdl.c
        video.c
        platform-sdl.c
)

if (USE_OPENGL)
    find_package(OpenGL REQUIRED)
    set(LIBRARIES ${LIBRARIES} ${OPENGL_LIBRARY})
    set(SOURCES ${SOURCES} render-gl.c)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRENDER_gl")
else()
    set(SOURCES ${SOURCES} render-soft.c)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRENDER_soft")
endif()

if (USE_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUSE_DEBUG)")
endif()

add_executable(uuu-sdl ${SOURCES})
target_link_libraries(uuu-sdl ${LIBRARIES})
target_include_directories(uuu-sdl PUBLIC ${SDL_INCLUDE_DIR})
