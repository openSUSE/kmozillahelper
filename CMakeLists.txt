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

install(TARGETS kmozillahelper DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/mozilla/)
install(FILES kmozillahelper.notifyrc DESTINATION ${KNOTIFYRC_INSTALL_DIR})
