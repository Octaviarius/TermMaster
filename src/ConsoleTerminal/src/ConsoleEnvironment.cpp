#include "ConsoleEnvironment.h"

#include <QSet>

#ifdef Q_OS_WIN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <userenv.h>
// clang-format on
#endif

namespace
{
QString keyIdentity(const QString& name)
{
#ifdef Q_OS_WIN
    return name.toCaseFolded();
#else
    return name;
#endif
}
} // namespace

QProcessEnvironment ConsoleEnvironment::userEnvironment(QString* error)
{
    if (error)
    {
        error->clear();
    }
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token))
    {
        if (error)
        {
            *error = QStringLiteral("Cannot open current user token (Windows error %1).").arg(GetLastError());
        }
        return {};
    }

    void*       block   = nullptr;
    const BOOL  created = CreateEnvironmentBlock(&block, token, FALSE);
    const DWORD failure = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(token);
    if (!created)
    {
        if (error)
        {
            *error = QStringLiteral("Cannot read current user environment (Windows error %1).").arg(failure);
        }
        return {};
    }

    QProcessEnvironment result;
    const wchar_t*      entry = static_cast<const wchar_t*>(block);
    while (*entry)
    {
        const QString   text      = QString::fromWCharArray(entry);
        const qsizetype separator = text.indexOf(u'=');
        if (separator > 0)
        {
            result.insert(text.left(separator), text.mid(separator + 1));
        }
        entry += text.size() + 1;
    }
    DestroyEnvironmentBlock(block);
    return result;
#else
    return QProcessEnvironment::systemEnvironment();
#endif
}

QProcessEnvironment ConsoleEnvironment::apply(const QProcessEnvironment& base,
                                              const QVariantMap&         overrides,
                                              QString*                   error)
{
    if (!validateOverrides(overrides, error))
    {
        return {};
    }

    QProcessEnvironment result = base;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
    {
        for (const QString& existing : result.keys())
        {
            if (keyIdentity(existing) == keyIdentity(it.key()))
            {
                result.remove(existing);
            }
        }
        if (!it.value().isNull())
        {
            result.insert(it.key(), it.value().toString());
        }
    }
    return result;
}

bool ConsoleEnvironment::validateOverrides(const QVariantMap& overrides, QString* error)
{
    if (error)
    {
        error->clear();
    }
    auto fail = [error](const QString& message) {
        if (error)
        {
            *error = message;
        }
        return false;
    };
    if (overrides.size() > 1024)
    {
        return fail(QStringLiteral("Environment overrides exceed 1024 entries."));
    }

    qsizetype     total = 0;
    QSet<QString> names;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
    {
        const QString& name = it.key();
        if (name.isEmpty() || name.size() > 1024 || name.contains(u'=') || name.contains(QChar(0)))
        {
            return fail(QStringLiteral("An environment name is empty, invalid or exceeds 1024 characters."));
        }
        const QString identity = keyIdentity(name);
        if (names.contains(identity))
        {
            return fail(QStringLiteral("Environment overrides contain conflicting names."));
        }
        names.insert(identity);
        total += name.size() + 2;
        if (!it.value().isNull())
        {
            if (it.value().metaType().id() != QMetaType::QString)
            {
                return fail(QStringLiteral("Environment values must be strings or null removals."));
            }
            const QString value = it.value().toString();
            if (value.size() > 32767 || value.contains(QChar(0)))
            {
                return fail(QStringLiteral("An environment value contains NUL or exceeds 32767 characters."));
            }
            total += value.size();
        }
        if (total > 1024 * 1024)
        {
            return fail(QStringLiteral("Environment overrides exceed 1048576 characters."));
        }
    }
    return true;
}
