########################################################
# FakeDecoWithShadows
########################################################
add_library(fakedecoshadows MODULE fakedecoration_with_shadows.cpp)
set_target_properties(fakedecoshadows PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/fakes/qindaqt-kwin/decorations")
target_link_libraries(fakedecoshadows
    PUBLIC
        Qt::Core
        Qt::Gui
    PRIVATE
        KDecoration3::KDecoration
        KF6::CoreAddons)

