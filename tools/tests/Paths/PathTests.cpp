#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <Termaster/Core/Path.h>
#include <Termaster/Core/PurePath.h>
#include <stdexcept>

class PathTests final : public QObject
{
    Q_OBJECT

private:
    Q_SLOT void normalization_data();
    Q_SLOT void normalization();
    Q_SLOT void rootsAndJoins();
    Q_SLOT void lexicalRelations();
    Q_SLOT void lexicalRoundTrips();
    Q_SLOT void extensions_data();
    Q_SLOT void extensions();
    Q_SLOT void explicitFlavours();
    Q_SLOT void invalidInputs();
    Q_SLOT void filesystemOperations();
};

void PathTests::normalization_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<QStringList>("parts");
    QTest::newRow("empty") << QString() << QString(".") << QStringList();
    QTest::newRow("dot") << QString("./.") << QString(".") << QStringList();
    QTest::newRow("separators") << QString("a//./b///") << QString("a/b") << QStringList({"a", "b"});
    QTest::newRow("parent-preserved") << QString("a/../b") << QString("a/../b") << QStringList({"a", "..", "b"});
}

void PathTests::normalization()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QFETCH(QStringList, parts);
    const PurePath path = PurePath::posix(input);
    QCOMPARE(path.toString(), expected);
    QCOMPARE(path.parts(), parts);
    QCOMPARE(PurePath::windows(input).parts(), parts);
}

void PathTests::rootsAndJoins()
{
    const PurePath root = PurePath::posix("/");
    QCOMPARE(root.root(), QString("/"));
    QVERIFY(root.parent() == root);
    QVERIFY(PurePath::posix("/a").parent() == root);
    QCOMPARE(PurePath::posix("a").parent().toString(), QString("."));
    QCOMPARE(PurePath::posix("//host/a").root(), QString("//"));
    QCOMPARE(PurePath::posix("///host/a").root(), QString("/"));
    QCOMPARE(PurePath::posix("/a").join("/b").toString(), QString("/b"));

    const PurePath drive = PurePath::windows("C:/a/b");
    QCOMPARE(drive.drive(), QString("C:"));
    QCOMPARE(drive.root(), QString("\\"));
    QCOMPARE(drive.anchor(), QString("C:\\"));
    QVERIFY(drive.isAbsolute());
    QVERIFY(!PurePath::windows("C:a").isAbsolute());
    QVERIFY(!PurePath::windows("/a").isAbsolute());
    QCOMPARE(drive.join("/c").toString(), QString("C:\\c"));
    QCOMPARE(drive.join("C:c").toString(), QString("C:\\a\\b\\c"));
    QCOMPARE(drive.join("D:c").toString(), QString("D:c"));
    QCOMPARE(PurePath::windows("C:a").join("b").toString(), QString("C:a\\b"));
    const PurePath driveRoot = PurePath::windows("C:/");
    QVERIFY(driveRoot.parent() == driveRoot);

    const PurePath unc = PurePath::windows("//server/share/a");
    QCOMPARE(unc.drive(), QString("\\\\server\\share"));
    QCOMPARE(unc.root(), QString("\\"));
    QVERIFY(unc.isAbsolute());
    const PurePath uncRoot = unc.parent();
    QVERIFY(uncRoot.parent() == uncRoot);
    QCOMPARE(uncRoot.anchor(), QString("\\\\server\\share\\"));
}

void PathTests::lexicalRelations()
{
    QVERIFY(PurePath::windows("C:/Users/FILE") == PurePath::windows("c:/users/file"));
    QVERIFY(!(PurePath::posix("/Users/FILE") == PurePath::posix("/users/file")));
    const auto relative = PurePath::windows("C:/Users/FILE").relativeTo(PurePath::windows("c:/users"));
    QVERIFY(relative.has_value());
    QCOMPARE(relative->toString(), QString("FILE"));
    QVERIFY(!PurePath::posix("/Users/file").relativeTo(PurePath::posix("/users")));
    QVERIFY(!PurePath::posix("/alphabet/file").relativeTo(PurePath::posix("/alpha")));
    QVERIFY(!PurePath::posix("/a/c").relativeTo(PurePath::posix("/a/b")));
    QVERIFY(!PurePath::posix("/a").relativeTo(PurePath::posix("/a/b")));
    const auto same = PurePath::posix("/a").relativeTo(PurePath::posix("/a"));
    QVERIFY(same.has_value());
    QCOMPARE(same->toString(), QString("."));
    const auto unresolved = PurePath::posix("a/../b").relativeTo(PurePath::posix("a"));
    QVERIFY(unresolved.has_value());
    QCOMPARE(unresolved->toString(), QString("../b"));
    QVERIFY(!PurePath::windows("D:/a").relativeTo(PurePath::windows("C:/")));
    QVERIFY(!PurePath::posix("a").relativeTo(PurePath::windows("a")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PurePath::posix("a").join(PurePath::windows("b")));
}

void PathTests::lexicalRoundTrips()
{
    for (const QString& input : QStringList({"",
                                             "/",
                                             "//host/share",
                                             "C:",
                                             "C:notes",
                                             "C:/notes",
                                             "./c:notes",
                                             "a/c:notes",
                                             "//server/share/a",
                                             "//?/UNC/server/share/a",
                                             "./..",
                                             "a/../b"}))
    {
        for (const auto syntax : {PurePath::Syntax::Posix, PurePath::Syntax::Windows})
        {
            const PurePath value(input, syntax);
            QVERIFY(PurePath(value.toString(), syntax) == value);
            QVERIFY(PurePath(value.parent().toString(), syntax) == value.parent());
        }
    }
    const PureWindowsPath value("a/c:notes");
    const auto            relative = value.relativeTo(PureWindowsPath("a"));
    QVERIFY(relative);
    QVERIFY(PureWindowsPath(relative->toString()) == *relative);
    QVERIFY(!relative->isAbsolute());
    QVERIFY(relative->drive().isEmpty());
    PurePath original = PurePath::posix("a");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, original /= PureWindowsPath("b"));
    QVERIFY(original == PurePosixPath("a"));
    const QString name = QString::fromUtf8("каталог/日本語.txt");
    QVERIFY(PurePosixPath(name).name() == QString::fromUtf8("日本語.txt"));
}

void PathTests::extensions_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("stem");
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<QStringList>("suffixes");
    QTest::newRow("archive") << QString("archive.tar.gz") << QString("archive.tar") << QString(".gz")
                             << QStringList({".tar", ".gz"});
    QTest::newRow("dotfile") << QString(".profile") << QString(".profile") << QString() << QStringList();
    QTest::newRow("dotfile-extension") << QString(".config.json") << QString(".config") << QString(".json")
                                       << QStringList({".json"});
    QTest::newRow("trailing-dot") << QString("file.") << QString("file") << QString(".") << QStringList({"."});
    QTest::newRow("multiple-leading-dots")
        << QString("..profile") << QString("..profile") << QString() << QStringList();
    QTest::newRow("only-dots") << QString("...") << QString("...") << QString() << QStringList();
    QTest::newRow("parent-name") << QString("..") << QString("..") << QString() << QStringList();
    QTest::newRow("plain") << QString("file") << QString("file") << QString() << QStringList();
}

void PathTests::extensions()
{
    QFETCH(QString, name);
    QFETCH(QString, stem);
    QFETCH(QString, suffix);
    QFETCH(QStringList, suffixes);
    for (const auto syntax : {PurePath::Syntax::Posix, PurePath::Syntax::Windows})
    {
        const PurePath path("folder/" + name, syntax);
        QCOMPARE(path.name(), name);
        QCOMPARE(path.stem(), stem);
        QCOMPARE(path.suffix(), suffix);
        QCOMPARE(path.suffixes(), suffixes);
    }
}

void PathTests::explicitFlavours()
{
    const PurePosixPath   posix("/menu/settings");
    const PureWindowsPath windows("C:/Users/example");
    QCOMPARE(posix.syntax(), PurePath::Syntax::Posix);
    QCOMPARE(windows.syntax(), PurePath::Syntax::Windows);
    QVERIFY(posix.parent() == PurePosixPath("/menu"));
    QVERIFY(windows.parent() == PureWindowsPath("C:/Users"));
    QVERIFY((posix / "appearance") == PurePosixPath("/menu/settings/appearance"));
    QVERIFY((windows / "config") == PureWindowsPath("C:/Users/example/config"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, posix.join(windows));
}

void PathTests::invalidInputs()
{
    QString embeddedNul("a");
    embeddedNul.append(QChar(0));
    embeddedNul.append("b");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PurePath::posix(embeddedNul));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, PurePath::windows(embeddedNul));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, (void)Path(embeddedNul));
    const auto foreign = PurePath::nativeSyntax() == PurePath::Syntax::Windows ? PurePath::posix("a") :
                                                                                 PurePath::windows("a");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, (void)Path(foreign));
}

void PathTests::filesystemOperations()
{
    QTemporaryDir temporary(QDir::current().filePath("PathTests-XXXXXX"));
    QVERIFY(temporary.isValid());
    const Path base(temporary.path());
    const Path directory = base / "nested/child";
    QString    error("stale");
    // Exercise relative creation without changing the process working directory.
    const Path relative(QDir::current().relativeFilePath(directory.toString()));
    QVERIFY(!relative.isAbsolute());
    QVERIFY2(relative.createDirectories(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(directory.exists());
    QVERIFY(directory.isDirectory());
    QVERIFY(!directory.isFile());
    QVERIFY(relative.absolute() == directory.absolute());

    const Path file = directory / "data.txt";
    QVERIFY2(file.createFile(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(file.exists());
    QVERIFY(file.isFile());
    QVERIFY(!file.isDirectory());
    QFile content(file.toString());
    QVERIFY(content.open(QIODevice::WriteOnly));
    QCOMPARE(content.write("retained"), qint64(8));
    content.close();
    error = "stale";
    QVERIFY2(file.createFile(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(content.open(QIODevice::ReadOnly));
    QCOMPARE(content.readAll(), QByteArray("retained"));
    content.close();

    const Path missing = base / "missing/file.txt";
    QVERIFY(!missing.createFile(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!missing.parent().exists());
    QVERIFY(!missing.canonical(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!(file / "blocked/child").createDirectories(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!directory.createFile(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(directory.createDirectories(&error));
    QVERIFY(error.isEmpty());

    error                = "stale";
    const auto canonical = file.canonical(&error);
    QVERIFY2(canonical.has_value(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(canonical->isAbsolute());
    QVERIFY(canonical->isFile());
    QVERIFY(canonical->canonical().has_value());
    QVERIFY(*canonical == *canonical->canonical());
}

QTEST_GUILESS_MAIN(PathTests)
#include "PathTests.moc"
