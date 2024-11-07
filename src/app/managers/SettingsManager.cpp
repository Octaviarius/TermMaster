#include "SettingsManager.h"

#include "app/version.h"
#include "core/Registree.h"
#include "yaml-cpp/yaml.h"

#include <QCoreApplication>
#include <QDir>
#include <QIODevice>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>

static void               _fromYAML(const YAML::Node& node, QVariantMap& map);
static YAML::Node         _toYaml(QVariantMap& map);
static void               _setYamlValue(YAML::Node node, const QVariant& value);
static void               _setVariantScalarValue(QVariant& value, const YAML::Node& node);
static bool               _readYamlFile(QIODevice& device, QSettings::SettingsMap& map);
static bool               _writeYamlFile(QIODevice& device, const QSettings::SettingsMap& map);
static QSettings::Format& _initSettingsYaml();

FileSettings::FileSettings(QString filePath, QString prefixPath, bool autocreate) :
    Settings(filePath, SettingsManager::YamlFormat)
{
    setAutoCreate(autocreate);
    beginGroup(prefixPath);
}

//////////////////////////////////////////////////////////////////

const QSettings::Format SettingsManager::YamlFormat = _initSettingsYaml();

PosixPath SettingsManager::appDataDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

PosixPath SettingsManager::sessionsDirPath()
{
    return appDataDirPath() / "sessions";
}

PosixPath SettingsManager::terminalsDirPath()
{
    return appDataDirPath() / "terminals";
}

PosixPath SettingsManager::generalSettingsFilePath()
{
    return appDataDirPath() / "general.yaml";
}

FileSettings* SettingsManager::applicationSettings(QString fileName, QString nodePath)
{
    return new FileSettings(appDataDirPath() / fileName, nodePath);
}

FileSettings* SettingsManager::environment(const PosixPath& path)
{
    QString p = path;
    return new FileSettings(p);
}

FileSettings* SettingsManager::generalSettings(QString nodePath)
{
    return new FileSettings(generalSettingsFilePath(), nodePath);
}

FileSettings* SettingsManager::session(uint id)
{
    return environment(sessionsDirPath() / QString("session_%1.yaml").arg(id));
}

FileSettings* SettingsManager::terminal(uint id)
{
    return environment(terminalsDirPath() / QString("terminal_%1.yaml").arg(id));
}

int SettingsManager::latestSessionId()
{
    static const auto sessionIdParser = QRegularExpression("session_(\\d+)");

    QDir dir(sessionsDirPath());

    uint id = -1;

    for (const auto& file : dir.entryList(QDir::Filter::Files))
    {
        auto matched = sessionIdParser.match(file);

        if (matched.hasMatch())
        {
            id = qMax<int>(id, matched.captured(1).toInt());
        }
    }

    return id;
}

int SettingsManager::latestTerminalId()
{
    static const auto terminalIdParser = QRegularExpression("terminal_(\\d+)");

    QDir dir(terminalsDirPath());

    int id = -1;

    for (const auto& file : dir.entryList(QDir::Filter::Files))
    {
        auto matched = terminalIdParser.match(file);

        if (matched.hasMatch())
        {
            id = qMax<int>(id, matched.captured().toInt());
        }
    }

    return id;
}

ConfigContainer::List SettingsManager::configContainers()
{
    return _configContainers.values();
}

ConfigContainer* SettingsManager::configContainer(QString name)
{
    return _configContainers[name];
}

bool SettingsManager::addConfigContainer(ConfigContainer* container)
{
    if (_configContainers.contains(container->name()))
    {
        qCritical() << QString("Config container '%1' is already exist").arg(container->name());
        return false;
    }
    else
    {
        _configContainers[container->name()] = container;
        return true;
    }
}

//////////////////////////////////////////////////////////////////

static YAML::Node _toYaml(QVariantMap& map)
{
    YAML::Node node;

    for (const auto& it : map.asKeyValueRange())
    {
        const QString& key   = it.first;
        QVariant&      value = it.second;

        switch (value.typeId())
        {
            case QMetaType::Type::QVariantMap:
                node[key.toStdString()] = _toYaml(reinterpret_cast<QVariantMap&>(value));
                break;

            case QMetaType::Type::QVariantList:
                qWarning() << "QVariantList temporary is not supported";
                break;

            default:
                _setYamlValue(node[key.toStdString()], value);
        }
    }

    return node;
}

static void _fromYAML(const YAML::Node& node, QVariantMap& map)
{
    for (YAML::const_iterator it = node.begin(); it != node.end(); ++it)
    {
        const std::string& key   = it->first.as<std::string>();
        const YAML::Node&  value = it->second;

        if (value.IsMap())
        {
            QVariantMap subMap;
            _fromYAML(value, subMap);
            map[QString::fromStdString(key)] = subMap;
        }
        else if (value.IsScalar())
        {
            _setVariantScalarValue(map[QString::fromStdString(key)], value);
        }
    }
}

static void _setYamlValue(YAML::Node node, const QVariant& value)
{
    switch (value.typeId())
    {
        case QMetaType::Type::Char:
        case QMetaType::Type::Short:
        case QMetaType::Type::Int:
            node = value.toInt();
            break;

        case QMetaType::Type::LongLong:
            node = value.toLongLong();
            break;

        case QMetaType::Type::UChar:
        case QMetaType::Type::UShort:
        case QMetaType::Type::UInt:
            node = value.toUInt();
            break;

        case QMetaType::Type::ULongLong:
            node = value.toULongLong();
            break;

        case QMetaType::Type::Float:
        case QMetaType::Type::Double:
            node = value.toDouble();
            break;

        default:
            node = value.toString().toStdString();
            break;
    }
}

static void _setVariantScalarValue(QVariant& value, const YAML::Node& node)
{
    QString scalarValue = QString::fromStdString(node.as<std::string>());

    bool ok;

    // Try to convert to int
    int intValue = scalarValue.toInt(&ok);

    if (ok)
    {
        value = QVariant::fromValue(intValue);
        return;
    }

    // Try to convert to double
    double doubleValue = scalarValue.toDouble(&ok);
    if (ok)
    {
        value = QVariant::fromValue(doubleValue);
        return;
    }

    // Try to convert to bool
    if (scalarValue.compare("true", Qt::CaseInsensitive) == 0 || scalarValue.compare("false", Qt::CaseInsensitive) == 0)
    {
        bool boolValue = scalarValue.compare("true", Qt::CaseInsensitive) == 0;
        value          = QVariant::fromValue(boolValue);
        return;
    }

    // Default to string
    value = QVariant::fromValue(scalarValue);
}

static void _unwrapReg(QStringList& path, QVariantMap& vm, QSettings::SettingsMap& map)
{
    for (auto it : vm.asKeyValueRange())
    {
        path.push_back(it.first);

        if (it.second.typeId() == QMetaType::Type::QVariantMap)
        {
            _unwrapReg(path, reinterpret_cast<QVariantMap&>(it.second), map);
        }
        else
        {
            map[path.join('/')] = it.second;
        }

        path.pop_back();
    }
}

static bool _readYamlFile(QIODevice& device, QSettings::SettingsMap& map)
{
    YAML::Node root;
    try
    {
        root = YAML::Load(device.readAll().toStdString());
    }
    catch (const YAML::Exception& e)
    {
        qCritical() << QString("Error reading YAML: %1").arg(e.what());
        return false;
    }

    Registree reg;
    _fromYAML(root, reinterpret_cast<QVariantMap&>(reg.rootMap()));

    QStringList path;

    _unwrapReg(path, reinterpret_cast<QVariantMap&>(reg.rootMap()), map);

    return true;
}

static void _factorizeMap(const QSettings::SettingsMap& origin, QSettings::SettingsMap& factor)
{
    for (const auto& it : origin.asKeyValueRange())
    {
        QVariantMap* vm = &factor;

        auto path = it.first.split('/');

        for (const auto& p : path)
        {
            if (p == path.last())
            {
                (*vm)[p] = it.second;
            }
            else
            {
                if (!(*vm).contains(p))
                {
                    (*vm)[p] = QVariantMap();
                }
                vm = reinterpret_cast<QVariantMap*>(&(*vm)[p]);
            }
        }
    }
}

static bool _writeYamlFile(QIODevice& device, const QSettings::SettingsMap& map)
{
    QSettings::SettingsMap factor;

    _factorizeMap(map, factor);

    YAML::Node root = _toYaml(factor);

    std::stringstream stream;
    stream << root;

    device.write(stream.str().data());
    return true;
}

static QSettings::Format& _initSettingsYaml()
{
    static QSettings::Format yamlFormat = QSettings::registerFormat("yaml", _readYamlFile, _writeYamlFile);
    return yamlFormat;
}
