#include "Path.h"

static QStringList _splitPath(const QString& path, QChar sep);

static QStringList _splitPath(const QString& path, QChar sep)
{
    QStringList res;

    if (path.length() > 0)
    {
        auto d = path.data();

        int s = 0;
        int e;

        for (e = 0; e < path.length(); e++)
        {
            if (d[e] == QChar('/') || d[e] == QChar('\\') || d[e] == sep)
            {
                if (e - s > 1)
                {
                    res += path.sliced(s, e - s);
                }

                s = ++e;
            }
        }

        if (e - s > 1)
        {
            res += path.sliced(s, e - s);
        }
    }

    return res;
}
