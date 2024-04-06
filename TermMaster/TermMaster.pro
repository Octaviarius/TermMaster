QT += core gui widgets serialport

CONFIG += c++17
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    Registree.cpp \
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
    Registree.h \
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


LIBS += -L$$PWD/../libs
LIBS += -lyaml-cpp

INCLUDEPATH += $$PWD/../3dparty/yaml-cpp/include
DEPENDPATH += $$PWD/../3dparty/yaml-cpp/include

RESOURCES += \
    res.qrc
