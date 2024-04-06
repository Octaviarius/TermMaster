#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Registree.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
} // namespace Ui
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Registree config, QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow* ui;
    Registree       _config;

private slots:

    void _onActionHelp_Sources();
    void _onActionHelp_Homepage();
    void _onActionHelp_About();
    void _onActionFile_NewSerial();

    void _onActionSetup_Terminal();
    void _onActionSetup_Application();
};
#endif // MAINWINDOW_H
