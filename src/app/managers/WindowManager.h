#pragma once

#include "core/Singleton.h"
#include "widgets/MainWindow.h"

#include <QAction>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class WindowManager : public QObject, public Singleton<WindowManager>
{
    Q_OBJECT
public:
    friend class Singleton<WindowManager>;

    MainWindow*        currentWindow();
    QList<MainWindow*> windows();

    MainWindow* newWindow(bool show = true);

private:
    WindowManager();

    QList<MainWindow*> _windows;
    int                _currentId;
    MainWindow*        _currentWindow;

signals:
    void currentWindowChanged(MainWindow* window);
    void windowCreated(MainWindow* window);
    void windowTerminated(MainWindow* window);
};
