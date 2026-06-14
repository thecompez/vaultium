cmake_minimum_required(VERSION 4.1)

project(Vaultium
        VERSION 0.2.0
        DESCRIPTION "Cross-platform backup manager (database, filesystem, and more)"
        LANGUAGES CXX
)

if(POLICY CMP0155)
    cmake_policy(SET CMP0155 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED IMPORTED_TARGET libssh2)

option(VAULTIUM_BUILD_UI "Build Vaultium Qt UI" OFF)
option(VAULTIUM_BUILD_TESTS "Build Vaultium tests" ON)

# Per-platform definitions, shared by the library and everything that links it.
if(UNIX AND NOT APPLE)
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_LINUX=1)
elseif(APPLE)
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_MACOS=1)
elseif(WIN32)
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_WINDOWS=1)
else()
    set(VAULTIUM_PLATFORM_DEFINE VAULTIUM_PLATFORM_UNKNOWN=1)
endif()

# ---------------------------------------------------------------------------
# vaultium_core: UI-independent backup engine. The CLI, the test suite, and a
# future Qt/QML GUI all link against this and consume its C++ modules.
# ---------------------------------------------------------------------------
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
target_compile_options(vaultium_core PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# vaultium: command-line front end.
# ---------------------------------------------------------------------------
add_executable(vaultium main.cpp)

target_sources(vaultium
        PRIVATE
        cli/cli_app.cpp

        PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        cli/cli_app.cppm
)

target_link_libraries(vaultium PRIVATE vaultium_core)
target_compile_options(vaultium PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
if(VAULTIUM_BUILD_TESTS)
    enable_testing()

    add_executable(vaultium_tests
            tests/test_main.cpp
    )

    target_link_libraries(vaultium_tests PRIVATE vaultium_core)
    target_compile_options(vaultium_tests PRIVATE -Wall -Wextra -Wpedantic)

    add_test(NAME vaultium_tests COMMAND vaultium_tests)
endif()

# ---------------------------------------------------------------------------
# vaultium_gui: optional Qt/QML front end. OFF by default so the CLI and core
# build fully headless without any Qt dependency. The GUI links the same
# vaultium_core library and consumes the source-based engine through a thin
# C++ view-model bridge (BackupController, BackupJobsModel).
# ---------------------------------------------------------------------------
if(VAULTIUM_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Quick Gui QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(vaultium_gui
            gui/main.cpp
    )

    # Theme is a QML singleton (global design tokens).
    set_source_files_properties(gui/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    # NOTE: the URI must not collide (case-insensitively, for macOS APFS) with the
    # "vaultium" executable name, or the generated module directory clobbers it.
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

    # Qt disables C++20 module scanning on its targets by default; the bridge
    # sources import vaultium_core, so re-enable scanning for this target.
    set_target_properties(vaultium_gui PROPERTIES CXX_SCAN_FOR_MODULES ON)
endif()
