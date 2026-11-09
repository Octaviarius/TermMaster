#pragma once

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

        QString    id;
        QString    title;
        QByteArray settingsSchema;
        Factory    create;
    };

    virtual ~ProcessorCatalog() = default;

    /** Throws when metadata, schema or resolved default configuration is invalid. */
    virtual void registerType(const QString& owner, Type type)                                     = 0;
    virtual bool unregisterType(const QString& owner, const QString& id, QString* error = nullptr) = 0;
};
