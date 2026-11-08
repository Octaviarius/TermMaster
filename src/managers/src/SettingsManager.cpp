#include "yaml-cpp/yaml.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <Termaster/Managers/SettingsManager.h>
#include <stdexcept>

static bool               _readYamlFile(QIODevice& device, QSettings::SettingsMap& map);
static bool               _writeYamlFile(QIODevice& device, const QSettings::SettingsMap& map);
static QSettings::Format& _initSettingsYaml();

namespace
{
bool readPluginDocument(const QString& path, YAML::Node& root, QHash<QString, bool>& states, QString* error);
} // namespace

FileSettings::FileSettings(QString filePath, QString prefixPath, bool autocreate) :
    Settings(filePath, SettingsManager::YamlFormat)
{
    setAutoCreate(autocreate);
    beginGroup(prefixPath);
}

//////////////////////////////////////////////////////////////////

const QSettings::Format SettingsManager::YamlFormat = _initSettingsYaml();

SettingsManager::SettingsManager(QString pluginSettingsPath) :
    _pluginSettingsPath(pluginSettingsPath.isEmpty() ? (appDataDirPath() / "plugins.yaml").toString() :
                                                       pluginSettingsPath)
{
}

bool SettingsManager::loadPluginStates(QHash<QString, bool>& states, QString* error)
{
    if (error)
    {
        error->clear();
    }
    YAML::Node           document;
    QHash<QString, bool> result;
    if (!readPluginDocument(_pluginSettingsPath, document, result, error))
    {
        return false;
    }
    states = result;
    return true;
}

bool SettingsManager::savePluginState(const QString& id, bool enabled, QString* error)
{
    if (error)
    {
        error->clear();
    }
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$").match(id).hasMatch())
    {
        if (error)
        {
            *error = "Invalid plugin ID";
        }
        return false;
    }
    YAML::Node           document;
    QHash<QString, bool> states;
    if (!readPluginDocument(_pluginSettingsPath, document, states, error))
    {
        return false;
    }
    try
    {
        document["enabled"][id.toStdString()] = enabled;
        const auto bytes                      = QByteArray::fromStdString(YAML::Dump(document));
        if (bytes.size() > 4 * 1024 * 1024)
        {
            throw std::runtime_error("Settings exceed 4 MiB");
        }
        if (!QDir().mkpath(QFileInfo(_pluginSettingsPath).absolutePath()))
        {
            throw std::runtime_error("Cannot create settings directory");
        }
        QSaveFile file(_pluginSettingsPath);
        if (!file.open(QIODevice::WriteOnly))
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        if (file.write(bytes) != bytes.size())
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        if (!file.commit())
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString("Cannot write %1: %2").arg(_pluginSettingsPath, QString::fromUtf8(exception.what()));
        }
        return false;
    }
}

Path SettingsManager::appDataDirPath()
{
    return Path::appData();
}

Path SettingsManager::sessionsDirPath()
{
    return appDataDirPath() / "sessions";
}

Path SettingsManager::terminalsDirPath()
{
    return appDataDirPath() / "terminals";
}

Path SettingsManager::generalSettingsFilePath()
{
    return appDataDirPath() / "general.yaml";
}

FileSettings* SettingsManager::applicationSettings(QString fileName, QString nodePath)
{
    return new FileSettings((appDataDirPath() / fileName).toString(), nodePath);
}

FileSettings* SettingsManager::environment(const Path& path)
{
    return new FileSettings(path.toString());
}

FileSettings* SettingsManager::generalSettings(QString nodePath)
{
    return new FileSettings(generalSettingsFilePath().toString(), nodePath);
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

    QDir dir(sessionsDirPath().toString());

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

    QDir dir(terminalsDirPath().toString());

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

namespace
{
constexpr qint64 MaxSettingsBytes = 4 * 1024 * 1024;

void validateTree(const YAML::Node& node, qsizetype& remaining, int depth = 0)
{
    if (depth > 64 || --remaining < 0)
    {
        throw std::runtime_error("YAML exceeds nesting or node limit");
    }
    if (node.IsMap())
    {
        QSet<QString> keys;
        for (const auto& entry : node)
        {
            if (!entry.first.IsScalar())
            {
                throw std::runtime_error("YAML mapping keys must be scalar");
            }
            const QString key = QString::fromStdString(entry.first.Scalar());
            if (keys.contains(key))
            {
                throw std::runtime_error("Duplicate YAML mapping key");
            }
            keys.insert(key);
            validateTree(entry.second, remaining, depth + 1);
        }
    }
    else if (node.IsSequence())
    {
        for (const auto& entry : node)
        {
            validateTree(entry, remaining, depth + 1);
        }
    }
}

QVariant fromYaml(const YAML::Node& node, int depth = 0)
{
    if (depth > 64)
    {
        throw std::runtime_error("YAML nesting exceeds 64 levels");
    }
    if (node.IsMap())
    {
        QVariantMap result;
        for (const auto& entry : node)
        {
            result[QString::fromStdString(entry.first.as<std::string>())] = fromYaml(entry.second, depth + 1);
        }
        return result;
    }
    if (node.IsSequence())
    {
        QVariantList result;
        for (const auto& entry : node)
        {
            result.append(fromYaml(entry, depth + 1));
        }
        return result;
    }
    if (node.IsNull())
    {
        return QVariant();
    }
    const auto text = QString::fromStdString(node.as<std::string>());
    if (node.Tag() == "!" || node.Tag() == "tag:yaml.org,2002:str")
    {
        return text;
    }
    if (text.compare("true", Qt::CaseInsensitive) == 0)
    {
        return true;
    }
    if (text.compare("false", Qt::CaseInsensitive) == 0)
    {
        return false;
    }
    bool       ok      = false;
    const auto integer = text.toLongLong(&ok);
    if (ok)
    {
        return integer;
    }
    const auto number = text.toDouble(&ok);
    if (ok)
    {
        return number;
    }
    return text;
}

YAML::Node toYaml(const QVariant& value)
{
    YAML::Node node;
    switch (value.typeId())
    {
        case QMetaType::QVariantMap:
        {
            node           = YAML::Node(YAML::NodeType::Map);
            const auto map = value.toMap();
            for (const auto& entry : map.asKeyValueRange())
            {
                node[entry.first.toStdString()] = toYaml(entry.second);
            }
            break;
        }
        case QMetaType::QVariantList:
        case QMetaType::QStringList:
            node = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& item : value.toList())
            {
                node.push_back(toYaml(item));
            }
            break;
        case QMetaType::Bool:
            node = value.toBool();
            break;
        case QMetaType::Char:
        case QMetaType::Short:
        case QMetaType::Int:
        case QMetaType::LongLong:
            node = value.toLongLong();
            break;
        case QMetaType::UChar:
        case QMetaType::UShort:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
            node = value.toULongLong();
            break;
        case QMetaType::Float:
        case QMetaType::Double:
            node = value.toDouble();
            break;
        default:
            if (value.isValid())
            {
                node = value.toString().toStdString();
                node.SetTag("tag:yaml.org,2002:str");
            }
            break;
    }
    return node;
}

void flatten(const QVariantMap& source, const QString& prefix, QSettings::SettingsMap& target)
{
    for (const auto& entry : source.asKeyValueRange())
    {
        const auto key = prefix + entry.first;
        if (entry.second.typeId() == QMetaType::QVariantMap && !entry.second.toMap().isEmpty())
        {
            flatten(entry.second.toMap(), key + '/', target);
        }
        else
        {
            target[key] = entry.second;
        }
    }
}

void insertPath(QVariantMap& map, const QStringList& path, qsizetype index, const QVariant& value)
{
    if (index == path.size() - 1)
    {
        map[path[index]] = value;
        return;
    }
    auto child = map.value(path[index]).toMap();
    insertPath(child, path, index + 1, value);
    map[path[index]] = child;
}

bool readPluginDocument(const QString& path, YAML::Node& root, QHash<QString, bool>& states, QString* error)
{
    try
    {
        QFile file(path);
        if (!file.exists())
        {
            root = YAML::Node(YAML::NodeType::Map);
            return true;
        }
        if (!file.open(QIODevice::ReadOnly))
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        if (file.size() > MaxSettingsBytes)
        {
            throw std::runtime_error("Settings file exceeds 4 MiB");
        }
        const auto data = file.readAll();
        if (file.error() != QFileDevice::NoError)
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        const auto documents = YAML::LoadAll(data.toStdString());
        if (documents.size() != 1 || !documents.front().IsMap())
        {
            throw std::runtime_error("Expected one YAML mapping document");
        }
        root                = documents.front();
        qsizetype remaining = 100000;
        validateTree(root, remaining);
        const auto enabled = static_cast<const YAML::Node&>(root)["enabled"];
        if (enabled.IsDefined())
        {
            if (!enabled.IsMap())
            {
                throw std::runtime_error("'enabled' must be a mapping");
            }
            for (const auto& entry : enabled)
            {
                const auto id    = QString::fromStdString(entry.first.as<std::string>());
                const auto value = entry.second;
                if (!value.IsScalar() || value.Tag() == "!" || value.Tag() == "tag:yaml.org,2002:str")
                {
                    throw std::runtime_error(
                        QString("Plugin '%1' must have a boolean enabled value").arg(id).toStdString());
                }
                const auto scalar = value.Scalar();
                if (scalar != "true" && scalar != "false" && scalar != "True" && scalar != "False" && scalar != "TRUE"
                    && scalar != "FALSE")
                {
                    throw std::runtime_error(
                        QString("Plugin '%1' must have a boolean enabled value").arg(id).toStdString());
                }
                if (states.contains(id))
                {
                    throw std::runtime_error(QString("Duplicate plugin setting '%1'").arg(id).toStdString());
                }
                states.insert(id, value.as<bool>());
            }
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error)
        {
            *error = QString("Cannot read %1: %2").arg(path, QString::fromUtf8(exception.what()));
        }
        return false;
    }
}
} // namespace

static bool _readYamlFile(QIODevice& device, QSettings::SettingsMap& map)
{
    try
    {
        const auto bytes = device.read(MaxSettingsBytes + 1);
        if (bytes.size() > MaxSettingsBytes)
        {
            throw std::runtime_error("Settings file exceeds 4 MiB");
        }
        const auto documents = YAML::LoadAll(bytes.toStdString());
        if (documents.empty())
        {
            return true;
        }
        if (documents.size() != 1 || !documents.front().IsMap())
        {
            throw std::runtime_error("Expected one YAML mapping document");
        }
        qsizetype remaining = 100000;
        validateTree(documents.front(), remaining);
        QSettings::SettingsMap result;
        flatten(fromYaml(documents.front()).toMap(), QString(), result);
        map = result;
        return true;
    }
    catch (const std::exception& exception)
    {
        qCritical() << "Cannot read YAML settings:" << exception.what();
        return false;
    }
}

static bool _writeYamlFile(QIODevice& device, const QSettings::SettingsMap& map)
{
    try
    {
        QVariantMap tree;
        for (const auto& entry : map.asKeyValueRange())
        {
            insertPath(tree, entry.first.split('/'), 0, entry.second);
        }
        const auto document  = toYaml(tree);
        qsizetype  remaining = 100000;
        validateTree(document, remaining);
        const auto serialized = YAML::Dump(document);
        const auto bytes      = QByteArray::fromStdString(serialized);
        if (bytes.size() > MaxSettingsBytes)
        {
            throw std::runtime_error("Settings exceed 4 MiB");
        }
        return device.write(bytes) == bytes.size();
    }
    catch (const std::exception& exception)
    {
        qCritical() << "Cannot write YAML settings:" << exception.what();
        return false;
    }
}

static QSettings::Format& _initSettingsYaml()
{
    static QSettings::Format yamlFormat = QSettings::registerFormat("yaml", _readYamlFile, _writeYamlFile);
    return yamlFormat;
}
