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

    MainWindow* newWindow(int id = -1, bool show = true);

    QList<uint> recentSessions();

private:
    WindowManager();

    QList<MainWindow*> _windows;
    int                _idCounter;
    MainWindow*        _currentWindow;

    QList<uint> _recentSessions;
    uint        _maxHistory;

signals:
    void currentWindowChanged(MainWindow* window);
    void windowCreated(MainWindow* window);
    void windowTerminated(MainWindow* window);
};
