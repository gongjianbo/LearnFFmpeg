#pragma once
#include <QMainWindow>
#include <QImage>
#include <QPixmap>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    QImage getFrameImage(const QString &filepath, int pos);

private:
    Ui::MainWindow *ui;
};
