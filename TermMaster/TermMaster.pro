TEMPLATE = app
TARGET = TermMaster

QT += core gui widgets serialport

CONFIG += c++17
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(yaml-cpp.pri)

SOURCES += \
    core/Registree.cpp \
    core/Module.cpp \
    core/ModuleManager.cpp \
    core/SettingsManager.cpp \
    modules/Base/BaseModule.cpp \
    modules/Base/BaseModuleApi.cpp \
    modules/Terminal/TerminalModule.cpp \
    modules/Terminal/TerminalModuleApi.cpp \
    widgets/ConfigSerialWidget.cpp \
    widgets/ConfigTerminalEmulationWidget.cpp \
    SerialTerminal.cpp \
    dialogs/SettingsDialog.cpp \
    Terminal.cpp \
    main.cpp \
    MainWindow.cpp \
    widgets/ConfigBaseWidget.cpp \
    widgets/ConfigTerminalWindowWidget.cpp

HEADERS += \
    core/Registree.h \
    core/Module.h \
    core/ModuleManager.h \
    core/SettingsManager.h \
    modules/Base/BaseModule.h \
    modules/Base/BaseModuleApi.h \
    modules/Terminal/TerminalModule.h \
    modules/Terminal/TerminalModuleApi.h \
    utils.h \
    widgets/ConfigSerialWidget.h \
    widgets/ConfigTerminalEmulationWidget.h \
    MainWindow.h \
    SerialTerminal.h \
    dialogs/SettingsDialog.h \
    Terminal.h \
    version.h \
    widgets/ConfigBaseWidget.h \
    widgets/ConfigTerminalWindowWidget.h

FORMS += \
    widgets/ConfigSerialWidget.ui \
    widgets/ConfigTerminalEmulationWidget.ui \
    MainWindow.ui \
    dialogs/SettingsDialog.ui \
    widgets/ConfigTerminalWindowWidget.ui


LIBS += -lyaml-cpp

RESOURCES += \
    res.qrc


