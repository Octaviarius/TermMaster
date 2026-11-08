#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVariant>
#include <memory>

/** Immutable, validated Draft 7 descriptor for a generated settings form. */
class SettingsSchema
{
public:
    struct Field
    {
        QString     key;
        QString     settingsPath;
        QString     type;
        QString     title;
        QString     description;
        QString     widget;
        QString     group;
        int         order      = 0;
        bool        required   = false;
        bool        readOnly   = false;
        bool        hasDefault = false;
        QVariant    defaultValue;
        QVariantMap definition;
        QVariantMap presentation;
    };
    struct Group
    {
        QString id;
        QString title;
        int     order = 0;
    };
    struct Error
    {
        QString path;
        QString message;
    };

    explicit SettingsSchema(const QByteArray& yamlDescriptor);
    ~SettingsSchema();
    Q_DISABLE_COPY_MOVE(SettingsSchema)

    const QList<Field>& fields() const;
    const QList<Group>& groups() const;
    QList<Error>        validate(const QVariantMap& document) const;
    /** Parses one bounded YAML value, preserving quoted strings and numeric types. */
    static QVariant   parseValue(const QByteArray& yaml);
    static QByteArray serializeValue(const QVariant& value);

private:
    struct Data;
    std::unique_ptr<Data> _data;
};
