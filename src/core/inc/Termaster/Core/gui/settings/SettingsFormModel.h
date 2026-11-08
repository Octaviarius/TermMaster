#pragma once
#include <QObject>
#include <QSet>
#include <Termaster/Core/SettingsOverlay.h>
#include <Termaster/Core/SettingsSchema.h>
#include <functional>

/** Draft configuration only. Apply updates the borrowed scope atomically without file I/O. */
class SettingsFormModel : public QObject
{
    Q_OBJECT
public:
    SettingsFormModel(std::shared_ptr<const SettingsSchema> schema, Settings::Node target, QObject* parent = nullptr);
    SettingsFormModel(std::shared_ptr<const SettingsSchema> schema, SettingsOverlay target, QObject* parent = nullptr);
    const SettingsSchema&        schema() const;
    QVariant                     value(const QString& key) const;
    bool                         contains(const QString& key) const;
    void                         setValue(const QString& key, const QVariant& value);
    void                         removeValue(const QString& key);
    QList<SettingsSchema::Error> errors() const;
    bool                         isDirty() const;
    bool                         isValid() const;
    void                         reload();
    bool                         apply(QString* error = nullptr);
    void                         setInputError(const QString& key, const QString& error);
    Q_SIGNAL void                fieldChanged(const QString& key);
    Q_SIGNAL void                validationChanged();
    Q_SIGNAL void                dirtyChanged(bool dirty);

private:
    std::shared_ptr<const SettingsSchema>                                 _schema;
    std::function<bool()>                                                 _valid;
    std::function<bool(const QString&)>                                   _contains;
    std::function<QVariant(const QString&)>                               _read;
    std::function<bool(const QVariantMap&, const QStringList&, QString*)> _patch;
    std::function<bool(const QString&)>                                   _hasInherited;
    std::function<QVariant(const QString&)>                               _readInherited;
    QSet<QString>                                                         _removed;
    QVariantMap                                                           _original;
    QVariantMap                                                           _draft;
    QSet<QString>                                                         _edited;
    QHash<QString, QString>                                               _inputErrors;
    QList<SettingsSchema::Error>                                          _errors;
    const SettingsSchema::Field*                                          _field(const QString& key) const;
    QVariantMap                                                           _current() const;
    void                                                                  _validate();
    void                                                                  _changed(const QString& key, bool wasDirty);
};
