cmake_minimum_required(VERSION 3.16)
project(QtController VERSION 1.4.0 LANGUAGES CXX)
if(MINGW)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-finput-charset=UTF-8>" "$<$<COMPILE_LANGUAGE:CXX>:-fexec-charset=UTF-8>")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/src/views")
include(CTest)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Concurrent)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
endif()
set(HOST_RUNTIME_OUTPUT_DIR "C:/Documents/0_Qt/build/Qt-GeneralController-qt${QT_VERSION_MAJOR}"
    CACHE PATH "Application and required runtime files only")
include(cmake/SignalDependencies.cmake)
add_library(signal_database STATIC
    src/domain/SignalTypes.h src/model/SignalCodec.h src/model/SignalCodec.cpp
    src/model/DatabaseImporter.h src/model/DatabaseImporter.cpp
    src/model/DbcImporter.cpp src/model/LdfImporter.cpp
    src/infrastructure/SignalConfigurationStore.cpp src/infrastructure/SignalConfigurationStore.h)
target_include_directories(signal_database PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(signal_database PUBLIC Qt${QT_VERSION_MAJOR}::Core PRIVATE signal_dbcppp signal_exact_decimal)
add_library(signal_runtime STATIC
    src/protocol/CanTxScheduler.cpp src/protocol/CanTxScheduler.h
    src/protocol/LinScheduleRunner.cpp src/protocol/LinScheduleRunner.h
    src/protocol/OperationCoordinator.h)
target_link_libraries(signal_runtime PUBLIC signal_database)
set(COMMUNICATION_SOURCE_DIR "" CACHE PATH "Directory containing the external peak_communication CMakeLists.txt")
if(NOT COMMUNICATION_SOURCE_DIR)
    # Resolve the shared module independently of the build directory. An explicit
    # cache value still takes precedence over the supported sibling layouts.
    foreach(candidate IN ITEMS
            "${CMAKE_CURRENT_SOURCE_DIR}/../Qt-ACTestController/resource/communication"
            "${CMAKE_CURRENT_SOURCE_DIR}/../communication-provider/resource/communication")
        if(EXISTS "${candidate}/CMakeLists.txt")
            get_filename_component(candidate "${candidate}" REALPATH)
            set(COMMUNICATION_SOURCE_DIR "${candidate}" CACHE PATH
                "Directory containing the external peak_communication CMakeLists.txt" FORCE)
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${COMMUNICATION_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Communication module not found at '${COMMUNICATION_SOURCE_DIR}'. "
        "Place Qt-ACTestController or communication-provider beside this project, "
        "or set -DCOMMUNICATION_SOURCE_DIR=<module>/resource/communication.")
endif()
message(STATUS "Communication module: ${COMMUNICATION_SOURCE_DIR}")
add_subdirectory("${COMMUNICATION_SOURCE_DIR}" peak_communication)
if(NOT TARGET peak_communication OR NOT COMMAND peak_deploy)
    message(FATAL_ERROR "COMMUNICATION_SOURCE_DIR must define peak_communication and peak_deploy")
endif()
add_library(cdd_model STATIC
    src/model/CddDatabase.h src/model/CddDatabase.cpp src/model/CddCodec.cpp)
target_include_directories(cdd_model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(cdd_model PUBLIC Qt${QT_VERSION_MAJOR}::Core)
if(MINGW)
    target_compile_options(cdd_model PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
add_library(bootloader_protocol STATIC
    src/protocol/DiagnosticTransport.h
    src/protocol/CanTransport.h src/protocol/CanTransport.cpp
    src/protocol/SimulatedUdsEcu.h src/protocol/SimulatedUdsEcu.cpp
    src/protocol/SimulatedCanEcu.h src/protocol/SimulatedCanEcu.cpp
    src/protocol/FirmwareImage.h src/protocol/FirmwareImage.cpp
    src/protocol/LinTransport.h src/protocol/LinTransport.cpp
    src/protocol/UdsSession.h src/protocol/UdsSession.cpp
    src/protocol/FlashJob.h src/protocol/FlashJob.cpp
    src/protocol/SimulatedLinEcu.h src/protocol/SimulatedLinEcu.cpp)
target_include_directories(bootloader_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bootloader_protocol PUBLIC Qt${QT_VERSION_MAJOR}::Core)
add_library(bootloader_ui STATIC
    src/localization/Language.h src/localization/Language.cpp resources/language.qrc
    src/views/UiLanguageController.h src/views/UiLanguageController.cpp
    src/infrastructure/BusHardware.h src/infrastructure/BusHardware.cpp
    src/infrastructure/TosunRuntime.h src/infrastructure/TosunRuntime.cpp
    src/infrastructure/TosunHardware.h src/infrastructure/TosunHardware.cpp
    src/infrastructure/TraceExporter.h src/infrastructure/TraceExporter.cpp
    src/infrastructure/TraceReader.h src/infrastructure/TraceReader.cpp
    src/views/FrameDataDelegate.h
    src/viewmodels/ReplayViewModel.h src/viewmodels/ReplayViewModel.cpp
    src/viewmodels/SignalPlotModel.h src/viewmodels/SignalPlotModel.cpp
    src/views/SignalPlotCanvas.h src/views/SignalPlotCanvas.cpp
    src/views/SignalPlotDialog.h src/views/SignalPlotDialog.cpp
    src/domain/HostTypes.h src/domain/HostTypes.cpp
    src/infrastructure/SettingsStore.h src/infrastructure/SettingsStore.cpp
    src/infrastructure/ChannelWorker.h src/infrastructure/ChannelWorker.cpp
    src/infrastructure/SignalTransmitter.h src/infrastructure/SignalTransmitter.cpp
    src/model/ChannelModel.h src/model/ChannelModel.cpp
    src/model/FrameTableModel.h src/model/FrameTableModel.cpp
    src/model/UdsServiceTableModel.h
    src/viewmodels/ChannelViewModel.h src/viewmodels/ChannelViewModel.cpp
    src/viewmodels/SignalTransmitViewModel.h src/viewmodels/SignalTransmitViewModel.cpp
    src/viewmodels/SignalTableModels.h src/viewmodels/SignalTableModels.cpp
    src/viewmodels/ChannelConfigurationViewModel.h src/viewmodels/ChannelConfigurationViewModel.cpp
    src/viewmodels/DiagnosticDraftViewModel.h src/viewmodels/DiagnosticDraftViewModel.cpp
    src/views/ChannelHardwareEditor.h src/views/ChannelHardwareEditor.cpp
    src/views/ChannelPage.h src/views/ChannelPage.cpp
    src/views/SignalTransmitPage.h src/views/SignalTransmitPage.cpp
    src/views/ChannelTabs.h
    src/views/UdsDiagnosticPage.h src/views/UdsDiagnosticPage.cpp
    src/views/UdsSettingsDialog.h src/views/UdsSettingsDialog.cpp
    src/views/MainWindow.h src/views/MainWindow.cpp src/views/MainWindow.ui
    resources/resources.qrc)
target_include_directories(bootloader_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bootloader_ui PUBLIC bootloader_protocol cdd_model peak_communication signal_runtime Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent)
if(MINGW)
    target_compile_options(bootloader_ui PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()
get_target_property(qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(qt_bin "${qmake_executable}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${qt_bin}" NO_DEFAULT_PATH)
option(BUILD_SEEDKEY_BRIDGE "Build the optional external DLL bridge (no algorithm included)" OFF)
if(BUILD_SEEDKEY_BRIDGE)
    set(SEEDKEY_CXX32 "" CACHE FILEPATH "Path to a 32-bit MinGW C++ compiler")
    if(NOT EXISTS "${SEEDKEY_CXX32}")
        message(FATAL_ERROR "Set SEEDKEY_CXX32 to a 32-bit MinGW C++ compiler")
    endif()
    set(SEEDKEY_BRIDGE "${CMAKE_BINARY_DIR}/seedkey/SeedkeyBridge32.exe")
    add_custom_command(OUTPUT "${SEEDKEY_BRIDGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/seedkey"
        COMMAND "${SEEDKEY_CXX32}" -O2 -static -municode "${CMAKE_SOURCE_DIR}/src/seedkey/SeedkeyBridge32.cpp" -o "${SEEDKEY_BRIDGE}"
        DEPENDS src/seedkey/SeedkeyBridge32.cpp VERBATIM)
    add_custom_target(seedkey_bridge ALL DEPENDS "${SEEDKEY_BRIDGE}")
endif()
function(host_deploy target)
    signal_deploy(${target})
    if(BUILD_SEEDKEY_BRIDGE)
        add_dependencies(${target} seedkey_bridge)
    endif()
    if(BUILD_SEEDKEY_BRIDGE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/seedkey"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SEEDKEY_BRIDGE}" "$<TARGET_FILE_DIR:${target}>/seedkey/SeedkeyBridge32.exe")
    endif()
    peak_deploy(${target})
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:${target}>")
    endif()
endfunction()
if(WIN32)
    enable_language(RC)
endif()
add_executable(QtBootloader WIN32 src/main.cpp)
set_target_properties(QtBootloader PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${HOST_RUNTIME_OUTPUT_DIR}")
if(WIN32)
    target_sources(QtBootloader PRIVATE resources/app.rc)
endif()
target_link_libraries(QtBootloader PRIVATE bootloader_ui)
host_deploy(QtBootloader)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Publish runtime files only; the build tree also contains sources and test binaries.
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(HOST_RELEASE_OUTPUT "${CMAKE_BINARY_DIR}/release/QtBootloader-${PROJECT_VERSION}-qt${QT_VERSION_MAJOR}"
        CACHE PATH "New directory for the distributable release (existing output is never overwritten)")
    add_custom_target(package_release
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/package_release.py"
            --build "${HOST_RUNTIME_OUTPUT_DIR}" --output "${HOST_RELEASE_OUTPUT}" --qt-major "${QT_VERSION_MAJOR}"
        DEPENDS QtBootloader
        COMMENT "Packaging runtime files without build caches or regression binaries"
        VERBATIM)
endif()
