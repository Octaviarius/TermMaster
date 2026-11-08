#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Settings.h>
#include <memory>
#include <yaml-cpp/yaml.h>

namespace
{
bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return file.readAll();
}
} // namespace

class SettingsTests : public QObject
{
    Q_OBJECT

private:
    Q_SLOT void roundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settings.yaml");
        Settings      settings(path);
        QString       error;
        QVERIFY2(settings.load(&error), qPrintable(error));
        QVERIFY(!settings.isDirty());
        QCOMPARE(settings.filePath(), path);
        const QVariantMap expected {
            {"boolean",                                      true},
            {"integer",                                        42},
            {   "real",                                      1.25},
            {   "text",                           QString("true")},
            {   "list",  QVariantList {1, QString("false"), true}},
            {"strings",                QStringList {"one", "two"}},
            {    "map", QVariantMap {{"child", QString("value")}}},
            {   "null",                               QVariant {}}
        };
        for (auto it = expected.cbegin(); it != expected.cend(); ++it)
        {
            QVERIFY2(settings.setValue("nested/" + it.key(), it.value(), &error), qPrintable(error));
        }
        QVERIFY(settings.isDirty());
        QVERIFY2(settings.sync(&error), qPrintable(error));
        QVERIFY(!settings.isDirty());

        Settings reloaded(path);
        QVERIFY2(reloaded.load(&error), qPrintable(error));
        for (auto it = expected.cbegin(); it != expected.cend(); ++it)
        {
            QVERIFY(reloaded.contains("nested/" + it.key()));
            const QVariant expectedValue = it.value().typeId() == QMetaType::QStringList ?
                                               QVariant(it.value().toList()) :
                                               it.value();
            QCOMPARE(reloaded.value("nested/" + it.key()), expectedValue);
        }
        QCOMPARE(reloaded.value("nested/text").metaType(), QMetaType::fromType<QString>());
        const QVariantMap before = reloaded.values();
        QCOMPARE(reloaded.value("absent", "fallback").toString(), QString("fallback"));
        QVERIFY(!reloaded.contains("absent"));
        QCOMPARE(reloaded.values(), before);
        QVERIFY(!reloaded.isDirty());
        QVERIFY(!reloaded.setValue("unsupported", QByteArray("bytes"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(reloaded.values(), before);
        QVERIFY(!reloaded.isDirty());
        QVERIFY(reloaded.remove("nested/integer", &error));
        QVERIFY(!reloaded.contains("nested/integer"));
        QVERIFY(reloaded.isDirty());
    }

    Q_SLOT void failedPersistencePreservesState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settings.yaml");
        QVERIFY(writeFile(path, "stable: 7\n"));
        Settings settings(path);
        QString  error;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.setValue("pending", 9, &error));
        const QVariantMap before = settings.values();
        QVERIFY(writeFile(path, "broken: [\n"));
        QVERIFY(!settings.load(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.values(), before);
        QVERIFY(settings.isDirty());

        const QString blockedPath = directory.filePath("blocked");
        QVERIFY(QDir().mkdir(blockedPath));
        const QString sentinel = blockedPath + "/sentinel";
        QVERIFY(writeFile(sentinel, "unchanged"));
        Settings blocked(blockedPath);
        QVERIFY(blocked.setValue("pending", 1, &error));
        QVERIFY(!blocked.sync(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(blocked.isDirty());
        QCOMPARE(blocked.value("pending").toInt(), 1);
        QCOMPARE(readFile(sentinel), QByteArray("unchanged"));
    }

    Q_SLOT void preservesUnrelatedYaml()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settings.yaml");
        QVERIFY(writeFile(path, "unknown: !vendor {quoted: 'true', code: '0012', list: [a, b]}\nknown: 1\n"));
        Settings settings(path);
        QString  error;
        QVERIFY2(settings.load(&error), qPrintable(error));
        QVERIFY(settings.setValue("known", 2, &error));
        QVERIFY2(settings.sync(&error), qPrintable(error));
        const YAML::Node document = YAML::Load(readFile(path).toStdString());
        QCOMPARE(QString::fromStdString(document["unknown"].Tag()), QString("!vendor"));
        QCOMPARE(QString::fromStdString(document["unknown"]["quoted"].Scalar()), QString("true"));
        QCOMPARE(QString::fromStdString(document["unknown"]["code"].Scalar()), QString("0012"));
        QCOMPARE(document["unknown"]["list"].size(), size_t(2));
        Settings reloaded(path);
        QVERIFY(reloaded.load(&error));
        QCOMPARE(reloaded.value("unknown/quoted").metaType(), QMetaType::fromType<QString>());
        QCOMPARE(reloaded.value("unknown/code").toString(), QString("0012"));
    }

    Q_SLOT void rejectsMalformedAndBoundedDocuments()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settings.yaml");
        Settings      settings(path);
        QString       error;
        QVERIFY(settings.setValue("retained", 5, &error));
        const QVariantMap before = settings.values();
        QList<QByteArray> invalid {"key: [\n", "key: 1\nkey: 2\n", "a: 1\n---\nb: 2\n", "[a, b]\n"};
        invalid.append(QByteArray(4 * 1024 * 1024 + 1, ' '));
        invalid.append("deep: " + QByteArray(70, '[') + "0" + QByteArray(70, ']') + "\n");
        QByteArray tooManyNodes("nodes: [");
        for (int i = 0; i < 100001; ++i)
        {
            tooManyNodes += "0,";
        }
        tooManyNodes += "]\n";
        invalid.append(tooManyNodes);
        for (const QByteArray& bytes : invalid)
        {
            QVERIFY(writeFile(path, bytes));
            error.clear();
            QVERIFY(!settings.load(&error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(settings.values(), before);
            QVERIFY(settings.isDirty());
        }
    }

    Q_SLOT void numericTypeBoundaries()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("numbers.yaml");
        QVERIFY(writeFile(path, "huge: 18446744073709551616\n"));
        Settings settings(path);
        QString  error;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.value("huge").metaType(), QMetaType::fromType<QString>());
        QCOMPARE(settings.value("huge").toString(), QString("18446744073709551616"));
        QVERIFY(settings.setValue("wholeFloat", 3.0, &error));
        QVERIFY(settings.sync(&error));
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.value("wholeFloat").metaType(), QMetaType::fromType<double>());
        QCOMPARE(settings.value("wholeFloat").toDouble(), 3.0);
    }

    Q_SLOT void aliasesAndDeepKeys()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("aliases.yaml");
        QVERIFY(writeFile(path, "other: &shared {value: false, nested: {retained: 1}}\nchanged: *shared\n"));
        Settings settings(path);
        QString  error;
        QVERIFY(settings.load(&error));
        auto changed = settings.node("changed");
        QVERIFY(changed.setValue("value", true, &error));
        QCOMPARE(settings.value("other/value").toBool(), false);
        QVERIFY(changed.remove("nested/retained", &error));
        QCOMPARE(settings.value("other/nested/retained").toInt(), 1);
        QVERIFY(settings.sync(&error));
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.value("other/value").toBool(), false);
        QCOMPARE(settings.value("other/nested/retained").toInt(), 1);
        const QVariantMap before = settings.values();
        const QString     deep   = QString("a/").repeated(10000) + "value";
        QVERIFY(!settings.setValue(deep, 1, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.values(), before);
        QVERIFY(!settings.isDirty());
        QVERIFY(!settings.node(deep).isValid());
        auto prefix = settings.node(QString("a/").repeated(63) + "a");
        QVERIFY(prefix.isValid());
        QVERIFY(!prefix.setValue("b", 1, &error));
        QCOMPARE(settings.values(), before);
    }

    Q_SLOT void scopedViews()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path     = directory.filePath("settings.yaml");
        auto          settings = std::make_unique<Settings>(path);
        auto          first    = settings->node("first");
        auto          second   = settings->node("second");
        auto          nested   = first.node("nested");
        auto          root     = settings->node("");
        QVERIFY(first.isValid());
        QCOMPARE(first.path(), QString("first"));
        QVERIFY(first.values().isEmpty());
        QVERIFY(!settings->isDirty());
        QVERIFY(settings->values().isEmpty());
        QString error;
        QVERIFY(first.setValue("same", 1, &error));
        QVERIFY(second.setValue("same", 2, &error));
        QVERIFY(nested.setValue("value", 3, &error));
        QCOMPARE(root.value("first/nested/value").toInt(), 3);
        QCOMPARE(first.value("same").toInt(), 1);
        QCOMPARE(second.value("same").toInt(), 2);
        QCOMPARE(first.node("").path(), first.path());
        for (const QString& invalid : QStringList {"/bad", "bad/", "bad//key", ".", "..", "../escape", "a/../b"})
        {
            QVERIFY(!settings->node(invalid).isValid());
            QVERIFY(!first.node(invalid).isValid());
            QVERIFY(!first.setValue(invalid, 8, &error));
            QVERIFY(!error.isEmpty());
        }
        QVERIFY(!first.setValue("", 8, &error));
        QVERIFY(!first.remove("", &error));
        QVERIFY(settings->setValue("scalar", 7, &error));
        QVERIFY(!settings->node("scalar").isValid());
        QVERIFY(!settings->setValue("scalar/child", 8, &error));
        QCOMPARE(settings->value("scalar").toInt(), 7);
        QVERIFY(first.remove("same", &error));
        QVERIFY(!first.contains("same"));
        QCOMPARE(second.value("same").toInt(), 2);
        auto copy = nested;
        QVERIFY(writeFile(path, "first: {nested: {value: 11}}\n"));
        QVERIFY(settings->load(&error));
        QCOMPARE(copy.value("value").toInt(), 11);
        QVERIFY(writeFile(path, "first: 12\n"));
        QVERIFY(settings->load(&error));
        QVERIFY(!nested.isValid());
        QVERIFY(!nested.setValue("value", 15, &error));
        QCOMPARE(settings->value("first").toInt(), 12);
        QVERIFY(writeFile(path, "{}\n"));
        QVERIFY(settings->load(&error));
        QVERIFY(nested.isValid());
        QVERIFY(nested.setValue("value", 16, &error));
        QCOMPARE(copy.value("value").toInt(), 16);
        settings.reset();
        QVERIFY(!copy.isValid());
        QCOMPARE(copy.value("value", 99).toInt(), 99);
        QVERIFY(copy.values().isEmpty());
        QVERIFY(!copy.contains("value"));
        QVERIFY(!copy.setValue("value", 17, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!copy.remove("value", &error));
    }
};

QTEST_GUILESS_MAIN(SettingsTests)
#include "SettingsTests.moc"
