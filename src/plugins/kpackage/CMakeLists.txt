# SPDX-FileCopyrightText: 2023 Alexander Lohnau <alexander.lohnau@gmx.de>
# SPDX-License-Identifier: BSD-2-Clause

function(kwin_add_kpackage_structure dir file)
    # QindaQt: the structures load by id (QindaQtKWin/...), the file names only
    # have to differ from stock KWin's kwin_* structures.
    kcoreaddons_add_plugin(qindaqt_kwin_${dir} SOURCES ${file} INSTALL_NAMESPACE kf6/packagestructure)
    target_link_libraries(qindaqt_kwin_${dir} KF6::Package)
endfunction()

kwin_add_kpackage_structure(decoration decoration/decoration.cpp)
kwin_add_kpackage_structure(effect effect/effect.cpp)
kwin_add_kpackage_structure(scripts scripts/scripts.cpp)
kwin_add_kpackage_structure(windowswitcher windowswitcher/windowswitcher.cpp)
