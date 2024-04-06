#include "MainWindow.h"
#include "Registree.h"

#include <QApplication>
#include <QFile>
#include <QSharedPointer>
#include <QVariant>
#include <yaml-cpp/yaml.h>

int main(int argc, char* argv[])
{
    // QFile fil("d:\\WORK\\PROJECT\\EyeSeal\\eyeseal\\manflow\\software\\project\\final.yaml");

    // fil.open(QFile::ReadOnly);
    // auto data = fil.readAll();
    // fil.close();

    // YAML::Node config = YAML::Load((char*)data.data());

    QApplication app(argc, argv);

    Registree rootConfig;

    MainWindow w(rootConfig);
    w.show();
    return app.exec();
}
