#pragma once

#include <Termaster/Core/Path.h>
#include <optional>

/** Resolves application-owned paths without creating directories or opening files. */
class ApplicationPaths
{
public:
    /** Requires an initialized QCoreApplication for application-local defaults. */
    ApplicationPaths();
    explicit ApplicationPaths(Path dataDirectory);
    ApplicationPaths(Path dataDirectory, Path pluginsDirectory);

    Path dataDirectory() const;
    Path configDirectory() const;
    Path logsDirectory() const;
    Path pluginsDirectory() const;

    /** Resolves a single filename, adding .yaml unless .yaml or .yml is present. */
    Path configFile(const QString& name) const;

    /** Rebases derived defaults while retaining explicitly overridden directories. */
    void setDataDirectory(const Path& directory);
    void setConfigDirectory(const Path& directory);
    void setLogsDirectory(const Path& directory);
    void setPluginsDirectory(const Path& directory);

private:
    Path                _dataDirectory;
    std::optional<Path> _configDirectory;
    std::optional<Path> _logsDirectory;
    std::optional<Path> _pluginsDirectory;
    Path                _defaultPluginsDirectory;

    static QString _yamlName(const QString& name);
};
