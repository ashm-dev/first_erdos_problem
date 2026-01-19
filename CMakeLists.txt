cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Настройки компилятора (ДО project!)
# ============================================================================
# Ищем clang-21 или просто clang.
# Это должно быть сделано ДО команды project(), чтобы CMake подхватил их.

find_program(CLANG_21 clang-21)

if(CLANG_21)
    set(CMAKE_C_COMPILER ${CLANG_21})
    # Если вдруг понадобится C++, можно раскомментировать:
    # find_program(CLANGXX_21 clang++-21)
    # if(CLANGXX_21)
    #     set(CMAKE_CXX_COMPILER ${CLANGXX_21})
    # endif()
    message(STATUS "Выбран компилятор: ${CLANG_21}")
else()
    find_program(CLANG clang)
    if(CLANG)
        set(CMAKE_C_COMPILER ${CLANG})
        message(STATUS "Выбран компилятор: ${CLANG}")
    else()
        message(WARNING "Clang не найден, будет использован системный компилятор (обычно GCC)")
    endif()
endif()

# ============================================================================
# Проект Erdos Solver
# ============================================================================

project(erdos_solver
    VERSION 1.0.0
    DESCRIPTION "Поиск B-последовательностей (задача Эрдёша)"
    LANGUAGES C
)

# ============================================================================
# Настройки стандарта C
# ============================================================================

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================================
# Типы сборки
# ============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Тип сборки" FORCE)
endif()

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "RelWithDebInfo" "MinSizeRel")

message(STATUS "Тип сборки: ${CMAKE_BUILD_TYPE}")

# ============================================================================
# Флаги компиляции
# ============================================================================

set(COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wformat=2
    -Wformat-security
    -Wnull-dereference
    -Wstack-protector
    -Wstrict-overflow=2
    -Warray-bounds
    -Wshift-overflow
    -Wcast-qual
    -Wcast-align
    -Wdouble-promotion
    -Wfloat-equal
    -Wundef
    -Wunused-macros
)

set(RELEASE_FLAGS
    -O3
    -march=native
    -mtune=native
    -flto=full
    -ffast-math
    -funroll-loops
    -fomit-frame-pointer
    -fstrict-aliasing
    -fno-exceptions
    -fno-rtti
    -fvectorize
    -fslp-vectorize
    -fmerge-all-constants
    -ffunction-sections
    -fdata-sections
)

set(DEBUG_FLAGS
    -O0
    -g3
    -ggdb
    -fno-omit-frame-pointer
    -fsanitize=address,undefined
    -fno-optimize-sibling-calls
)

set(RELWITHDEBINFO_FLAGS
    -O2
    -g
    -march=native
    -mtune=native
    -flto=thin
    -fno-omit-frame-pointer
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(${COMMON_WARNINGS} ${RELEASE_FLAGS})
    # Для Clang линковка LTO требует правильного линкера (обычно lld или gold),
    # но флаги ниже работают в большинстве случаев
    add_link_options(-flto=full -Wl,--gc-sections -Wl,-O2)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(${COMMON_WARNINGS} ${DEBUG_FLAGS})
    add_link_options(-fsanitize=address,undefined)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(${COMMON_WARNINGS} ${RELWITHDEBINFO_FLAGS})
    add_link_options(-flto=thin)
else()
    add_compile_options(${COMMON_WARNINGS} -O2)
endif()

# ============================================================================
# Поиск зависимостей
# ============================================================================

# SQLite3
find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3 найден: ${SQLite3_LIBRARIES}")

# Threads
find_package(Threads REQUIRED)

# mimalloc
option(USE_MIMALLOC "Использовать mimalloc аллокатор" ON)

if(USE_MIMALLOC)
    find_package(mimalloc QUIET)
    if(mimalloc_FOUND)
        message(STATUS "mimalloc найден через find_package")
        set(MIMALLOC_AVAILABLE TRUE)
    else()
        find_library(MIMALLOC_LIBRARY mimalloc)
        find_path(MIMALLOC_INCLUDE_DIR mimalloc.h PATH_SUFFIXES mimalloc)
        if(MIMALLOC_LIBRARY)
            message(STATUS "mimalloc найден: ${MIMALLOC_LIBRARY}")
            set(MIMALLOC_AVAILABLE TRUE)
        else()
            message(STATUS "mimalloc не найден, загружаем через FetchContent...")
            include(FetchContent)
            FetchContent_Declare(
                mimalloc
                GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
                GIT_TAG v2.2.4
                GIT_SHALLOW TRUE
            )
            set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
            set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
            set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
            FetchContent_MakeAvailable(mimalloc)
            # Отключаем строгие предупреждения для внешней зависимости
            if(TARGET mimalloc-static)
                target_compile_options(mimalloc-static PRIVATE
                    -w  # Отключить все предупреждения для этой цели
                )
            endif()
            set(MIMALLOC_AVAILABLE TRUE)
            set(MIMALLOC_FETCHED TRUE)
            message(STATUS "mimalloc загружен через FetchContent")
        endif()
    endif()
endif()

# ============================================================================
# Исходные файлы
# ============================================================================

set(SOURCES
    src/main.c
    src/logger.c
    src/subset_sum_manager.c
    src/backtrack_solver.c
    src/db_manager.c
)

set(HEADERS
    include/types.h
    include/logger.h
    include/subset_sum_manager.h
    include/backtrack_solver.h
    include/db_manager.h
)

# ============================================================================
# Исполняемый файл
# ============================================================================

add_executable(erdos_solver ${SOURCES} ${HEADERS})

target_include_directories(erdos_solver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(erdos_solver PRIVATE
    ${SQLite3_LIBRARIES}
    Threads::Threads
    m
)

# Подключение mimalloc
if(MIMALLOC_AVAILABLE)
    if(mimalloc_FOUND)
        target_link_libraries(erdos_solver PRIVATE mimalloc)
    elseif(MIMALLOC_FETCHED)
        target_link_libraries(erdos_solver PRIVATE mimalloc-static)
    elseif(MIMALLOC_LIBRARY)
        target_link_libraries(erdos_solver PRIVATE ${MIMALLOC_LIBRARY})
        if(MIMALLOC_INCLUDE_DIR)
            target_include_directories(erdos_solver PRIVATE ${MIMALLOC_INCLUDE_DIR})
        endif()
    endif()
    # Макрос для возможного использования в коде, если нужно
    target_compile_definitions(erdos_solver PRIVATE USE_MIMALLOC)
    message(STATUS "mimalloc успешно подключен к erdos_solver")
endif()

target_compile_definitions(erdos_solver PRIVATE
    _GNU_SOURCE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# ============================================================================
# Установка
# ============================================================================

install(TARGETS erdos_solver RUNTIME DESTINATION bin)

# ============================================================================
# Дополнительные цели
# ============================================================================

add_custom_target(clean-cmake
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMENT "Очистка кэша CMake"
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${SOURCES} ${HEADERS}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Форматирование кода с помощью clang-format"
    )
endif()

find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY} ${SOURCES} -- -I${CMAKE_SOURCE_DIR}/include
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Статический анализ с помощью clang-tidy"
    )
endif()

# ============================================================================
# Инфо
# ============================================================================
message(STATUS "")
message(STATUS "=== Конфигурация завершена ===")
message(STATUS "Компилятор: ${CMAKE_C_COMPILER}")
message(STATUS "")
