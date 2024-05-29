#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/Registree.h"
#include "core/SettingsManager.h"

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
    MainWindow(SettingsProxy config, QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow* ui;
    SettingsProxy   _config;
};
#endif // MAINWINDOW_H
