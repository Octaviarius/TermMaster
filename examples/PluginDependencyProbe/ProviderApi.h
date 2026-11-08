#pragma once

#include <QtGlobal>

#ifdef PROVIDER_BUILD
#define PROVIDER_API Q_DECL_EXPORT
#else
#define PROVIDER_API Q_DECL_IMPORT
#endif

/** An ordinary exported C++ function, not a Qt plugin entry point. */
PROVIDER_API int providerAdd(int left, int right);

/** A C++ class API also exported independently of the plugin interface. */
class PROVIDER_API ProviderMath
{
public:
    static int multiply(int left, int right);
};
