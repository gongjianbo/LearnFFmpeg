#include <QApplication>
#include "MainWindow.h"
extern "C" {
#include "libavutil/avutil.h"
}

int main(int argc, char *argv[])
{
    fprintf(stderr, "ffmpeg version:%s\n", av_version_info());
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
