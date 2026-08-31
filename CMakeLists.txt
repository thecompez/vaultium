cmake_minimum_required(VERSION 3.28)

project(Vaultium
    VERSION 0.3.0
    DESCRIPTION "Reliable backup manager for databases, filesystems, and service configuration"
    LANGUAGES CXX
)

if(POLICY CMP0155)
    cmake_policy(SET CMP0155 NEW)
endif()

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VAULTIUM_BUILD_GUI "Build the Vaultium Qt/QML application" OFF)
option(VAULTIUM_BUILD_TESTS "Build Vaultium tests" ON)
option(VAULTIUM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(VAULTIUM_BUILD_UI "Deprecated alias for VAULTIUM_BUILD_GUI" OFF)

if(VAULTIUM_BUILD_UI)
    message(DEPRECATION "VAULTIUM_BUILD_UI is deprecated; use VAULTIUM_BUILD_GUI.")
    set(VAULTIUM_BUILD_GUI ON CACHE BOOL "Build the Vaultium Qt/QML application" FORCE)
endif()

if(APPLE)
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_MACOS=1)
elseif(UNIX)
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_LINUX=1)
else()
    message(FATAL_ERROR "Vaultium 0.3 currently supports Linux and macOS. Windows support is not release-ready yet.")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)

function(vaultium_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(VAULTIUM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(VAULTIUM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_library(vaultium_core STATIC)

target_sources(vaultium_core
    PRIVATE
        core/logger.cpp
        core/sha256.cpp
        core/service_catalog.cpp
        core/backup_config.cpp
        core/process_runner.cpp
        core/mysql_backup_engine.cpp
        core/postgresql_backup_engine.cpp
        core/sqlite_backup_engine.cpp
        core/database_backup_source.cpp
        core/filesystem_backup_source.cpp
        core/service_config_backup_source.cpp
        core/backup_source.cpp
        core/backup_manager.cpp
        core/inventory.cpp
        core/scheduler.cpp
        remote/ssh_client.cpp
        remote/remote_client.cpp
        remote/remote_provisioner.cpp
        remote/ssh_command_runner.cpp
        agent/agent_app.cpp
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        core/types.cppm
        core/logger.cppm
        core/sha256.cppm
        core/service_catalog.cppm
        core/backup_config.cppm
        core/process_runner.cppm
        core/database_backup_engine.cppm
        core/mysql_backup_engine.cppm
        core/postgresql_backup_engine.cppm
        core/sqlite_backup_engine.cppm
        core/backup_source.cppm
        core/database_backup_source.cppm
        core/filesystem_backup_source.cppm
        core/service_config_backup_source.cppm
        core/backup_manager.cppm
        core/inventory.cppm
        core/scheduler.cppm
        core/core.cppm
        remote/ssh_client.cppm
        remote/remote_client.cppm
        remote/remote_provisioner.cppm
        remote/ssh_command_runner.cppm
        agent/agent_app.cppm
)

target_compile_features(vaultium_core PUBLIC cxx_std_23)
target_compile_definitions(vaultium_core PUBLIC ${VAULTIUM_PLATFORM_DEFINE})
target_link_libraries(vaultium_core PUBLIC PkgConfig::LIBSSH2)
vaultium_enable_warnings(vaultium_core)

add_executable(vaultium main.cpp)
target_sources(vaultium
    PRIVATE
        cli/cli_app.cpp
    PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        cli/cli_app.cppm
)
target_link_libraries(vaultium PRIVATE vaultium_core)
vaultium_enable_warnings(vaultium)

if(VAULTIUM_BUILD_TESTS)
    include(CTest)
    add_executable(vaultium_tests tests/test_main.cpp)
    target_link_libraries(vaultium_tests PRIVATE vaultium_core)
    vaultium_enable_warnings(vaultium_tests)
    add_test(NAME vaultium_tests COMMAND vaultium_tests)
endif()

if(VAULTIUM_BUILD_GUI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Gui QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(vaultium_gui gui/main.cpp)
    set_source_files_properties(gui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(vaultium_gui
        URI VaultiumUI
        VERSION 1.0
        QML_FILES
            gui/qml/Theme.qml
            gui/qml/AppIcon.qml
            gui/qml/Card.qml
            gui/qml/AppButton.qml
            gui/qml/AppTextField.qml
            gui/qml/AppComboBox.qml
            gui/qml/AppSwitch.qml
            gui/qml/IconButton.qml
            gui/qml/StatusPill.qml
            gui/qml/NavItem.qml
            gui/qml/Toast.qml
            gui/qml/TransferBar.qml
            gui/qml/LogConsole.qml
            gui/qml/EmptyState.qml
            gui/qml/SegmentedControl.qml
            gui/qml/StatCard.qml
            gui/qml/Skeleton.qml
            gui/qml/Banner.qml
            gui/qml/ConfirmDialog.qml
            gui/qml/WizardShell.qml
            gui/qml/BackupWizard.qml
            gui/qml/Main.qml
            gui/qml/DashboardScreen.qml
            gui/qml/ServersScreen.qml
            gui/qml/SchedulesScreen.qml
            gui/qml/BackupJobsScreen.qml
            gui/qml/CreateBackupJobScreen.qml
            gui/qml/BackupHistoryScreen.qml
            gui/qml/RestoreVerifyScreen.qml
            gui/qml/SettingsScreen.qml
        SOURCES
            gui/app_state.h
            gui/app_state.cpp
            gui/backup_controller.h
            gui/backup_controller.cpp
            gui/backup_jobs_model.h
            gui/backup_jobs_model.cpp
            gui/inventory_view_model.h
            gui/inventory_view_model.cpp
            gui/schedule_view_model.h
            gui/schedule_view_model.cpp
            gui/servers_view_model.h
            gui/servers_view_model.cpp
    )

    target_include_directories(vaultium_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(vaultium_gui PRIVATE vaultium_core Qt6::Quick Qt6::QuickControls2)
    set_target_properties(vaultium_gui PROPERTIES CXX_SCAN_FOR_MODULES ON)
    vaultium_enable_warnings(vaultium_gui)

    install(TARGETS vaultium_gui
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(TARGETS vaultium RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
