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

/*
Setting::Setting(QString filePath)
{
    _filePath = filePath;
    _mutex    = QSharedPointer<QMutex>::create();
}

Setting::~Setting()
{
    save();
}

void Setting::setValue(QString path, const QVariant& value)
{
    QMutexLocker locker(&*_mutex);
    _reg.setValue(path, value);
}

QVariant& Setting::value(QString path, const QVariant& defaultValue)
{
    QMutexLocker locker(&*_mutex);
    return _reg.value(path, defaultValue);
}

QVariant& Setting::value(QString path)
{
    return value(path, QVariant());
}

QVariant Setting::constValue(QString path, const QVariant& defaultValue) const
{
    QMutexLocker locker(&*_mutex);
    return _reg.constValue(path, defaultValue);
}

QVariant Setting::constValue(QString path) const
{
    return constValue(path, QVariant());
}

Setting Setting::node(QString relPath)
{
    QMutexLocker locker(&*_mutex);
    auto         result = Setting(_filePath);
    result._mutex       = _mutex;
    result._reg         = _reg.node(relPath);
    return result;
}

QVariant Setting::operator[](QString path) const
{
    return constValue(path);
}

QVariant& Setting::operator[](QString path)
{
    return value(path);
}

void Setting::save()
{
    QMutexLocker locker(&*_mutex);

    YAML::Node root = _toYaml(reinterpret_cast<QVariantMap&>(_reg.rootMap()));

    auto file = QFile(_filePath);

    if (file.open(QFile::OpenModeFlag::WriteOnly))
    {
        std::stringstream stream;
        stream << root;

        file.write(QString::fromStdString(stream.str()).toUtf8());
        file.close();
    }
}

void Setting::load(bool clean)
{
    QMutexLocker locker(&*_mutex);

    auto file = QFile(_filePath);

    if (file.open(QFile::OpenModeFlag::ReadOnly))
    {
        auto root = YAML::Load(file.readAll().toStdString());
        file.close();

        if (clean)
        {
            _reg.rootMap().clear();
            _reg.rootMap() = QVariantMap();
        }

        _fromYAML(root, reinterpret_cast<QVariantMap&>(_reg.rootMap()));
    }
}

void Setting::sync()
{
    QMutexLocker locker(&*_mutex);
}
*/

static QSharedPointer<QSettings> _makeCopyQSettings(const QSettings& settings)
{
    QSettings::Format format = settings.format();

    QSharedPointer<QSettings> res;

    if (!settings.fileName().isEmpty())
    {
        res = QSharedPointer<QSettings>::create(settings.fileName(), format);
    }
    else
    {
        res = QSharedPointer<QSettings>::create(settings.organizationName(),
                                                settings.applicationName(),
                                                settings.parent());
    }

    res->setAtomicSyncRequired(settings.isAtomicSyncRequired());
    res->setFallbacksEnabled(settings.fallbacksEnabled());

    res->beginGroup(settings.group());

    return res;
}

FileSettings::FileSettings(QString filePath, QString prefixPath, bool autocreate) :
    _settings(filePath, SettingsManager::YamlFormat), _autocreate(autocreate)
{
    _settings.setAtomicSyncRequired(true);
    _prefix = prefixPath;
};

void FileSettings::_setValue(QString path, const QVariant& v)
{
    _settings.setValue(_prefix / path, v);
}

QVariant FileSettings::_value(QString path, const QVariant& defaultValue)
{
    QString p = _prefix / path;

    if (_settings.contains(p))
    {
        return _settings.value(p);
    }
    else
    {
        if (_autocreate)
        {
            _settings.setValue(p, defaultValue);
        }

        return defaultValue;
    }
}

bool FileSettings::exists(QString path) const
{
    return _settings.contains(_prefix / path);
}

void FileSettings::sync()
{
    _settings.sync();
}

QSettings& FileSettings::settings()
{
    return _settings;
}

ISettings* FileSettings::node(QString prefixPath)
{
    auto res = new FileSettings(_settings.fileName(), _prefix / prefixPath, _autocreate);
    return res;
}

//////////////////////////////////////////////////////////////////

PrioritizedSettings::PrioritizedSettings(QList<ISettings*> descendList)
{
    _descendList = descendList;
}

void PrioritizedSettings::_setValue(QString path, const QVariant& v)
{
    _descendList[0]->_setValue(path, v);
}

QVariant PrioritizedSettings::_value(QString path, const QVariant& defaultValue)
{
    auto p = _prefix / path;
    for (auto it : _descendList)
    {
        if (it->exists(p))
        {
            return it->_value(path, defaultValue);
        }
    }

    return defaultValue;
}

bool PrioritizedSettings::exists(QString path) const
{
    auto p = _prefix / path;
    for (auto it : _descendList)
    {
        if (it->exists(p))
        {
            return true;
        }
    }

    return false;
}

void PrioritizedSettings::sync()
{
    for (auto it : _descendList)
    {
        it->sync();
    }
}

ISettings* PrioritizedSettings::node(QString prefixPath)
{
    auto res     = new PrioritizedSettings(_descendList);
    res->_prefix = prefixPath;
    return res;
}

//////////////////////////////////////////////////////////////////

ProxySettings::ProxySettings(ISettings* settings) : _settings(settings)
{
}

ISettings* ProxySettings::settings() const
{
    return _settings;
}

void ProxySettings::setSettings(ISettings* settings)
{
    _settings = settings;
}

void ProxySettings::_setValue(QString path, const QVariant& v)
{
    if (!_settings)
    {
        qCritical() << "ProxySettings has no pointed object";
    }
    else
    {
        _settings->_setValue(path, v);
    }
}

QVariant ProxySettings::_value(QString path, const QVariant& defaultValue)
{
    if (!_settings)
    {
        qCritical() << "ProxySettings has no pointed object";
        return QVariant();
    }
    else
    {
        return _settings->_value(path, defaultValue);
    }
}

bool ProxySettings::exists(QString path) const
{
    if (!_settings)
    {
        qCritical() << "ProxySettings has no pointed object";
        return false;
    }
    else
    {
        return _settings->exists(path);
    }
}

void ProxySettings::sync()
{
    if (!_settings)
    {
        qCritical() << "ProxySettings has no pointed object";
    }
    else
    {
        _settings->sync();
    }
}

ISettings* ProxySettings::node(QString prefixPath)
{
    if (!_settings)
    {
        qCritical() << "ProxySettings has no pointed object";
        return nullptr;
    }
    else
    {
        return _settings->node(prefixPath);
    }
}

//////////////////////////////////////////////////////////////////

UndoableSettings::UndoableSettings(ISettings* settings) : _settings(settings)
{
}

ISettings* UndoableSettings::settings() const
{
    return _settings;
}

void UndoableSettings::setSettings(ISettings* settings)
{
    _settings = settings;
}

void UndoableSettings::_setValue(QString path, const QVariant& v)
{
    if (!_settings)
    {
        qCritical() << "UndoableSettings has no pointed object";
    }
    else
    {
        _tempValues[path] = v;
    }
}

QVariant UndoableSettings::_value(QString path, const QVariant& defaultValue)
{
    if (!_settings)
    {
        qCritical() << "UndoableSettings has no pointed object";
        return QVariant();
    }
    else
    {
        if (_tempValues.contains(path))
        {
            return _tempValues[path];
        }
        else
        {
            return _settings->_value(path, defaultValue);
        }
    }
}

bool UndoableSettings::exists(QString path) const
{
    if (!_settings)
    {
        qCritical() << "UndoableSettings has no pointed object";
        return false;
    }
    else
    {
        return _tempValues.contains(path) || _settings->exists(path);
    }
}

void UndoableSettings::sync()
{
    if (!_settings)
    {
        qCritical() << "UndoableSettings has no pointed object";
    }
    else
    {
        for (const auto& it : _tempValues.asKeyValueRange())
        {
            _settings->_setValue(it.first, it.second);
        }
        _settings->sync();
        _tempValues.clear();
    }
}

ISettings* UndoableSettings::node(QString prefixPath)
{
    if (!_settings)
    {
        qCritical() << "UndoableSettings has no pointed object";
        return nullptr;
    }
    else
    {
        return _settings->node(prefixPath);
    }
}

//////////////////////////////////////////////////////////////////
/// \brief SettingsManager::YamlFormat
///
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

uint SettingsManager::latestSessionId()
{
    static const auto sessionIdParser = QRegularExpression("session_(\\d+)");

    QDir dir(sessionsDirPath());

    uint id = 0;

    for (const auto& file : dir.entryList(QDir::Filter::Files))
    {
        auto matched = sessionIdParser.match(file);

        if (matched.hasMatch())
        {
            id = qMax(id, matched.captured().toUInt());
        }
    }

    return id;
}

uint SettingsManager::latestTerminalId()
{
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

static bool _writeYamlFile(QIODevice& device, const QSettings::SettingsMap& map)
{
    Registree reg;

    for (auto it : map.asKeyValueRange())
    {
        reg[it.first] = it.second;
    }

    YAML::Node root = _toYaml(reinterpret_cast<QVariantMap&>(reg.rootMap()));

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
