#pragma once

#include <QList>
#include <QObject>
#include <Termaster/Core/Singleton.h>
#include <Termaster/Managers/WindowManager.h>

class GeneralLogic : public QObject, public Singleton<GeneralLogic>
{
    Q_OBJECT
public:
    friend class Singleton<GeneralLogic>;

private:
    GeneralLogic();
    QList<QAction*> _recentSessions;

    void _updateRecentSessions();

private slots:
    void onWindowCreated(MainWindow* window);
    void onWindowTerminated(MainWindow* window);
};
