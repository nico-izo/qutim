import "../Protocol.qbs" as Protocol

Protocol {
    condition: false //qbs.targetOS === 'linux'

    //Depends { name: "telepathy.qt" }

    Depends {
        name: "Qt.dbus"
        condition: qbs.targetOS.contains("linux")
    }
    cpp.dynamicLibraries: ["telepathy-qt5"]
    cpp.includePaths: ["/usr/include/telepathy-qt5/"]
}
