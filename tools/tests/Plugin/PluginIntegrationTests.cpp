#include "core/plugin/PluginManager.h"
#include "managers/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <yaml-cpp/yaml.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class PluginIntegrationTests final : public QObject
{
    Q_OBJECT

private:
    static bool write(const QString& path, const QByteArray& bytes)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
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

private slots:
    void independentSdkConsumer()
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
        SettingsManager settings(temporary.path() + "/plugin.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("externalConsumer", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        QCOMPARE(entry(manager, "externalConsumer").status, PluginManager::Status::Running);
        QVERIFY(manager.stop());
    }

    void cleanup()
    {
        qunsetenv("TERMASTER_PLUGIN_TRACE");
        qunsetenv("TERMASTER_PLUGIN_REFUSE_STOP");
        qunsetenv("TERMASTER_PLUGIN_RUNTIME_FAILURE");
    }
    void runtimeFailureAndManagerRestart()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_Consumer, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/plugin.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("consumer", true));
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

    void missingDependencyAppears()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(!copy(FIXTURE_Consumer, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/plugin.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("consumer", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        QCOMPARE(entry(manager, "consumer").status, PluginManager::Status::Blocked);
        QVERIFY(!copy(FIXTURE_Provider, directory + "/provider/bin").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(entry(manager, "consumer").status, PluginManager::Status::Running, 6000);
        QVERIFY(manager.stop());
    }

    void unrelatedChangesDoNotRetryFailedStartup()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StartFailure, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/plugin.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("startFailure", true));
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

    void genericYamlRoundTrip()
    {
        QTemporaryDir temporary;
        const QString path = temporary.path() + "/general.yaml";
        QVERIFY(write(path, "other:\n  sequence: [alpha, beta]\n  text: 'true'\n"));
        {
            FileSettings settings(path, {}, false);
            QVERIFY(!settings.isAutoCreate());
            QCOMPARE(settings.value<QString>("other/text"), QString("true"));
            QCOMPARE(settings.value<QVariantList>("other/sequence").size(), 2);
            settings.setValue("changed", true);
            settings.sync();
            QCOMPARE(settings.status(), QSettings::NoError);
        }
        const auto document = YAML::Load(read(path).toStdString());
        QVERIFY(document["changed"].as<bool>());
        QCOMPARE(document["other"]["sequence"].size(), size_t(2));
        QCOMPARE(QString::fromStdString(document["other"]["text"].as<std::string>()), QString("true"));
    }
    void dependencyLifecycle()
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
        SettingsManager settings(temporary.path() + "/settings.yaml");
        PluginManager   manager(settings, directory);
        QSignalSpy      started(&manager, &PluginManager::pluginStarted);
        QSignalSpy      stopped(&manager, &PluginManager::pluginStopped);
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
        QHash<QString, bool> states;
        QVERIFY(settings.loadPluginStates(states));
        QVERIFY(states.value("provider"));
        QVERIFY(states.value("consumer"));
        QVERIFY(states.value("leaf"));
    }
    void invalidGraphs()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_ProviderV2, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_Missing, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_CycleA, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_CycleB, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_BadId, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/settings.yaml");
        QVERIFY(settings.savePluginState("missing", true));
        QVERIFY(settings.savePluginState("cycleA", true));
        QVERIFY(settings.savePluginState("cycleB", true));
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
    void failedStartupIsDrained()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StartFailure, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/settings.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("startFailure", true));
        PluginManager manager(settings, directory);
        manager.start();
        QCOMPARE(entry(manager, "startFailure").status, PluginManager::Status::Failed);
        QVERIFY(read(trace).contains("startFailure:stop"));
        QVERIFY(read(trace).contains("startFailure:destroy"));
        QVERIFY(manager.stop());
    }
    void failedShutdownPinsDependencies()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        const QString trace     = temporary.path() + "/trace";
        qputenv("TERMASTER_PLUGIN_TRACE", trace.toLocal8Bit());
        QVERIFY(!copy(FIXTURE_Provider, directory).isEmpty());
        QVERIFY(!copy(FIXTURE_StopFailure, directory).isEmpty());
        SettingsManager settings(temporary.path() + "/settings.yaml");
        QVERIFY(settings.savePluginState("provider", true));
        QVERIFY(settings.savePluginState("stopFailure", true));
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
    void directoryChangesAndReplacement()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QDir().mkpath(directory);
        SettingsManager settings(temporary.path() + "/settings.yaml");
        PluginManager   manager(settings, directory);
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
    void settingsValidationAndPreservation()
    {
        QTemporaryDir        temporary;
        const QString        path = temporary.path() + "/settings.yaml";
        SettingsManager      settings(path);
        QHash<QString, bool> states;
        QString              error;
        QVERIFY(settings.loadPluginStates(states, &error));
        QVERIFY(states.isEmpty());
        for (const QByteArray bytes :
             {QByteArray("enabled: ["), QByteArray("enabled:\n  test: maybe\n"), QByteArray("enabled: []\n")})
        {
            QVERIFY(write(path, bytes));
            QVERIFY(!settings.loadPluginStates(states, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(!settings.savePluginState("test", true, &error));
            QCOMPARE(read(path), bytes);
        }
        QVERIFY(
            write(path, "enabled:\n  unrelated: false\nextra:\n  sequence: [alpha, beta]\n  nested: {answer: 42}\n"));
        QVERIFY(settings.savePluginState("test", true, &error));
        QVERIFY(settings.loadPluginStates(states, &error));
        QCOMPARE(states.value("unrelated", true), false);
        QCOMPARE(states.value("test", false), true);
        const auto document = YAML::Load(read(path).toStdString());
        QCOMPARE(document["extra"]["sequence"].size(), size_t(2));
        QCOMPARE(QString::fromStdString(document["extra"]["sequence"][1].as<std::string>()), QString("beta"));
        QCOMPARE(document["extra"]["nested"]["answer"].as<int>(), 42);
        const QString obstacle = temporary.path() + "/file";
        QVERIFY(write(obstacle, "obstacle"));
        SettingsManager unwritable(obstacle + "/settings.yaml");
        QVERIFY(!unwritable.savePluginState("test", true, &error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_MAIN(PluginIntegrationTests)
#include "PluginIntegrationTests.moc"
