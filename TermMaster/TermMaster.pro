TEMPLATE = app
TARGET = TermMaster

QT += core gui widgets serialport

CONFIG += c++17
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(yaml-cpp.pri)

SOURCES += \
    core/Path.cpp \
    core/Registree.cpp \
    core/Module.cpp \
    core/ModuleManager.cpp \
    core/SettingsManager.cpp \
    modules/Base/BaseModule.cpp \
    modules/Base/BaseModuleApi.cpp \
    modules/Base/windows/MainWindow.cpp \
    modules/Base/dialogs/SettingsDialog.cpp \
    modules/Base/widgets/ConfigBaseWidget.cpp \
    modules/Terminal/TerminalModule.cpp \
    modules/Terminal/TerminalModuleApi.cpp \
    modules/Terminal/widgets/ConfigSerialWidget.cpp \
    modules/Terminal/widgets/ConfigTerminalEmulationWidget.cpp \
    modules/Terminal/widgets/ConfigTerminalWindowWidget.cpp \
    main.cpp \

HEADERS += \
    core/Path.h \
    core/Registree.h \
    core/Module.h \
    core/ModuleManager.h \
    core/SettingsManager.h \
    core/utils.h \
    modules/Base/BaseModule.h \
    modules/Base/BaseModuleApi.h \
    modules/Base/windows/MainWindow.h \
    modules/Base/dialogs/SettingsDialog.h \
    modules/Base/widgets/ConfigBaseWidget.h \
    modules/Terminal/TerminalModule.h \
    modules/Terminal/TerminalModuleApi.h \
    modules/Terminal/widgets/ConfigSerialWidget.h \
    modules/Terminal/widgets/ConfigTerminalEmulationWidget.h \
    modules/Terminal/widgets/ConfigTerminalWindowWidget.h \
    version.h

FORMS += \
    modules/Base/windows/MainWindow.ui \
    modules/Base/dialogs/SettingsDialog.ui \
    modules/Terminal/widgets/ConfigSerialWidget.ui \
    modules/Terminal/widgets/ConfigTerminalEmulationWidget.ui \
    modules/Terminal/widgets/ConfigTerminalWindowWidget.ui


LIBS += -lyaml-cpp

RESOURCES += \
    res.qrc

DISTFILES += \
    res/icon/cat-connections.svg \
    res/icon/cat-terminal.svg \
    res/icon/connected.svg \
    res/icon/disconnected.svg \
    res/icon/new-terminal-console.svg \
    res/icon/new-terminal-serial.svg \
    res/icon/new-terminal-socket.svg \
    res/icon/new-terminal-ssh.svg \
    res/icon/new-terminal.svg


