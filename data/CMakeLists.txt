# QindaQt: no hicolor "kwin" icons and no kconf_update migrations; a fresh
# config namespace has nothing to migrate (container-wm ADR-0289).

########### install files ###############

install(FILES org_kde_kwin.categories DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR})
