#include "../SettingsVariantFixture.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Help/HelpSource.h>

namespace
{
bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray manifest(const QString& page = QStringLiteral("page.md"), const QJsonArray& assets = {})
{
    const QJsonObject topic {
        {   QStringLiteral("id"), QStringLiteral("guide.page")},
        {QStringLiteral("title"),      QStringLiteral("Guide")},
        { QStringLiteral("file"),                         page}
    };
    const QJsonObject content {
        {QStringLiteral("topics"), QJsonArray {topic}},
        {QStringLiteral("assets"),             assets}
    };
    return QJsonDocument(QJsonObject {
                             {    QStringLiteral("version"),                                1},
                             {       QStringLiteral("type"), QStringLiteral("termaster.help")},
                             {QStringLiteral("description"),      QStringLiteral("Test help")},
                             {    QStringLiteral("content"),                          content}
    })
        .toJson();
}
} // namespace

class HelpSourceTests : public QObject
{
    Q_OBJECT
private slots:
    void atomicReplacementAndRemoval();
    void manifestSnapshotsAndLinks();
    void rejectsUnsafeAndMalformedInput();
    void rejectsSymlinkEscape();
    void boundedSnapshots();
    void ownsBorrowedText();
    void schemaVariantsAndSearch();
};

void HelpSourceTests::atomicReplacementAndRemoval()
{
    HelpSource catalog;
    QSignalSpy changes(&catalog, &HelpSource::changed);
    QString    error;
    QVERIFY(catalog.replaceTopics(QStringLiteral("one"),
                                  {
                                      {"start", "Start", {}, "initial"}
    },
                                  &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!catalog.replaceTopics(QStringLiteral("two"),
                                   {
                                       {"start", "Duplicate", {}, "wrong"}
    },
                                   &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(catalog.topic(QStringLiteral("start"))->markdown, QStringLiteral("initial"));
    QVERIFY(!catalog.replaceTopics(QStringLiteral("one"),
                                   {
                                       {"a", "A", "b", ""},
                                       {"b", "B", "a", ""}
    }));
    QVERIFY(catalog.topic(QStringLiteral("start")));
    QCOMPARE(changes.size(), 1);

    const auto copied = *catalog.topic(QStringLiteral("start"));
    catalog.removeOwner(QStringLiteral("one"));
    QVERIFY(!catalog.topic(QStringLiteral("start")));
    QVERIFY(catalog.search(QStringLiteral("initial")).isEmpty());
    QCOMPARE(copied.markdown, QStringLiteral("initial"));
    QCOMPARE(changes.size(), 2);
}

void HelpSourceTests::manifestSnapshotsAndLinks()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(write(root.filePath("page.md"), "# Offline\n\n[This page](page.md#title) ![Image](image.png)"));
    const QByteArray image("synthetic image bytes\0tail", 26);
    QVERIFY(write(root.filePath("image.png"), image));
    QVERIFY(write(root.filePath("manifest.json"), manifest("page.md", {QStringLiteral("image.png")})));
    QTemporaryDir otherDirectory;
    QVERIFY(otherDirectory.isValid());
    const auto previousDirectory = QDir::currentPath();
    const auto restoreDirectory  = qScopeGuard([previousDirectory] { QDir::setCurrent(previousDirectory); });
    QVERIFY(QDir::setCurrent(otherDirectory.path()));

    HelpSource catalog;
    QString    error;
    QVERIFY2(catalog.loadManifest("module", root.filePath("manifest.json"), &error), qPrintable(error));
    const auto topic    = *catalog.topic("guide.page");
    const auto imageUrl = topic.source.resolved(QUrl("image.png"));
    QCOMPARE(catalog.asset(imageUrl), image);
    QCOMPARE(catalog.topicForUrl(topic.source.resolved(QUrl("page.md#title"))), QStringLiteral("guide.page"));
    QCOMPARE(catalog.topicForUrl(QUrl("help:guide.page")), QStringLiteral("guide.page"));
    QVERIFY(catalog.topicForUrl(QUrl("help:missing")).isEmpty());
    QVERIFY(catalog.asset(topic.source.resolved(QUrl("missing.png"))).isEmpty());

    QVERIFY(QFile::remove(root.filePath("image.png")));
    QVERIFY(QFile::remove(root.filePath("page.md")));
    QCOMPARE(catalog.asset(imageUrl), image);
    QVERIFY(catalog.topic("guide.page")->markdown.contains("Offline"));
    catalog.removeOwner("module");
    QVERIFY(catalog.asset(imageUrl).isEmpty());
    QVERIFY(catalog.topicForUrl(topic.source).isEmpty());
}

void HelpSourceTests::rejectsUnsafeAndMalformedInput()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    HelpSource catalog;
    QVERIFY(catalog.replaceTopics("module",
                                  {
                                      {"retained", "Retained", {}, "unchanged"}
    }));
    const auto path = root.filePath("manifest.json");
    for (const auto& unsafe : {QStringLiteral("../outside.md"),
                               QStringLiteral("/absolute.md"),
                               QStringLiteral("C:/absolute.md"),
                               QStringLiteral("a\\page.md"),
                               QStringLiteral("./page.md"),
                               QString(QChar::Null)})
    {
        QVERIFY(write(path, manifest(unsafe)));
        QVERIFY(!catalog.loadManifest("module", path));
        QVERIFY(catalog.topic("retained"));
    }
    for (const auto& bytes : {QByteArray("{"), QByteArray("[]"), QByteArray("{\"version\":2}")})
    {
        QVERIFY(write(path, bytes));
        QVERIFY(!catalog.loadManifest("module", path));
    }
    QVERIFY(!catalog.loadManifest("module", "manifest.json"));
    QVERIFY(write(root.filePath("page.md"), QByteArray("\xff", 1)));
    QVERIFY(write(path, manifest()));
    QVERIFY(!catalog.loadManifest("module", path));
}

void HelpSourceTests::rejectsSymlinkEscape()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    QVERIFY(write(outside.filePath("outside.md"), "outside"));
    const auto link = root.filePath("link.md");
    if (!QFile::link(outside.filePath("outside.md"), link) || !QFileInfo(link).isSymbolicLink())
    {
        QSKIP("Host does not permit symbolic links");
    }
    QVERIFY(write(root.filePath("manifest.json"), manifest("link.md")));
    HelpSource catalog;
    QVERIFY(!catalog.loadManifest("module", root.filePath("manifest.json")));
}

void HelpSourceTests::boundedSnapshots()
{
    HelpSource catalog;
    QVERIFY(!catalog.replaceTopics("module",
                                   {
                                       {"large", "Large", {}, QString(256 * 1024 + 1, QLatin1Char('x'))}
    }));
    QList<HelpSource::Topic> topics;
    for (int i = 0; i < 1025; ++i)
    {
        topics.append({QStringLiteral("topic.%1").arg(i), QStringLiteral("Title"), {}, {}});
    }
    QVERIFY(!catalog.replaceTopics("module", topics));
    QVERIFY(catalog.topics().isEmpty());
}

void HelpSourceTests::ownsBorrowedText()
{
    HelpSource catalog;
    QString    backing  = QStringLiteral("borrowed text");
    const auto borrowed = QString::fromRawData(backing.constData(), backing.size());
    QVERIFY(catalog.replaceTopics("module",
                                  {
                                      {"copied", borrowed, {}, borrowed}
    }));
    backing.fill(QLatin1Char('x'));
    QCOMPARE(catalog.topic("copied")->title, QStringLiteral("borrowed text"));
    QCOMPARE(catalog.topic("copied")->markdown, QStringLiteral("borrowed text"));
}

void HelpSourceTests::schemaVariantsAndSearch()
{
    const auto reference = HelpSource::schemaReference(settingsVariantFixture());
    QVERIFY(reference.contains("Packet format"));
    QVERIFY(reference.contains("Variants: fixed"));
    QVERIFY(reference.contains("Variants: varint"));
    QVERIFY(reference.contains("maximum: `8`"));
    QVERIFY(reference.contains("Default: `2`"));
    QVERIFY(reference.contains("enum:"));
    QVERIFY(reference.contains("Required: yes"));
    QVERIFY(HelpSource::schemaReference("not a schema").isEmpty());

    HelpSource catalog;
    QVERIFY(catalog.replaceTopics("generated",
                                  {
                                      {"parameters", "Parameter reference", {}, reference}
    }));
    QCOMPARE(catalog.search("PACKET FORMAT").size(), 1);
    QCOMPARE(catalog.search("parameter reference").size(), 1);
    QVERIFY(catalog.search("   ").isEmpty());
    QVERIFY(catalog.search("missing phrase").isEmpty());
}

QTEST_GUILESS_MAIN(HelpSourceTests)
#include "HelpSourceTests.moc"
