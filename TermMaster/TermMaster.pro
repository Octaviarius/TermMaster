QT += core gui widgets serialport

CONFIG += c++17
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ConfigSerialDialog.cpp \
    ConfigTerminalDialog.cpp \
    SerialTerminal.cpp \
    SettingsDialog.cpp \
    Terminal.cpp \
    main.cpp \
    MainWindow.cpp \
    utils.cpp

HEADERS += \
    ConfigSerialDialog.h \
    ConfigTerminalDialog.h \
    MainWindow.h \
    SerialTerminal.h \
    SettingsDialog.h \
    Terminal.h \
    utils.h \
    version.h

FORMS += \
    ConfigSerialDialog.ui \
    ConfigTerminalDialog.ui \
    MainWindow.ui \
    SettingsDialog.ui

CONFIG(debug, debug|release)
{
    LIBS += -L$$PWD/../3dparty/build-yaml-cpp-Desktop_Qt_6_4_0_MinGW_64_bit-Debug/ -lyaml-cppd
}

CONFIG(release, debug|release)
{
    LIBS += -L$$PWD/../3dparty/build-yaml-cpp-Desktop_Qt_6_4_0_MinGW_64_bit-Release/ -lyaml-cpp
}

INCLUDEPATH += $$PWD/../3dparty/yaml-cpp/include
DEPENDPATH += $$PWD/../3dparty/yaml-cpp/include
