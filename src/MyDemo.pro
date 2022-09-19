QT += core
QT += gui
QT += widgets
QT += multimedia
QT += concurrent

#output dir
#CONFIG(debug, debug|release) { }
DESTDIR = $$PWD/../bin

greaterThan(QT_MAJOR_VERSION, 4) {
    TARGET_ARCH=$${QT_ARCH}
} else {
    TARGET_ARCH=$${QMAKE_HOST.arch}
}

contains(TARGET_ARCH, x86_64) {
#only x64 msvc
win32 {
include($$PWD/../ProCommon.pri)

LIBS += $$PWD/../3rd/ffmpeg/lib/*.lib
INCLUDEPATH += $$PWD/../3rd/ffmpeg/include
DEPENDPATH += $$PWD/../3rd/ffmpeg/include
}
}

SOURCES += \
    main.cpp \
    MainWindow.cpp

HEADERS += \
    MainWindow.h

FORMS += \
    MainWindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
