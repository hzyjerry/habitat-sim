set(datatool_SOURCES datatool.cpp SceneLoader.cpp)

add_executable(datatool ${datatool_SOURCES})

# Assimp. Use a system package, if preferred.
if(USE_SYSTEM_ASSIMP)
  find_package(ASSIMP REQUIRED)
else()
  set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "ASSIMP_BUILD_ASSIMP_TOOLS" FORCE)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "ASSIMP_BUILD_TESTS" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "ASSIMP_BUILD_TESTS" FORCE)
  add_subdirectory("${DEPS_DIR}/assimp" "${CMAKE_CURRENT_BINARY_DIR}/assimp")
  include_directories(SYSTEM "${DEPS_DIR}/assimp/include")
  # message(STATUS "Found ASSIMP in ${ASSIMP_INCLUDE_DIR}")
endif()

target_link_libraries(datatool
  PRIVATE
    assets
    assimp
    nav
)
