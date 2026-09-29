########### next target ###############

set(kwin_killer_helper_SRCS killer.cpp)

add_executable(kwin_killer_helper ${kwin_killer_helper_SRCS})
set_target_properties(kwin_killer_helper PROPERTIES OUTPUT_NAME killer-helper)

target_link_libraries(kwin_killer_helper
    KF6::AuthCore
    KF6::GuiAddons
    KF6::I18n
    KF6::Service
    KF6::WidgetsAddons
    KF6::WindowSystem
    Qt::GuiPrivate
    Qt::Widgets
)

ecm_qt_declare_logging_category(kwin_killer_helper
    HEADER debug.h
    IDENTIFIER KWIN_KILLER
    CATEGORY_NAME org.qindaqt.kwin.killer
)

install(TARGETS kwin_killer_helper DESTINATION ${KDE_INSTALL_LIBEXECDIR}/qindaqt-kwin)

configure_file(org.qindaqt.kwin.killer.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/org.qindaqt.kwin.killer.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.qindaqt.kwin.killer.desktop DESTINATION ${KDE_INSTALL_APPDIR})
