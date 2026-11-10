cmake_minimum_required(VERSION 3.24)
# The version lives here only; it flows into the app, the installers and the release names.
project(ChemFox VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS supported")

# Where "Help → Report a Problem" and the update link point. The release workflow sets it.
set(CHEMFOX_REPO_URL "https://github.com/VoxelMC/ChemFox" CACHE STRING "GitHub repository URL, e.g. https://github.com/you/ChemFox")
configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

# RDKit ships no CMake config on conda-forge for Windows, so link it by hand.
set(RDKIT_ROOT "" CACHE PATH "Prefix that contains include/rdkit and lib/RDKit*")
find_path(RDKIT_INCLUDE_DIR GraphMol/ROMol.h HINTS ${RDKIT_ROOT}/include/rdkit REQUIRED)
set(RDKIT_COMPONENTS GraphMol SmilesParse FileParsers Depictor Descriptors RDInchiLib
    RDGeneral RDGeometryLib DataStructs SubstructMatch MolTransforms ChemTransforms
    RingDecomposerLib coordgen MolInterchange FMCS CIPLabeler)
set(RDKIT_LIBRARIES "")
foreach(c ${RDKIT_COMPONENTS})
  find_library(RDKIT_${c}_LIB NAMES RDKit${c} HINTS ${RDKIT_ROOT}/lib REQUIRED)
  list(APPEND RDKIT_LIBRARIES ${RDKIT_${c}_LIB})
endforeach()
find_path(BOOST_INCLUDE_DIR boost/version.hpp HINTS ${RDKIT_ROOT}/include REQUIRED)

add_library(rdkit_deps INTERFACE)
target_include_directories(rdkit_deps INTERFACE ${RDKIT_INCLUDE_DIR} ${BOOST_INCLUDE_DIR})
target_link_libraries(rdkit_deps INTERFACE ${RDKIT_LIBRARIES})
target_compile_definitions(rdkit_deps INTERFACE RDKIT_DYN_LINK BOOST_ALL_NO_LIB BOOST_ALL_DYN_LINK)

if(MSVC)
  add_compile_options(/W3 /permissive- /Zc:__cplusplus /utf-8 /bigobj /EHsc)
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

function(chemfox_module name dir)
  file(GLOB srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/${dir}/*.cpp ${CMAKE_SOURCE_DIR}/src/${dir}/*.h)
  add_library(${name} STATIC ${srcs})
  target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
endfunction()

chemfox_module(cd_model core/model)
target_link_libraries(cd_model PUBLIC Qt6::Core)

chemfox_module(cd_chem core/chem)
target_link_libraries(cd_chem PUBLIC cd_model PRIVATE rdkit_deps)

chemfox_module(cd_commands core/commands)
target_link_libraries(cd_commands PUBLIC cd_model Qt6::Gui)

chemfox_module(cd_style style)
target_link_libraries(cd_style PUBLIC Qt6::Gui)

chemfox_module(cd_render render)
target_link_libraries(cd_render PUBLIC cd_model cd_style Qt6::Gui)

chemfox_module(cd_warnings warnings)
target_link_libraries(cd_warnings PUBLIC cd_model cd_chem cd_style)

chemfox_module(cd_io io)
target_link_libraries(cd_io PUBLIC cd_model cd_chem cd_render cd_style Qt6::Svg)
if(WIN32)
  target_link_libraries(cd_io PRIVATE gdiplus)
endif()

chemfox_module(cd_editor editor)
target_link_libraries(cd_editor PUBLIC cd_model cd_chem cd_commands cd_render cd_style cd_warnings cd_io Qt6::Widgets)

# --- the application ---------------------------------------------------------------
file(GLOB app_srcs CONFIGURE_DEPENDS src/app/*.cpp src/app/*.h)
set(platform_srcs "")
if(WIN32)
  configure_file(packaging/windows/chemfox.rc.in ${CMAKE_BINARY_DIR}/generated/chemfox.rc @ONLY)
  list(APPEND platform_srcs ${CMAKE_BINARY_DIR}/generated/chemfox.rc)
elseif(APPLE)
  set(MACOS_ICON ${CMAKE_SOURCE_DIR}/packaging/icon/chemfox.icns)
  set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  list(APPEND platform_srcs ${MACOS_ICON})
endif()
qt_add_executable(chemfox WIN32 MACOSX_BUNDLE ${app_srcs} ${platform_srcs})
set_target_properties(chemfox PROPERTIES
  OUTPUT_NAME ChemFox
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
  MACOSX_BUNDLE_BUNDLE_NAME ChemFox
  MACOSX_BUNDLE_GUI_IDENTIFIER dev.trevfox.chemfox
  MACOSX_BUNDLE_ICON_FILE chemfox.icns
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
qt_add_resources(chemfox "icons" PREFIX "/icons" BASE packaging/icon FILES packaging/icon/chemfox.png)
target_link_libraries(chemfox PRIVATE cd_editor cd_io cd_warnings Qt6::Network)  # Network: the update check only

# Developer runs on Windows: let the exe find Qt plugins in the dependency prefix, and give a
# launcher that puts the prefix's DLLs on PATH. (Installed builds carry their own copies.)
get_filename_component(DEP_PREFIX "${RDKIT_INCLUDE_DIR}/../.." ABSOLUTE)
if(WIN32)
  file(WRITE ${CMAKE_BINARY_DIR}/bin/qt.conf "[Paths]\nPrefix=${DEP_PREFIX}\nPlugins=lib/qt6/plugins\n")
  file(TO_NATIVE_PATH "${DEP_PREFIX}/bin" DEP_BIN)
  file(WRITE ${CMAKE_SOURCE_DIR}/ChemFox.cmd
    "@echo off\r\nset \"PATH=${DEP_BIN};%PATH%\"\r\nstart \"\" \"%~dp0build\\bin\\ChemFox.exe\" %*\r\n")
endif()

# --- tests ---------------------------------------------------------------------------
enable_testing()
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
  file(GLOB test_srcs CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(chemfox_tests ${test_srcs} src/app/ReactionBuilder.cpp src/app/ReactionBuilder.h
    src/app/StyleEditor.cpp src/app/StyleEditor.h src/app/Shortcuts.cpp src/app/Shortcuts.h
    src/app/Preferences.cpp src/app/Preferences.h)
  target_compile_definitions(chemfox_tests PRIVATE CHEMFOX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(chemfox_tests PRIVATE cd_editor cd_io cd_commands cd_warnings Catch2::Catch2WithMain)
  add_test(NAME chemfox_tests COMMAND chemfox_tests)
endif()
