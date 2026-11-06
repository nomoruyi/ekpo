# Installed files besides the binaries, and the .deb (ARCHITECTURE.md §6: CPack, no debhelper).
set(EKPO_HELPER_INSTALL_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/ekpo/ekpo-helper")
configure_file(io.github.nomoruyi.ekpo.policy.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.nomoruyi.ekpo.policy @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.nomoruyi.ekpo.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES io.github.nomoruyi.ekpo.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES io.github.nomoruyi.ekpo.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME ekpo)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Design and switch the look of the boot menu, boot splash and Plasma splash")
set(CPACK_PACKAGE_CONTACT "nomoruyi <nosakhare_omoruyi@hotmail.de>")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION admin)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON) # library dependencies from the binaries
# QML modules and tools the binaries can't declare themselves.
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-org-kde-kirigami, qml6-module-org-kde-desktop, qml6-module-qtquick-controls, qml6-module-qtquick-dialogs, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qt6-svg-plugins, pkexec, systemd, grub2-common, bash")
include(CPack)
