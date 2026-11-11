#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Logging/Logging.h>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace
{
Q_LOGGING_CATEGORY(flow, "termaster.test.flow", QtInfoMsg)
Q_LOGGING_CATEGORY(flowTrace, "termaster.test.flow.trace", QtInfoMsg)
Q_LOGGING_CATEGORY(otherTrace, "termaster.test.other.trace", QtInfoMsg)
std::atomic<int> restoredMessages {0};
void             previousHandler(QtMsgType, const QMessageLogContext&, const QString&)
{
    ++restoredMessages;
}
QByteArray readLog(const Path& directory)
{
    QFile file((directory / "application.log").toString());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray {};
}
Logger::Settings outputs()
{
    Logger::Settings settings;
    settings.gui  = false;
    settings.file = true;
    return settings;
}
} // namespace

class LoggingTests final : public QObject
{
    Q_OBJECT
private slots:
    void routesLevelsTagsAndDestinationBoundaries()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path user(temporary.filePath("user"));
        const Path workspace(temporary.filePath("workspace"));
        Logging    logging(user);
        auto       settings = outputs();
        QVERIFY(logging.configure(settings, user));
        int traceEvaluations = 0;
        qCDebug(flowTrace) << ++traceEvaluations;
        QCOMPARE(traceEvaluations, 0);
        qCInfo(flow) << "User record";
        QVERIFY(logging.configure(settings, workspace));
        settings.tagLevels["termaster:test:flow"] = Logger::Level::trace;
        QVERIFY(logging.configure(settings, workspace));
        qCDebug(flowTrace) << "Trace record";
        qCDebug(otherTrace) << "Filtered record";
        std::thread producer([] { qCInfo(flow) << QString::fromUtf8("Worker 雪"); });
        producer.join();
        QVERIFY(logging.flush());
        QCOMPARE(logging.directory(), workspace.absolute());
        const auto userBytes      = readLog(user);
        const auto workspaceBytes = readLog(workspace);
        QVERIFY(userBytes.contains("User record"));
        QVERIFY(!userBytes.contains("Trace record"));
        QVERIFY(!workspaceBytes.contains("User record"));
        QVERIFY2(workspaceBytes.contains("TRACE termaster:test:flow Trace record"), workspaceBytes.constData());
        QVERIFY(workspaceBytes.contains(QString::fromUtf8("Worker 雪").toUtf8()));
        QVERIFY(!workspaceBytes.contains("Filtered record"));
        settings.tagLevels.clear();
        QVERIFY(logging.configure(settings, workspace));
        qCDebug(flowTrace) << ++traceEvaluations;
        QCOMPARE(traceEvaluations, 0);
    }

    void failedSwitchRetainsPolicyAndDisabledOutputCreatesNothing()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path       user(temporary.filePath("user"));
        const Path       disabled(temporary.filePath("disabled"));
        Logging          logging(user);
        Logger::Settings off;
        off.gui = false;
        QVERIFY(logging.configure(off, disabled));
        qCWarning(flow) << "Not persisted";
        QVERIFY(logging.flush());
        QVERIFY(!QFileInfo::exists(disabled.toString()));
        auto settings = outputs();
        QVERIFY(logging.configure(settings, user));
        QFile occupied(temporary.filePath("occupied"));
        QVERIFY(occupied.open(QIODevice::WriteOnly));
        occupied.write("Keep this file");
        occupied.close();
        QString error;
        QVERIFY(!logging.configure(settings, Path(occupied.fileName()), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(logging.directory(), user.absolute());
        auto invalid             = settings;
        invalid.maximumFileBytes = 1;
        QVERIFY(!logging.configure(invalid, user, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(logging.configure(settings, user, &error));
        QVERIFY(error.isEmpty());
        qCWarning(flow) << "Old destination retained";
        QVERIFY(logging.flush());
        QVERIFY(readLog(user).contains("Old destination retained"));
        QVERIFY(!readLog(user).contains("Not persisted"));
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("Keep this file"));
    }

    void concurrentDeliverySurvivesDirectorySwitches()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path first(temporary.filePath("first"));
        const Path second(temporary.filePath("second"));
        Logging    logging(first);
        const auto settings = outputs();
        QVERIFY(logging.configure(settings, first));
        std::thread producer([] {
            for (int index = 0; index < 200; ++index)
            {
                qCInfo(flow) << "Concurrent route marker" << index;
                std::this_thread::yield();
            }
        });
        bool        configured = true;
        for (int index = 0; index < 10; ++index)
        {
            configured = logging.configure(settings, index % 2 ? first : second) && configured;
        }
        producer.join();
        QVERIFY(configured);
        QVERIFY(logging.flush());
        const auto all = readLog(first) + readLog(second);
        QCOMPARE(all.count("Concurrent route marker"), 200);
    }

    void shutdownDrainsAndRestoresQtHandlerAndCategoryFilter()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const Path user(temporary.path());
        const auto previous = qInstallMessageHandler(previousHandler);
        {
            Logging logging(user);
            auto    settings      = outputs();
            settings.minimumLevel = Logger::Level::trace;
            QVERIFY(logging.configure(settings, user));
            QVERIFY(flowTrace().isDebugEnabled());
            QVERIFY_EXCEPTION_THROWN(Logging duplicate(user), std::logic_error);
            for (int index = 0; index < 100; ++index)
            {
                qCInfo(flow) << "Queued record" << index;
            }
        }
        const auto before = restoredMessages.load();
        qWarning("Restored handler");
        QCOMPARE(restoredMessages.load(), before + 1);
        qInstallMessageHandler(previous);
        QVERIFY(!flowTrace().isDebugEnabled());
        QCOMPARE(readLog(user).count("Queued record"), 100);
    }
};
QTEST_GUILESS_MAIN(LoggingTests)
#include "LoggingTests.moc"
