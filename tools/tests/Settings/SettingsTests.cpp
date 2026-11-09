#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <Termaster/Core/Settings.h>
#include <Termaster/Core/SettingsOverlay.h>
#include <cmath>
#include <limits>
#include <memory>
#include <yaml-cpp/yaml.h>

namespace
{
bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray envelope(const QByteArray& content)
{
    QByteArray result("version: 1\ntype: termaster.settings\ndescription: Termaster settings\ncontent:\n");
    for (const auto& line : content.split('\n'))
    {
        result += "  " + line + '\n';
    }
    return result;
}

bool writeDocument(const QString& path, const QByteArray& content)
{
    return writeFile(path, envelope(content));
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

    Q_SLOT void readableTypedYaml()
    {
        QTemporaryDir directory;
        const auto    path = directory.filePath("readable.yaml");
        QVERIFY(writeDocument(path,
                              "settings: {ui: {toolbarVisible: true}}\n"
                              "loaded: ['true', !!str 0012, !!float 1, 18446744073709551616]\n"));
        Settings settings(path);
        QString  error;
        QVERIFY2(settings.load(&error), qPrintable(error));
        const QString      escaped = QString::fromUtf8("Unicode: Привет ") + '\n' + '\"' + '\\' + QChar::Null;
        const QVariantList values {QString("true"),
                                   QString("false"),
                                   QString("null"),
                                   QString("0012"),
                                   QString("1.0"),
                                   QString("1e3"),
                                   QString(""),
                                   escaped,
                                   true,
                                   false,
                                   QVariant::fromValue(std::numeric_limits<qlonglong>::min()),
                                   QVariant::fromValue(std::numeric_limits<qulonglong>::max()),
                                   1.0,
                                   -0.0,
                                   std::numeric_limits<double>::max(),
                                   std::numeric_limits<double>::denorm_min(),
                                   QVariantMap {},
                                   QVariantList {},
                                   QVariant {}};
        QVERIFY2(settings.setValue("values", values, &error), qPrintable(error));
        QVERIFY2(settings.sync(&error), qPrintable(error));
        const auto bytes = readFile(path);
        QVERIFY(!bytes.contains("tag:yaml.org,2002:str"));
        QVERIFY(!bytes.contains("tag:yaml.org,2002:float"));
        QVERIFY(bytes.contains("settings:\n    ui:\n      toolbarVisible: true"));
        QVERIFY(bytes.contains("- 1.0"));
        QVERIFY(bytes.contains("- -0.0"));
        QVERIFY(bytes.contains("- 18446744073709551615"));
        QVERIFY2(settings.load(&error), qPrintable(error));
        const auto actual = settings.value("values").toList();
        QCOMPARE(actual.size(), values.size());
        for (qsizetype index = 0; index < values.size(); ++index)
        {
            QCOMPARE(actual[index].metaType(), values[index].metaType());
            QCOMPARE(actual[index], values[index]);
        }
        QVERIFY(std::signbit(actual[13].toDouble()));
        const auto loaded = settings.value("loaded").toList();
        QCOMPARE(loaded[0].metaType(), QMetaType::fromType<QString>());
        QCOMPARE(loaded[1].toString(), QString("0012"));
        QCOMPARE(loaded[2].metaType(), QMetaType::fromType<double>());
        QCOMPARE(loaded[2].toDouble(), 1.0);
        QCOMPARE(loaded[3].toString(), QString("18446744073709551616"));
        QVERIFY(settings.value("settings/ui/toolbarVisible").toBool());
    }

    Q_SLOT void documentMetadata()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString        path = directory.filePath("typed.yaml");
        const Settings::Info expected {"termaster.workspace", "Workspace settings"};
        Settings             settings(path, expected);
        QString              error;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.version(), 1);
        QCOMPARE(settings.info().type, expected.type);
        QCOMPARE(settings.info().description, expected.description);
        QVERIFY(settings.values().isEmpty());
        QVERIFY(!settings.contains("version"));
        QVERIFY(!settings.contains("type"));
        QVERIFY(!settings.contains("description"));
        QVERIFY(!settings.contains("content"));
        QVERIFY(settings.setValue("value", 1, &error));
        QVERIFY(settings.sync(&error));
        Settings wrongType(path);
        QVERIFY(!wrongType.load(&error));
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.value("value").toInt(), 1);
    }

    Q_SLOT void rejectsInvalidEnvelopes()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("invalid.yaml");
        Settings      settings(path);
        QString       error;
        const QList<QByteArray>
            invalid {"plain: settings\n",
                     "type: termaster.settings\ndescription: settings\ncontent: {}\n",
                     "version: 2\ntype: termaster.settings\ndescription: settings\ncontent: {}\n",
                     "version: '1'\ntype: termaster.settings\ndescription: settings\ncontent: {}\n",
                     "version: true\ntype: termaster.settings\ndescription: settings\ncontent: {}\n",
                     "version: 01\ntype: termaster.settings\ndescription: settings\ncontent: {}\n",
                     "version: 1\ndescription: settings\ncontent: {}\n",
                     "version: 1\ntype: other\ndescription: settings\ncontent: {}\n",
                     "version: 1\ntype: termaster.settings\ncontent: {}\n",
                     "version: 1\ntype: termaster.settings\ndescription: true\ncontent: {}\n",
                     "version: 1\ntype: termaster.settings\ndescription: 18446744073709551616\ncontent: {}\n",
                     "version: 1\ntype: termaster.settings\ndescription: 1e9999\ncontent: {}\n",
                     "version: 1\ntype: termaster.settings\ndescription: settings\n",
                     "version: 1\ntype: termaster.settings\ndescription: settings\ncontent: []\n"};
        QVERIFY(settings.setValue("retained", 9, &error));
        for (const auto& bytes : invalid)
        {
            QVERIFY(writeFile(path, bytes));
            QVERIFY(!settings.load(&error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(settings.value("retained").toInt(), 9);
            QVERIFY(settings.isDirty());
        }
    }

    Q_SLOT void quotedNumericDescription()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("metadata.yaml");
        QVERIFY(writeFile(path,
                          "version: 1\ntype: termaster.settings\n"
                          "description: '18446744073709551616'\ncontent: {}\n"));
        Settings settings(path);
        QString  error;
        QVERIFY2(settings.load(&error), qPrintable(error));
        QCOMPARE(settings.info().description, QString("18446744073709551616"));
        QVERIFY(settings.setValue("value", 1, &error));
        QVERIFY(settings.sync(&error));
        QVERIFY2(settings.load(&error), qPrintable(error));
        QCOMPARE(settings.info().description, QString("18446744073709551616"));
    }

    Q_SLOT void preservesUnknownMetadataAndContentAliases()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("aliases.yaml");
        QVERIFY(writeFile(path,
                          "version: 1\ntype: termaster.settings\ndescription: custom description\n"
                          "vendor: &shared !vendor {value: 7, quoted: 'true'}\ncontent: *shared\n"));
        Settings settings(path);
        QString  error;
        QVERIFY(settings.load(&error));
        QCOMPARE(settings.info().description, QString("custom description"));
        QVERIFY(!settings.contains("vendor"));
        QVERIFY(settings.setValue("value", 8, &error));
        QVERIFY(settings.remove("quoted", &error));
        QVERIFY(settings.sync(&error));
        const auto document = YAML::Load(readFile(path).toStdString());
        QCOMPARE(document["vendor"]["value"].as<int>(), 7);
        QCOMPARE(QString::fromStdString(document["vendor"].Tag()), QString("!vendor"));
        QCOMPARE(QString::fromStdString(document["vendor"]["quoted"].Scalar()), QString("true"));
        QCOMPARE(document["content"]["value"].as<int>(), 8);
        QVERIFY(settings.load(&error));
        QVERIFY(!settings.contains("quoted"));
    }

    Q_SLOT void failedPersistencePreservesState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("settings.yaml");
        QVERIFY(writeDocument(path, "stable: 7\n"));
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
        QVERIFY(writeDocument(path, "unknown: !vendor {quoted: 'true', code: '0012', list: [a, b]}\nknown: 1\n"));
        Settings settings(path);
        QString  error;
        QVERIFY2(settings.load(&error), qPrintable(error));
        QVERIFY(settings.setValue("known", 2, &error));
        QVERIFY2(settings.sync(&error), qPrintable(error));
        const YAML::Node document = YAML::Load(readFile(path).toStdString());
        QCOMPARE(QString::fromStdString(document["content"]["unknown"].Tag()), QString("!vendor"));
        QCOMPARE(QString::fromStdString(document["content"]["unknown"]["quoted"].Scalar()), QString("true"));
        QCOMPARE(QString::fromStdString(document["content"]["unknown"]["code"].Scalar()), QString("0012"));
        QCOMPARE(document["content"]["unknown"]["list"].size(), size_t(2));
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
        invalid.append(envelope("key: 1\nkey: 2\n"));
        invalid.append(envelope("[a, b]\n"));
        invalid.append(QByteArray(4 * 1024 * 1024 + 1, ' '));
        invalid.append(envelope("deep: " + QByteArray(70, '[') + "0" + QByteArray(70, ']') + "\n"));
        QByteArray tooManyNodes("nodes: [");
        for (int i = 0; i < 100001; ++i)
        {
            tooManyNodes += "0,";
        }
        tooManyNodes += "]\n";
        invalid.append(envelope(tooManyNodes));
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
        QVERIFY(writeDocument(path, "huge: 18446744073709551616\n"));
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
        QVERIFY(writeDocument(path, "other: &shared {value: false, nested: {retained: 1}}\nchanged: *shared\n"));
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
        const QString boundary = QString("a/").repeated(63) + "value";
        QVERIFY(settings.setValue(boundary, 1, &error));
        QCOMPARE(settings.value(boundary).toInt(), 1);
        QVERIFY(settings.remove(boundary, &error));
        QVERIFY(settings.load(&error));
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
        QVERIFY(writeDocument(path, "first: {nested: {value: 11}}\n"));
        QVERIFY(settings->load(&error));
        QCOMPARE(copy.value("value").toInt(), 11);
        QVERIFY(writeDocument(path, "first: 12\n"));
        QVERIFY(settings->load(&error));
        QVERIFY(!nested.isValid());
        QVERIFY(!nested.setValue("value", 15, &error));
        QCOMPARE(settings->value("first").toInt(), 12);
        QVERIFY(writeDocument(path, "{}\n"));
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
    void batchPreservesUnknownEnvelopeFields()
    {
        QTemporaryDir directory;
        const auto    path = directory.filePath("metadata.yaml");
        QVERIFY(writeFile(
            path,
            "version: 1\ntype: termaster.settings\ndescription: Custom description\nextra: {retained: true}\ncontent: {unknown: {value: 7}, prior: 1}\n"));
        Settings settings(path);
        QString  error;
        QVERIFY(settings.load(&error));
        QVERIFY(settings.applyValues(
            {
                {"new", 2}
        },
            {"prior"},
            &error));
        QVERIFY(settings.sync(&error));
        const auto document = YAML::Load(readFile(path).toStdString());
        QCOMPARE(document["description"].as<std::string>(), std::string("Custom description"));
        QVERIFY(document["extra"]["retained"].as<bool>());
        QCOMPARE(document["content"]["unknown"]["value"].as<int>(), 7);
        QVERIFY(!document["content"]["prior"].IsDefined());
    }

    void atomicPatches()
    {
        QTemporaryDir directory;
        Settings      settings(directory.filePath("settings.yaml"));
        QString       error;
        QVERIFY(settings.applyValues({}, {}, &error));
        QVERIFY(!settings.isDirty());
        QVERIFY(settings.applyValues(
            {
                {          "keep", 7},
                {"scope/original", 1},
                {         "block", 4}
        },
            {},
            &error));
        QVERIFY(settings.sync(&error));
        const auto before = settings.values();
        QVERIFY(!settings.applyValues(
            {
                {      "fresh", 2},
                {"block/child", 3}
        },
            {"keep"},
            &error));
        QCOMPARE(settings.values(), before);
        QVERIFY(!settings.isDirty());
        QVERIFY(!settings.applyValues(
            {
                {      "fresh",                   2},
                {"unsupported", QByteArray("bytes")}
        },
            {},
            &error));
        QCOMPARE(settings.values(), before);
        QVERIFY(!settings.isDirty());
        QVERIFY(!settings.applyValues(
            {
                {      "scope", 2},
                {"scope-other", 3},
                {"scope/child", 4}
        },
            {},
            &error));
        QVERIFY(!settings.applyValues(
            {
                {"keep", 2}
        },
            {"keep"},
            &error));
        QVERIFY(!settings.applyValues(
            {
                {QString("nul") + QChar::Null, 2}
        },
            {},
            &error));
        QCOMPARE(settings.values(), before);
        QVERIFY(settings.node("scope").applyValues(
            {
                {"fresh", 3}
        },
            {"original"},
            &error));
        QCOMPARE(settings.value("scope/fresh").toInt(), 3);
        QVERIFY(!settings.contains("scope/original"));
        QCOMPARE(settings.value("keep").toInt(), 7);

        Settings        overrides(directory.filePath("overrides.yaml"));
        SettingsOverlay overlay(settings.node(""), overrides.node(""));
        auto            scoped = overlay.node("scope");
        QVERIFY(scoped.applyValues(
            {
                {"fresh",  9},
                {"other", 10}
        },
            {},
            &error));
        QCOMPARE(scoped.value("fresh").toInt(), 9);
        QVERIFY(scoped.containsInherited("fresh"));
        QCOMPARE(scoped.inheritedValue("fresh").toInt(), 3);
        QVERIFY(!scoped.containsInherited("other"));
        QCOMPARE(scoped.inheritedValue("other", 77).toInt(), 77);
        QCOMPARE(settings.value("scope/fresh").toInt(), 3);
        QVERIFY(scoped.applyValues({}, {"fresh"}, &error));
        QCOMPARE(scoped.value("fresh").toInt(), 3);
        const auto overrideBefore = overrides.values();
        QVERIFY(!scoped.applyValues(
            {
                { "valid", 1},
                {"../bad", 2}
        },
            {},
            &error));
        QCOMPARE(overrides.values(), overrideBefore);

        auto owner   = std::make_unique<Settings>(directory.filePath("expired.yaml"));
        auto expired = owner->node("scope");
        owner.reset();
        QVERIFY(!expired.applyValues(
            {
                {"key", 1}
        },
            {},
            &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SettingsTests)
#include "SettingsTests.moc"
