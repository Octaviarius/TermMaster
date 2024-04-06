#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "utils/Registree.h"
#include "modules/core/SettingsManager.h"

#include <QList>
#include <QMainWindow>
#include <QMap>

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

    void addMenu(QAction* menu);

private:
    Ui::MainWindow* ui;
    SettingsProxy   _config;
};
#endif // MAINWINDOW_H
