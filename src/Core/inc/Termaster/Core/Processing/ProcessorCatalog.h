#pragma once

#include <QMap>
#include <QStringList>
#include <Termaster/Core/Processing/Processor.h>
#include <memory>

/** Pure host catalog ABI. Plugins link this SDK and Qt, never a static Core copy. */
class ProcessorCatalog
{
public:
    struct Type
    {
        using Factory = std::function<
            std::unique_ptr<Processor>(const QVariantMap&, const Processor::Limits&, QString*)>;

        QString     id;
        QString     title;
        QByteArray  settingsSchema;
        Factory     create;
        QString     kind = QStringLiteral("native");
        QString     origin;
        QVariantMap defaults;
        QVariantMap editor;
        QVariantMap definition;
        QStringList references;
        bool        available = true;
        QString     unavailableReason;
    };

    /** Source validation is static and must not execute user code. Preparation may
     * compile source lazily, but must not construct a runtime. The returned factory
     * uses its final configuration argument. Copy referenced factories before
     * returning; every factory invocation must produce independent state.
     */
    struct Provider
    {
        using References = QMap<QString, Type::Factory>;
        std::function<bool(const QVariantMap&, QString*)> validate;
        std::function<
            Type::
                Factory(const QVariantMap&, const QVariantMap&, const References&, const Processor::Limits&, QString*)>
            prepare;
    };

    virtual ~ProcessorCatalog() = default;

    /** Throws when metadata, schema or resolved default configuration is invalid. */
    virtual void registerType(const QString& owner, Type type)                                         = 0;
    virtual void registerProvider(const QString& owner, const QString& id, Provider provider)          = 0;
    virtual bool unregisterProvider(const QString& owner, const QString& id, QString* error = nullptr) = 0;
    virtual bool unregisterType(const QString& owner, const QString& id, QString* error = nullptr)     = 0;
};
