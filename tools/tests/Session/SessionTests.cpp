#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Session.h>
#include <filesystem>

class SessionTests : public QObject
{
    Q_OBJECT
private slots:
    void layeredSettings()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Settings      workspace(temporary.filePath("workspace.yaml"));
        QVERIFY(base.setValue("font/size", 12));
        QVERIFY(base.setValue("font/family", "mono"));
        QVERIFY(workspace.setValue("font/size", 14));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        QVERIFY(session.create("test-1", "Test"));
        auto font = session.settings().node("font");
        QCOMPARE(font.value("size").toInt(), 14);
        QVERIFY(font.setValue("size", 16));
        QCOMPARE(font.value("size").toInt(), 16);
        QVERIFY(font.remove("size"));
        QVERIFY(workspace.setValue("font/size", 18));
        QCOMPARE(font.value("size").toInt(), 18);
        QCOMPARE(font.value("family").toString(), QString("mono"));
        Settings        overrides(temporary.filePath("fourth.yaml"));
        SettingsOverlay fourth(inherited.node("font"), overrides.node(""));
        QCOMPARE(fourth.value("size").toInt(), 18);
        QVERIFY(fourth.setValue("size", 20));
        QCOMPARE(fourth.value("size").toInt(), 20);
    }

    void roundTripAndFailedValidation()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        const Path      directory(temporary.filePath("session"));
        Session         session(directory, inherited);
        QString         error;
        QVERIFY2(session.create("stable-id", "Saved session", &error), qPrintable(error));
        const QVariantMap object {
            {     "type",                   "terminal"},
            {   "module",      "unknown.future.module"},
            { "settings", QVariantMap {{"future", 42}}},
            {"extension",                         true}
        };
        QVERIFY(session.objects().setValue("terminal-1", object));
        const QVariantMap endpoint {
            {"object", "terminal-1"},
            {  "port",      "bytes"}
        };
        QVERIFY(session.links().setValue("loop",
                                         QVariantMap {
                                             {"source", endpoint},
                                             {"target", endpoint}
        }));
        QVERIFY(session.resources().setValue("asset", "assets/missing.bin"));
        QVERIFY(session.presentation().setValue("future",
                                                QVariantMap {
                                                    {"layout", 7}
        }));
        QVERIFY2(session.sync(&error), qPrintable(error));
        Session loaded(directory, inherited);
        QVERIFY2(loaded.load(&error), qPrintable(error));
        QCOMPARE(loaded.id(), QString("stable-id"));
        QCOMPARE(loaded.objects().value("terminal-1").toMap(), object);
        QVERIFY(loaded.resourcePath("asset", &error));
        auto borrowed = loaded.objects();
        QVERIFY(loaded.links().setValue("loop/source/object", "missing"));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.isDirty());
        QVERIFY(loaded.load(&error));
        QVERIFY(!borrowed.isValid());
        QVERIFY(loaded.resources().setValue("escape", "../outside"));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(!loaded.resourcePath("escape", &error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.resources().setValue("absolute", temporary.path()));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.resources().setValue("nul", QString("bad") + QChar::Null + "path"));
        QVERIFY(!loaded.resourcePath("nul", &error));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.links().setValue("loop/source/port", "  "));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.objects().setValue("bad id", object));
        QVERIFY(!loaded.sync(&error));
        QVERIFY(loaded.load(&error));
        QVERIFY(loaded.setName("Pending"));
        QFile damaged(loaded.filePath().toString());
        QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(damaged.write("invalid: document") > 0);
        damaged.close();
        QVERIFY(!loaded.load(&error));
        QCOMPARE(loaded.name(), QString("Pending"));
        QVERIFY(loaded.isDirty());
    }

    void noClobberAndWriteFailure()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        const Path      directory(temporary.filePath("session"));
        Session         first(directory, inherited);
        QVERIFY(first.create("first", "First"));
        Session other(directory, inherited);
        QVERIFY(!other.create("second", "Second"));
        QVERIFY(first.load());
        QCOMPARE(first.id(), QString("first"));
        QVERIFY(first.setName("Pending"));
        QVERIFY(QFile::remove(first.filePath().toString()));
        QVERIFY(QDir().mkdir(first.filePath().toString()));
        QVERIFY(!first.sync());
        QVERIFY(first.isDirty());
        QCOMPARE(first.name(), QString("Pending"));
        Session absent(Path(temporary.filePath("missing/child")), inherited);
        QVERIFY(!absent.create("absent", "Absent"));
    }

    void symlinkResourceConfinement()
    {
        QTemporaryDir temporary;
        Settings      base(temporary.filePath("base.yaml"));
        Settings      workspace(temporary.filePath("workspace.yaml"));
        Session       session(Path(temporary.filePath("session")), SettingsOverlay(base.node(""), workspace.node("")));
        QVERIFY(session.create("links", "Links"));
        QVERIFY(session.resources().setValue("asset", "linked/missing.bin"));
        QVERIFY(session.resourcePath("asset"));
        QVERIFY(QDir().mkdir(temporary.filePath("outside")));
        std::error_code error;
        std::filesystem::create_directory_symlink(std::filesystem::path(temporary.filePath("outside").toStdU16String()),
                                                  std::filesystem::path(
                                                      session.directory().join("linked").toString().toStdU16String()),
                                                  error);
        if (error)
        {
            QSKIP("Directory symlink creation is unavailable for this user");
        }
        QVERIFY(!session.resourcePath("asset"));
        QVERIFY(!session.sync());
        QVERIFY(session.isDirty());
        QVERIFY(QFile::remove(session.directory().join("linked").toString()));
    }

    void manifestSymlinkConfinement()
    {
        QTemporaryDir   temporary;
        Settings        base(temporary.filePath("base.yaml"));
        Settings        workspace(temporary.filePath("workspace.yaml"));
        SettingsOverlay inherited(base.node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        Session         foreign(Path(temporary.filePath("foreign")), inherited);
        QVERIFY(session.create("session", "Session"));
        QVERIFY(foreign.create("foreign", "Foreign"));
        QVERIFY(session.setName("Pending"));
        QVERIFY(QFile::remove(session.filePath().toString()));
        std::error_code error;
        std::filesystem::create_symlink(std::filesystem::path(foreign.filePath().toString().toStdU16String()),
                                        std::filesystem::path(session.filePath().toString().toStdU16String()),
                                        error);
        if (error)
        {
            QSKIP("File symlink creation is unavailable for this user");
        }
        QVERIFY(!session.load());
        QCOMPARE(session.name(), QString("Pending"));
        QVERIFY(!session.sync());
        QVERIFY(session.isDirty());
        QVERIFY(foreign.load());
        QCOMPARE(foreign.name(), QString("Foreign"));
        QVERIFY(QFile::remove(session.filePath().toString()));
    }

    void expiredInheritance()
    {
        QTemporaryDir   temporary;
        Settings        workspace(temporary.filePath("workspace.yaml"));
        auto            base = std::make_unique<Settings>(temporary.filePath("base.yaml"));
        SettingsOverlay inherited(base->node(""), workspace.node(""));
        Session         session(Path(temporary.filePath("session")), inherited);
        QVERIFY(session.create("test", "Test"));
        auto settings = session.settings();
        base.reset();
        QVERIFY(!settings.isValid());
        QVERIFY(!settings.setValue("x", 1));
    }
};
QTEST_GUILESS_MAIN(SessionTests)
#include "SessionTests.moc"
