#include <QCoreApplication>
#include <Termaster/Core/ApplicationPaths.h>
#include <stdexcept>

ApplicationPaths::ApplicationPaths() : ApplicationPaths(Path::appData())
{
}

ApplicationPaths::ApplicationPaths(Path dataDirectory) :
    _dataDirectory(dataDirectory.absolute()),
    _defaultPluginsDirectory((Path(QCoreApplication::applicationDirPath()) / "plugin").absolute())
{
}

ApplicationPaths::ApplicationPaths(Path dataDirectory, Path pluginsDirectory) : ApplicationPaths(dataDirectory)
{
    setPluginsDirectory(pluginsDirectory);
}

Path ApplicationPaths::dataDirectory() const
{
    return _dataDirectory;
}

Path ApplicationPaths::configDirectory() const
{
    return _configDirectory.value_or(_dataDirectory);
}

Path ApplicationPaths::logsDirectory() const
{
    return _logsDirectory.value_or(_dataDirectory / "logs");
}

Path ApplicationPaths::pluginsDirectory() const
{
    return _pluginsDirectory.value_or(_defaultPluginsDirectory);
}

Path ApplicationPaths::configFile(const QString& name) const
{
    return configDirectory() / _yamlName(name);
}

void ApplicationPaths::setDataDirectory(const Path& directory)
{
    _dataDirectory = directory.absolute();
}

void ApplicationPaths::setConfigDirectory(const Path& directory)
{
    _configDirectory = directory.absolute();
}

void ApplicationPaths::setLogsDirectory(const Path& directory)
{
    _logsDirectory = directory.absolute();
}

void ApplicationPaths::setPluginsDirectory(const Path& directory)
{
    _pluginsDirectory = directory.absolute();
}

QString ApplicationPaths::_yamlName(const QString& name)
{
    if (name.isEmpty() || name == "." || name == ".." || name.contains(QChar::Null) || name.contains('/')
        || name.contains('\\') || name.contains(':'))
    {
        throw std::invalid_argument("Settings filename must be a single nonempty filename");
    }

    if (name.endsWith(".yaml", Qt::CaseInsensitive) || name.endsWith(".yml", Qt::CaseInsensitive))
    {
        return name;
    }
    return name + ".yaml";
}
