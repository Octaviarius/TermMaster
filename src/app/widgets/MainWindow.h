#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/Settings.h"
#include "core/MenubarActionContainer.h"
#include "core/ToolbarActionContainer.h"
#include "managers/SettingsManager.h"

#include <QMainWindow>
#include <QPointer>

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
    friend class WindowManager;

    MainWindow(uint id, QWidget* parent = nullptr);
    ~MainWindow();

    MenubarActionContainer* mainMenu();
    ToolbarActionContainer* toolbar();

    uint id() const;

    Settings* settings();

protected:
    bool event(QEvent* event) override;

private:
    Ui::MainWindow* ui;
    uint            _id;

    MenubarActionContainer* _mainMenuContainer;
    ToolbarActionContainer* _toolbarContainer;
    FileSettings*               _settings;

signals:
    void focusChanged(bool activeState);
};
#endif // MAINWINDOW_H
