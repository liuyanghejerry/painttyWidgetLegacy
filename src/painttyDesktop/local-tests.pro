include(painttyDesktop.pro)
QT += testlib
CONFIG += testcase
TARGET = local-tests
SOURCES -= main.cpp
SOURCES += tests/local-tests.cpp
DESTDIR = $$OUT_PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/generated
RCC_DIR = $$OUT_PWD/generated
UI_DIR = $$OUT_PWD/generated
INCLUDEPATH += $$PWD $$UI_DIR
