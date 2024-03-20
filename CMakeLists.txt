cmake_minimum_required(VERSION 3.20.6)

project(kmozillahelper)

set(KF_MIN_VERSION "5.85.0")

find_package(
    ECM
    ${KF_MIN_VERSION}
    REQUIRED
    NO_MODULE)

set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR})

set(KDE_COMPILERSETTINGS_LEVEL ${KF_MIN_VERSION})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(FeatureSummary)

find_package(
    KF5 REQUIRED
    COMPONENTS Notifications
               KIO
               WindowSystem
               I18n)

add_executable(kmozillahelper main.cpp main.h)

target_link_libraries(
    kmozillahelper
    KF5::I18n
    KF5::KIOWidgets
    KF5::Notifications
    KF5::WindowSystem)

# add clang-format target for all our real source files
file(
    GLOB_RECURSE
    ALL_CLANG_FORMAT_SOURCE_FILES
    *.cpp
    *.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES
                         FATAL_ON_MISSING_REQUIRED_PACKAGES)

install(TARGETS kmozillahelper DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/mozilla/)
install(FILES kmozillahelper.notifyrc DESTINATION ${KNOTIFYRC_INSTALL_DIR})
