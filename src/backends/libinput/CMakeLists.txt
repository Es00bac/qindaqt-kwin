qt_generate_dbus_interface(device.h org.kde.kwin.InputDevice.xml OPTIONS -A)

add_custom_target(
    KWinInputDBusInterfaces
    ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml
)

# QindaQt: installed under its qindaqt-kwin name (container-wm ADR-0289).
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kwin.InputDevice.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR} RENAME org.qindaqt.KWin.InputDevice.xml)

target_sources(kwin PRIVATE
    connection.cpp
    context.cpp
    device.cpp
    events.cpp
    libinput_logging.cpp
    libinputbackend.cpp
)
target_link_libraries(kwin PRIVATE Libinput::Libinput)
