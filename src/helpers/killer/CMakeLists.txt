########### next target ###############

set(kwin_killer_helper_SRCS killer.cpp)

add_executable(kwin_killer_helper ${kwin_killer_helper_SRCS})
set_target_properties(kwin_killer_helper PROPERTIES OUTPUT_NAME killer-helper)
# QindaQt: processes of root or another user end through pkexec and the native
# terminate-process helper instead of KAuth (container-wm ADR-0289).
target_compile_definitions(kwin_killer_helper PRIVATE
    QINDAQT_KWIN_TERMINATE_PROCESS_BIN="${QINDAQT_KWIN_TERMINATE_PROCESS_BIN}"
    QINDAQT_KWIN_PKEXEC="${QINDAQT_KWIN_PKEXEC}")

target_link_libraries(kwin_killer_helper
    qindaqt_terminate_process_core
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
