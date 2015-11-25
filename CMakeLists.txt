cmake_minimum_required( VERSION 2.6 )

project( kmozillahelper )

find_package( KDE4 REQUIRED )
include( KDE4Defaults )
include( MacroLibrary )
add_definitions( ${QT_DEFINITIONS} ${KDE4_DEFINITIONS} -DHAVE_CONFIG_H=1 )
include_directories( ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${KDE4_INCLUDES} )

########### next target ###############

set( kmozillahelper_SRCS
     main.cpp )

kde4_add_executable( kmozillahelper ${kmozillahelper_SRCS} )
target_link_libraries( kmozillahelper ${KDE4_KFILE_LIBS} )

# TODO hardcoded path?
install( TARGETS kmozillahelper DESTINATION /usr/lib/mozilla/ )

install( FILES kmozillahelper.notifyrc  DESTINATION  ${DATA_INSTALL_DIR}/kmozillahelper )
