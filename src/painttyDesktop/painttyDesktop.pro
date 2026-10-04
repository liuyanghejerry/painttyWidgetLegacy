#-------------------------------------------------
#
# Project created by QtCreator 2012-05-26T18:10:10
#
#-------------------------------------------------

QT       += core gui widgets

DEFINES += PAINTTY_DESKTOP

win32 {
    RC_FILE = app.rc
    LIBS += -limm32
    SOURCES +=
    HEADERS +=
}

!win32 {
    SOURCES +=
    HEADERS +=
}

linux {
    QMAKE_LFLAGS += -no-pie
}

mac {
    macx-clang: warning("if you encounter \"fatal error: \'initializer_list\' file not found\", try using makespecs \"macx-clang-libc++\"")
    ICON = iconset/icon.icns
}

include(../../commonconfigure.pri)

CONFIG += c++17

linux{
    TARGET = mrpaint
}

!linux{
    TARGET = MrPaint
}

TEMPLATE = app

SOURCES += main.cpp\
    widgets/mainwindow.cpp \
    widgets/newprojectdialog.cpp \
    widgets/canvas.cpp \
    misc/layermanager.cpp \
    widgets/colorwheel.cpp \
    widgets/colorgriditem.cpp \
    widgets/colorgrid.cpp \
    widgets/flowlayout.cpp \
    widgets/layerwidgetheader.cpp \
    widgets/layerwidget.cpp \
    widgets/layerlabel.cpp \
    widgets/layeritem.cpp \
    widgets/iconcheckbox.cpp \
    misc/layer.cpp \
    widgets/colorspinboxgroup.cpp \
    widgets/colorbox.cpp \
    widgets/aboutdialog.cpp \
    misc/singleshortcut.cpp \
    widgets/canvascontainer.cpp \
    paintingTools/brush/brushmanager.cpp \
    widgets/brushsettingswidget.cpp \
    widgets/helpdialog.cpp \
    widgets/panoramaview.cpp \
    widgets/panoramawidget.cpp \
    widgets/panoramaslider.cpp \
    misc/platformextend_win32.cpp \
    misc/platformextend_dummy.cpp \
    misc/platformextend.cpp \
    misc/shortcutmanager.cpp \
    widgets/configuredialog.cpp\
    widgets/clearlineedit.cpp \
    widgets/easycopylineedit.cpp \
    widgets/gradualbox.cpp \
    paintingTools/brush/abstractbrush.cpp \
    paintingTools/brush/abstractbrushv3.cpp \
    paintingTools/brush/basic-stamp.cpp \
    paintingTools/brush/basic-trail.cpp \
    paintingTools/brush/basic-color-system.cpp \
    paintingTools/brush/basicbrush.cpp \
    paintingTools/brush/basiceraser.cpp \
    paintingTools/brush/binarybrush.cpp \
    paintingTools/brush/brushfeature.cpp \
    paintingTools/brush/maskbased.cpp \
    paintingTools/brush/sketchbrush.cpp \
    paintingTools/brush/basicbrushv3.cpp \
    paintingTools/brush/basicbrushv3-simd.cpp \
    paintingTools/brush/basic-stamp-simd.cpp \
    widgets/panoramarotator.cpp \
    misc/projectfile.cpp \
    misc/psdexport.cpp


HEADERS  += widgets/mainwindow.h \
    widgets/newprojectdialog.h \
    widgets/canvas.h \
    misc/layermanager.h \
    widgets/colorwheel.h \
    widgets/colorgriditem.h \
    widgets/colorgrid.h \
    widgets/flowlayout.h \
    widgets/layerwidgetheader.h \
    widgets/layerwidget.h \
    widgets/layerlabel.h \
    widgets/layeritem.h \
    widgets/iconcheckbox.h \
    misc/layer.h \
    widgets/colorspinboxgroup.h \
    widgets/colorbox.h \
    widgets/aboutdialog.h \
    common/common.h \
    misc/singleshortcut.h \
    widgets/canvascontainer.h \
    widgets/brushsettingswidget.h \
    widgets/helpdialog.h \
    widgets/panoramaview.h \
    widgets/panoramawidget.h \
    widgets/panoramaslider.h \
    misc/platformextend.h \
    misc/singleton.h \
    misc/call_once.h \
    misc/shortcutmanager.h \
    widgets/configuredialog.h\
    widgets/clearlineedit.h \
    widgets/easycopylineedit.h \
    widgets/gradualbox.h \
    paintingTools/brush/brushmanager.h \
    paintingTools/brush/abstractbrush.h \
    paintingTools/brush/abstractbrushv3.h \
    paintingTools/brush/basic-stamp.h \
    paintingTools/brush/basic-trail.h \
    paintingTools/brush/basic-color-system.h \
    paintingTools/brush/basicbrush.h \
    paintingTools/brush/basiceraser.h \
    paintingTools/brush/binarybrush.h \
    paintingTools/brush/brushfeature.h \
    paintingTools/brush/brushsettings.h \
    paintingTools/brush/maskbased.h \
    paintingTools/brush/sketchbrush.h \
    paintingTools/brush/basicbrushv3.h \
    paintingTools/brush/basicbrushv3-simd.h \
    paintingTools/brush/basic-stamp-simd.h \
    widgets/panoramarotator.h \
    misc/projectfile.h \
    misc/psdexport.h

FORMS    += widgets/mainwindow.ui \
    widgets/colorspinboxgroup.ui \
    widgets/colorbox.ui \
    widgets/aboutdialog.ui \
    widgets/helpdialog.ui \
    widgets/configuredialog.ui

TRANSLATIONS += translation/paintty_zh_CN.ts \
    translation/paintty_zh_TW.ts \
    translation/paintty_ja.ts

RESOURCES += resources.qrc

UI_DIR = $$_PRO_FILE_PWD_/widgets
