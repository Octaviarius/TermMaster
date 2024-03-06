#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;

private slots:

    void _onActionHelp_Sources();
    void _onActionHelp_Homepage();
    void _onActionHelp_About();
    void _onActionFile_NewSerial();

    void _onActionSetup_Terminal();
};
#endif // MAINWINDOW_H
