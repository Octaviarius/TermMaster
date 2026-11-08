#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/plugin/PluginManager.h>
#include <yaml-cpp/yaml.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
QByteArray envelope(const QByteArray& content)
{
    QByteArray result("version: 1\ntype: termaster.settings\ndescription: Plugin test preferences\ncontent:\n");
    for (const auto& line : content.split('\n'))
    {
        if (!line.isEmpty())
        {
            result += "  " + line + '\n';
        }
    }
    return result;
}

bool setPreference(Settings& settings, const QString& id, bool enabled)
{
    return settings.load() && settings.setValue("enabled/" + id, enabled) && settings.sync();
}
} // namespace

class PluginIntegrationTests final : public QObject
{
    Q_OBJECT

private:
    static bool write(const QString& path, const QByteArray& bytes)
    {
        QFile      file(path);
        const auto document = envelope(bytes);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(document) == document.size();
    }
    static QString copy(const char* source, const QString& directory, const QString& name = {})
    {
        QDir().mkpath(directory);
        const QString target = directory + "/" + (name.isEmpty() ? QFileInfo(source).fileName() : name);
        return QFile::copy(QString::fromUtf8(source), target) ? target : QString();
    }
    static PluginManager::Entry entry(const PluginManager& manager, const QString& id)
    {
        for (const auto& item : manager.plugins())
        {
            if (item.info.id == id)
            {
                return item;
            }
        }
        return {};
    }
    static QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    Q_SLOT void independentSdkConsumer()
    {
        const QByteArray external = qgetenv("TERMASTER_EXTERNAL_PLUGIN");
        if (external.isEmpty())
        {
            QSKIP("Set TERMASTER_EXTERNAL_PLUGIN to the independently built SDK consumer");
        }

        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(external.constData(), directory).isEmpty());
        Settings settings(temporary.path() + "/plugin.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "externalConsumer", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        QCOMPARE(entry(manager, "externalConsumer").status, PluginManager::Status::Running);
        QVERIFY(manager.stop());
    }

    Q_SLOT void cleanup()
    {
        qunsetenv("TERMASTER_PLUGIN_TRACE");
        qunsetenv("TERMASTER_PLUGIN_REFUSE_STOP");
        qunsetenv("TERMASTER_PLUGIN_RUNTIME_FAILURE");
    }
    Q_SLOT void runtimeFailureAndManagerRestart()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_Consumer, directory).isEmpty());
        Settings settings(temporary.path() + "/plugin.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "consumer", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        qputenv("TERMASTER_PLUGIN_RUNTIME_FAILURE", "1");
        QVERIFY(!manager.rescan());
        QCOMPARE(entry(manager, "provider").status, PluginManager::Status::Failed);
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Blocked);
        qunsetenv("TERMASTER_PLUGIN_RUNTIME_FAILURE");
        QVERIFY(manager.reload("provider"));
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Running);
        QVERIFY(manager.stop());
        QVERIFY(manager.start());
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Running);
        QVERIFY(manager.stop());
    }

    Q_SLOT void missingDependencyAppears()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(!copy(FIXTURE_Consumer, directory).isEmpty());
        Settings settings(temporary.path() + "/plugin.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "consumer", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Blocked);
        QVERIFY(!copy(FIXTURE_Provider, directory + "/provider/bin").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(entry(manager, "consumer").status, PluginManager::Status::Running, 6000);
        QVERIFY(manager.stop());
    }

    Q_SLOT void unrelatedChangesDoNotRetryFailedStartup()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StartFailure, directory).isEmpty());
        Settings settings(temporary.path() + "/plugin.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "startFailure", true));
        PluginManager manager(settings, directory);
        QVERIFY(!manager.start());
        QCOMPARE(read(trace).count("startFailure:start"), 1);
        QVERIFY(!copy(FIXTURE_Missing, directory).isEmpty());
        QVERIFY(manager.rescan());
        QVERIFY(manager.setEnabled("missing", true));
        QCOMPARE(read(trace).count("startFailure:start"), 1);
        QCOMPARE(entry(manager, "startFailure").status, PluginManager::Status::Failed);
        QVERIFY(manager.stop());
    }

    Q_SLOT void pluginYamlPreservesScalarTypes()
    {
        QTemporaryDir temporary;
        const QString path = temporary.path() + "/plugins.yaml";
        QVERIFY(write(path, "other:\n  sequence: [alpha, beta]\n  text: 'true'\n"));
        Settings      settings(path);
        PluginManager manager(settings, temporary.filePath("empty"));
        QVERIFY(manager.start());
        QVERIFY(manager.setEnabled("provider", true));
        const auto document = YAML::Load(read(path).toStdString())["content"];
        QVERIFY(document["enabled"]["provider"].as<bool>());
        QCOMPARE(document["other"]["sequence"].size(), size_t(2));
        QCOMPARE(QString::fromStdString(document["other"]["text"].as<std::string>()), QString("true"));
        const QString tag = QString::fromStdString(document["other"]["text"].Tag());
        QVERIFY(tag == "!" || tag == "tag:yaml.org,2002:str");
    }
    Q_SLOT void dependencyLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = temporary.path() + "/plugins";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        const QString provider = copy(FIXTURE_Provider, directory);
        QVERIFY(!provider.isEmpty());
        QVERIFY(!copy(FIXTURE_Consumer, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_Leaf, directory + "/nested").isEmpty());
        Settings      settings(temporary.path() + "/settings.yaml");
        PluginManager manager(settings, directory);
        QSignalSpy    started(&manager, &PluginManager::pluginStarted);
        QSignalSpy    stopped(&manager, &PluginManager::pluginStopped);
        QVERIFY(manager.start());
        QCOMPARE(manager.rowCount(), 3);
        QCOMPARE(started.count(), 0);
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Disabled);
        QVERIFY(manager.setEnabled("leaf", true));
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Blocked);
        QVERIFY(manager.setEnabled("consumer", true));
        QVERIFY(manager.setEnabled("provider", true));
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Running);
        QCOMPARE(read(trace), QByteArray("provider:start\nconsumer:start\nleaf:start\n"));
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(reinterpret_cast<LPCWSTR>(provider.utf16())) != nullptr);
#endif
        QVERIFY(manager.setEnabled("provider", false));
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Blocked);
        QVERIFY(entry(manager, "consumer").enabled);
        const QByteArray shutdown = read(trace);
        QVERIFY(shutdown.indexOf("leaf:stop") < shutdown.indexOf("consumer:stop"));
        QVERIFY(shutdown.indexOf("consumer:stop") < shutdown.indexOf("provider:stop"));
        QVERIFY(shutdown.contains("provider:destroy"));
#ifdef Q_OS_WIN
        QVERIFY(GetModuleHandleW(reinterpret_cast<LPCWSTR>(provider.utf16())) == nullptr);
#endif
        QCOMPARE(stopped.count(), 3);
        QVERIFY(manager.setEnabled("provider", true));
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Running);
        QVERIFY(manager.reload("provider"));
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Running);
        QVERIFY(manager.stop());
        QVariantMap states;
        QVERIFY(settings.load());
        states = settings.value("enabled").toMap();
        QVERIFY(states.value("provider").toBool());
        QVERIFY(states.value("consumer").toBool());
        QVERIFY(states.value("leaf").toBool());
    }
    Q_SLOT void invalidGraphs()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_ProviderV2, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_Missing, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_CycleA, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_CycleB, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_BadId, directory).isEmpty());
        Settings settings(temporary.path() + "/settings.yaml");
        QVERIFY(setPreference(settings, "missing", true));
        QVERIFY(setPreference(settings, "cycleA", true));
        QVERIFY(setPreference(settings, "cycleB", true));
        PluginManager manager(settings, directory);
        manager.start();
        QCOMPARE(manager.plugins().size(), 6);
        for (const auto& item : manager.plugins())
        {
            QVERIFY(item.status != PluginManager::Status::Running);
            QVERIFY2(!item.error.isEmpty(), qPrintable(item.info.id));
        }
        QVERIFY(manager.stop());
    }
    Q_SLOT void failedStartupIsDrained()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StartFailure, directory).isEmpty());
        Settings settings(temporary.path() + "/settings.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "startFailure", true));
        PluginManager manager(settings, directory);
        manager.start();
        QCOMPARE(entry(manager, "startFailure").status, PluginManager::Status::Failed);
        QVERIFY(read(trace).contains("startFailure:stop"));
        QVERIFY(read(trace).contains("startFailure:destroy"));
        QVERIFY(manager.stop());
    }
    Q_SLOT void failedShutdownPinsDependencies()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StopFailure, directory).isEmpty());
        Settings settings(temporary.path() + "/settings.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "stopFailure", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        qputenv("TERMASTER_PLUGIN_REFUSE_STOP", "1");
        QVERIFY(!manager.stop());
        QVERIFY(!read(trace).contains("provider:stop"));
        QVERIFY(!read(trace).contains("provider:destroy"));
        QVERIFY(!read(trace).contains("stopFailure:destroy"));
        qunsetenv("TERMASTER_PLUGIN_REFUSE_STOP");
        QVERIFY(manager.stop());
        QVERIFY(read(trace).contains("provider:destroy"));
        QVERIFY(read(trace).contains("stopFailure:destroy"));
    }
    Q_SLOT void directoryChangesAndReplacement()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QDir().mkpath(directory);
        Settings      settings(temporary.path() + "/settings.yaml");
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        const QString provider = copy(FIXTURE_Provider, directory);
        QVERIFY(!provider.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(manager.rowCount(), 1, 6000);
        QCOMPARE(entry(manager, "provider").info.version, QString("1.0.0"));
        QVERIFY(manager.setEnabled("provider", true));
        QVERIFY(manager.setEnabled("provider", false));
        QVERIFY(QFile::remove(provider));
        QVERIFY(!copy(FIXTURE_ProviderV2, directory, QFileInfo(provider).fileName()).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(entry(manager, "provider").info.version, QString("2.0.0"), 6000);
        QVERIFY(manager.setEnabled("provider", true));
        QCOMPARE(entry(manager, "provider").status, PluginManager::Status::Running);
        QVERIFY(manager.setEnabled("provider", false));
        QVERIFY(QFile::remove(provider));
        QTRY_COMPARE_WITH_TIMEOUT(manager.rowCount(), 0, 6000);
        QVERIFY(manager.stop());
    }
    Q_SLOT void settingsValidationAndPreservation()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath("settings.yaml");
        Settings      settings(path);
        PluginManager manager(settings, temporary.filePath("empty"));
        QString       error;
        for (const QByteArray& bytes : QList<QByteArray> {"enabled: {test: maybe}\n",
                                                          "enabled: []\n",
                                                          "enabled: {test: 'true'}\n",
                                                          "enabled: {test: !!str true}\n"})
        {
            QVERIFY(write(path, bytes));
            QVERIFY(!manager.start(&error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(read(path), envelope(bytes));
            QVERIFY(!manager.isActive());
        }
        QVERIFY(write(path, "enabled: {unrelated: false}\nextra: {sequence: [alpha, beta], answer: 42}\n"));
        QVERIFY(manager.start(&error));
        QVERIFY(manager.setEnabled("test", true, &error));
        auto document = YAML::Load(read(path).toStdString())["content"];
        QVERIFY(!document["enabled"]["unrelated"].as<bool>());
        QVERIFY(document["enabled"]["test"].as<bool>());
        QCOMPARE(document["extra"]["sequence"].size(), size_t(2));
        QCOMPARE(document["extra"]["answer"].as<int>(), 42);
        const auto before = read(path);
        QVERIFY(!manager.setEnabled("bad/id", true, &error));
        QCOMPARE(read(path), before);
        QVERIFY(settings.setValue("pending", 7));
        QVERIFY(!manager.setEnabled("test", false, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value("pending").toInt(), 7);
        QCOMPARE(read(path), before);
        QVERIFY(settings.load());
        QVERIFY(write(path, "other: &shared {provider: false}\nenabled: *shared\n"));
        QVERIFY(manager.setEnabled("provider", true, &error));
        document = YAML::Load(read(path).toStdString())["content"];
        QVERIFY(!document["other"]["provider"].as<bool>());
        QVERIFY(document["enabled"]["provider"].as<bool>());
        QVERIFY(manager.stop());

        const QString blockedPath = temporary.filePath("blocked.yaml");
        Settings      blocked(blockedPath);
        PluginManager blockedManager(blocked, temporary.filePath("empty"));
        QVERIFY(blockedManager.start());
        QVERIFY(QDir().mkdir(blockedPath));
        QVERIFY(!blockedManager.setEnabled("test", true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo(blockedPath).isDir());
        QVERIFY(blockedManager.stop());
    }
};
QTEST_MAIN(PluginIntegrationTests)
#include "PluginIntegrationTests.moc"
