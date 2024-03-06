#include "MainWindow.h"

#include "utils.h"
#include <QApplication>
#include <QFile>
#include <yaml-cpp/yaml.h>

PathedValue pv;

int main(int argc, char* argv[])
{
    QFile fil("d:\\WORK\\PROJECT\\EyeSeal\\eyeseal\\manflow\\software\\project\\final.yaml");

    fil.open(QFile::ReadOnly);
    auto data = fil.readAll();
    fil.close();

    YAML::Node config = YAML::Load((char*)data.data());

    switch (config.Type())
    {
        case YAML::NodeType::Map:
            foreach (auto& it, config)
            {
                qDebug() << QString(it.first.Scalar().data());
            }

            break;

        case YAML::NodeType::Sequence:
            break;

        default:
            break;
    }

    auto n = config["name"];

    pv["config/4/size"] = QVariant(456.22);

    QVariant* r = &pv["config/4/size"];
    r           = &pv["config/1/aaa"];
    *r          = 666;
    r           = &pv["config/1/aaa"];

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
