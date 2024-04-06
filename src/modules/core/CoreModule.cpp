#include "CoreModule.h"

#include <QCoreApplication>

bool CoreModule::startup()
{
    qDebug() << "CoreModule loaded!";
    return true;
}

bool CoreModule::shutdown()
{
    qDebug() << "CoreModule unloaded...";
    return true;
}
