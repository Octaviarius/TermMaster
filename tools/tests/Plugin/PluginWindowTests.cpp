#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Gui/Plugin/PluginManagerGui.h>
#include <Termaster/Core/Plugin/PluginManager.h>
#include <Termaster/Core/Settings.h>
#include <memory>

namespace
{
bool setPreference(Settings& settings, const QString& id, bool enabled)
{
    return settings.load() && settings.setValue("enabled/" + id, enabled) && settings.sync();
}
} // namespace

class PluginWindowTests final : public QObject
{
    Q_OBJECT

private:
    static bool copy(const char* source, const QString& directory)
    {
        return QDir().mkpath(directory)
            && QFile::copy(QString::fromUtf8(source), directory + '/' + QFileInfo(source).fileName());
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
    static QModelIndex index(QTableView* table, const QString& id)
    {
        for (int row = 0; row < table->model()->rowCount(); ++row)
        {
            const auto candidate = table->model()->index(row, 0);
            if (candidate.data(PluginManager::IdRole).toString() == id)
            {
                return candidate;
            }
        }
        return {};
    }
    static void select(QTableView* table, const QString& id)
    {
        table->setCurrentIndex(index(table, id));
    }
    static QPushButton* button(QDialog* window, const char* name)
    {
        return window->findChild<QPushButton*>(name);
    }

    Q_SLOT void desiredStateAndDependencyCascade()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(copy(FIXTURE_Provider, directory));
        QVERIFY(copy(FIXTURE_Consumer, directory));
        QVERIFY(copy(FIXTURE_Leaf, directory));
        Settings      settings(temporary.path() + "/settings.yaml");
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto table = window->findChild<QTableView*>("pluginTable");
        QVERIFY(table);
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QCOMPARE(table->model()->index(0, 1).data().toString(), QString("consumer"));
        QCOMPARE(entry(manager, "provider").status, PluginManager::Status::Disabled);
        select(table, "leaf");
        QCOMPARE(window->findChild<QPlainTextEdit*>("pluginReverseDependencies")->toPlainText(), QString("None"));
        QTest::mouseClick(button(window.get(), "enablePlugin"), Qt::LeftButton);
        QTRY_VERIFY(entry(manager, "leaf").enabled);
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Blocked);
        select(table, "consumer");
        QVERIFY(table->model()->setData(index(table, "consumer"), Qt::Checked, Qt::CheckStateRole));
        QTRY_VERIFY(entry(manager, "consumer").enabled);
        select(table, "provider");
        QCOMPARE(window->findChild<QPlainTextEdit*>("pluginReverseDependencies")->toPlainText(), QString("consumer"));
        QCOMPARE(manager.reverseDependencies("provider"), QStringList {"consumer"});
        QTest::mouseClick(button(window.get(), "enablePlugin"), Qt::LeftButton);
        QTRY_COMPARE(entry(manager, "leaf").status, PluginManager::Status::Running);
        QSignalSpy started(&manager, &PluginManager::pluginStarted);
        QSignalSpy stopped(&manager, &PluginManager::pluginStopped);
        QTRY_VERIFY(button(window.get(), "restartPlugin")->isEnabled());
        QTest::mouseClick(button(window.get(), "restartPlugin"), Qt::LeftButton);
        QTRY_COMPARE(started.count(), 3);
        QCOMPARE(stopped.count(), 3);
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Running);
        QTest::mouseClick(button(window.get(), "disablePlugin"), Qt::LeftButton);
        QTRY_COMPARE(entry(manager, "provider").status, PluginManager::Status::Disabled);
        QCOMPARE(entry(manager, "leaf").status, PluginManager::Status::Blocked);
        QVERIFY(entry(manager, "consumer").enabled);
        QVERIFY(entry(manager, "leaf").enabled);
        QVariantMap states;
        QVERIFY(settings.load());
        states = settings.value("enabled").toMap();
        QCOMPARE(states.value("provider").toBool(), false);
        QCOMPARE(states.value("leaf").toBool(), true);
    }

    Q_SLOT void invalidPluginCanBeDisabled()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugin";
        QVERIFY(copy(FIXTURE_Provider, directory));
        QVERIFY(copy(FIXTURE_Provider, directory + "/duplicate"));
        Settings settings(temporary.path() + "/settings.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto table = window->findChild<QTableView*>("pluginTable");
        select(table, "provider");
        QCOMPARE(entry(manager, "provider").status, PluginManager::Status::Invalid);
        QVERIFY(!button(window.get(), "enablePlugin")->isEnabled());
        QVERIFY(button(window.get(), "disablePlugin")->isEnabled());
        QTest::mouseClick(button(window.get(), "disablePlugin"), Qt::LeftButton);
        QTRY_VERIFY(!entry(manager, "provider").enabled);
        QVERIFY(!table->model()->setData(index(table, "provider"), Qt::Checked, Qt::CheckStateRole));
    }

    Q_SLOT void selectionSurvivesDiscoveryAndRefresh()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(copy(FIXTURE_Provider, directory));
        Settings      settings(temporary.path() + "/settings.yaml");
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto table = window->findChild<QTableView*>("pluginTable");
        QVERIFY(table);
        QTRY_COMPARE(table->model()->rowCount(), 1);
        select(table, "provider");
        auto path = window->findChild<QLineEdit*>("pluginPath");
        QVERIFY(path);
        const QString selectedPath = path->text();
        QVERIFY(!selectedPath.isEmpty());
        QVERIFY(copy(FIXTURE_Missing, directory + "/nested"));
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 2, 6000);
        QTRY_COMPARE(path->text(), selectedPath);
        QVERIFY(copy(FIXTURE_Consumer, directory));
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 3, 6000);
        QTRY_COMPARE(window->findChild<QPlainTextEdit*>("pluginReverseDependencies")->toPlainText(),
                     QString("consumer"));
        QCOMPARE(path->text(), selectedPath);
        QTest::mouseClick(button(window.get(), "refreshPlugins"), Qt::LeftButton);
        QTRY_COMPARE(path->text(), selectedPath);
        QCOMPARE(window->findChild<QLineEdit*>("pluginId")->text(), QString("provider"));
    }

    Q_SLOT void filterClearsActionsAndShowsEmptyState()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(copy(FIXTURE_Consumer, directory));
        Settings      settings(temporary.path() + "/settings.yaml");
        PluginManager manager(settings, directory);
        QVERIFY(manager.start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto table  = window->findChild<QTableView*>("pluginTable");
        auto filter = window->findChild<QLineEdit*>("pluginFilter");
        QVERIFY(table);
        QVERIFY(filter);
        QTRY_COMPARE(table->model()->rowCount(), 1);
        filter->setText("PROVIDER");
        QCOMPARE(table->model()->rowCount(), 1);
        filter->setFocus();
        QTest::keyClick(filter, Qt::Key_Return);
        QVERIFY(window->isVisible());
        QVERIFY(!entry(manager, "consumer").enabled);
        filter->setText("no-such-plugin");
        QTRY_COMPARE(table->model()->rowCount(), 0);
        QVERIFY(!button(window.get(), "enablePlugin")->isEnabled());
        QVERIFY(!button(window.get(), "disablePlugin")->isEnabled());
        QVERIFY(!button(window.get(), "restartPlugin")->isEnabled());
        QVERIFY(window->findChild<QLabel*>("pluginEmptyState")->isVisible());
        QVERIFY(window->findChild<QLineEdit*>("pluginId")->text().isEmpty());
        filter->clear();
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QTRY_COMPARE(window->findChild<QLineEdit*>("pluginId")->text(), QString("consumer"));
    }

    Q_SLOT void concreteDiagnostics()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(copy(FIXTURE_Provider, directory));
        QVERIFY(copy(FIXTURE_Missing, directory));
        QVERIFY(copy(FIXTURE_StartFailure, directory));
        Settings settings(temporary.path() + "/settings.yaml");
        QVERIFY(setPreference(settings, "provider", true));
        QVERIFY(setPreference(settings, "missing", true));
        QVERIFY(setPreference(settings, "startFailure", true));
        PluginManager manager(settings, directory);
        QVERIFY(!manager.start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto table      = window->findChild<QTableView*>("pluginTable");
        auto diagnostic = window->findChild<QPlainTextEdit*>("pluginDiagnostic");
        QVERIFY(table);
        QVERIFY(diagnostic);
        QTRY_COMPARE(table->model()->rowCount(), 3);
        select(table, "missing");
        QTRY_VERIFY(diagnostic->toPlainText().contains("absent"));
        QVERIFY(window->findChild<QPlainTextEdit*>("pluginDependencies")->toPlainText().contains("absent"));
        select(table, "startFailure");
        QTRY_VERIFY(diagnostic->toPlainText().contains("Synthetic startup failure"));
        QCOMPARE(diagnostic->toPlainText(), entry(manager, "startFailure").error);
        QTRY_VERIFY(button(window.get(), "restartPlugin")->isEnabled());
        QTest::mouseClick(button(window.get(), "restartPlugin"), Qt::LeftButton);
        QTRY_VERIFY(window->findChild<QLabel*>("pluginOperationMessage")->text().contains("Synthetic startup failure"));
        const QString capture = qEnvironmentVariable("TERMASTER_PLUGIN_GUI_CAPTURE");
        if (!capture.isEmpty())
        {
            QTest::qWait(100);
            QVERIFY(window->screen()->availableGeometry().contains(window->frameGeometry()));
            QVERIFY2(window->grab().save(capture), qPrintable(capture));
        }
    }

    Q_SLOT void managerDestructionWithQueuedCommand()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(copy(FIXTURE_Provider, directory));
        Settings settings(temporary.path() + "/settings.yaml");
        auto     manager = std::make_unique<PluginManager>(settings, directory);
        QVERIFY(manager->start());
        std::unique_ptr<QDialog> window(createPluginManagerWindow(*manager));
        window->show();
        auto table = window->findChild<QTableView*>("pluginTable");
        QVERIFY(table);
        QTRY_COMPARE(table->model()->rowCount(), 1);
        select(table, "provider");
        QTest::mouseClick(button(window.get(), "enablePlugin"), Qt::LeftButton);
        manager.reset();
        QTRY_COMPARE(table->model()->rowCount(), 0);
        QVERIFY(!button(window.get(), "enablePlugin")->isEnabled());
        QVERIFY(!button(window.get(), "disablePlugin")->isEnabled());
        QVERIFY(!button(window.get(), "restartPlugin")->isEnabled());
        QVERIFY(!button(window.get(), "refreshPlugins")->isEnabled());
        QTRY_VERIFY(window->findChild<QLabel*>("pluginOperationMessage")->text().contains("no longer available"));
    }

    Q_SLOT void emptyFolderAndMalformedSettings()
    {
        QTemporaryDir temporary;
        const QString directory = temporary.path() + "/plugins";
        QVERIFY(QDir().mkpath(directory));
        const QString settingsPath = temporary.path() + "/settings.yaml";
        {
            Settings                 settings(settingsPath);
            PluginManager            manager(settings, directory);
            std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
            window->show();
            QTRY_VERIFY(manager.isActive());
            QCOMPARE(window->findChild<QTableView*>("pluginTable")->model()->rowCount(), 0);
            QVERIFY(window->findChild<QLabel*>("pluginEmptyState")->isVisible());
            QVERIFY(!button(window.get(), "enablePlugin")->isEnabled());
            QCOMPARE(window->findChild<QLineEdit*>("pluginFolder")->text(), directory);
        }
        QFile file(settingsPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("plugins: [unterminated"), qint64(22));
        file.close();
        Settings                 settings(settingsPath);
        PluginManager            manager(settings, directory);
        std::unique_ptr<QDialog> window(createPluginManagerWindow(manager));
        window->show();
        auto message = window->findChild<QLabel*>("pluginOperationMessage");
        QVERIFY(message);
        QTRY_VERIFY(!message->text().isEmpty());
        QVERIFY(message->isVisible());
        QVERIFY(!manager.isActive());
        QVERIFY(!button(window.get(), "enablePlugin")->isEnabled());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(
            file.write(
                "version: 1\ntype: termaster.settings\ndescription: Plugin test preferences\ncontent: {enabled: {}}\n")
            > 0);
        file.close();
        QTest::mouseClick(button(window.get(), "refreshPlugins"), Qt::LeftButton);
        QTRY_VERIFY(manager.isActive());
        QVERIFY(!message->isVisible());
    }
};

QTEST_MAIN(PluginWindowTests)
#include "PluginWindowTests.moc"
