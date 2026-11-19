cmake_minimum_required(VERSION 3.24)

project(PlantAnything VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# CommonLibSSE-NG (alandtse/CommonLibVR, "ng" branch) checked out in extern/CommonLibSSE-NG.
# Its dependencies come from vcpkg (see .github/workflows/build.yml for the exact commands).
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG/CMakeLists.txt")
	message(FATAL_ERROR "Clone CommonLibSSE-NG first:\n  git clone --branch v16.5.0 --recurse-submodules https://github.com/alandtse/CommonLibVR extern/CommonLibSSE-NG")
endif()
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLib tests" FORCE)
add_subdirectory(extern/CommonLibSSE-NG CommonLibSSE EXCLUDE_FROM_ALL)

add_library(${PROJECT_NAME} SHARED
	src/main.cpp
	src/Config.cpp
	src/PlantRegistry.cpp
	src/Garden.cpp
	src/Events.cpp
	src/Soil.cpp
	src/SoilPlants.cpp
	src/Fixups.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /Zc:preprocessor /utf-8 /EHsc /W4 /wd4100)
	target_link_options(${PROJECT_NAME} PRIVATE /DEBUG:FULL)
endif()

# Optional: set SKYRIM_MODS_FOLDER (e.g. your MO2 "mods" folder) to auto-deploy after build.
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
	set(DEPLOY_DIR "$ENV{SKYRIM_MODS_FOLDER}/PlantAnything/SKSE/Plugins")
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/dist/SKSE/Plugins/PlantAnything.ini" "${DEPLOY_DIR}"
	)
endif()
