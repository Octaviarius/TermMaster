#include "PortClassification.h"

#include <QUuid>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
// Windows base types and property-key definitions must precede the device headers.
// clang-format off
#include <windows.h>
#include <initguid.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <setupapi.h>
// clang-format on
#include <array>
#include <cstring>
#include <vector>
#endif

namespace
{
QString normalizedGuid(QString value)
{
    return value.remove('{').remove('}').toUpper();
}

PortClassification::Bus nodeBus(const PortClassification::Node& node, bool* conflict)
{
    using Bus                  = PortClassification::Bus;
    const QString enumerator   = node.enumerator.toUpper();
    const QString guid         = normalizedGuid(node.busTypeGuid);
    Bus           byEnumerator = Bus::Unknown;
    Bus           byGuid       = Bus::Unknown;
    if (enumerator == "USB" || enumerator == "USBPRINT")
    {
        byEnumerator = Bus::Usb;
    }
    else if (enumerator == "PCI")
    {
        byEnumerator = Bus::Pci;
    }
    else if (enumerator == "BTHENUM" || enumerator == "BTHMODEM" || enumerator == "BTHLEDEVICE")
    {
        byEnumerator = Bus::Bluetooth;
    }
    if (guid == "9D7DEBBC-C85D-11D1-9EB4-006008C3A19A")
    {
        byGuid = Bus::Usb;
    }
    else if (guid == "C8EBDFB0-B510-11D0-80E5-00A0C92542E3")
    {
        byGuid = Bus::Pci;
    }
    else if (!QUuid(node.busTypeGuid).isNull())
    {
        byGuid = Bus::Other;
    }
    *conflict = byGuid != Bus::Other && byEnumerator != Bus::Unknown && byGuid != Bus::Unknown
             && byEnumerator != byGuid;
    return byEnumerator != Bus::Unknown ? byEnumerator : byGuid;
}

bool legacyUart(const PortClassification::Node& node)
{
    for (const QString& id : node.hardwareIds)
    {
        const QString upper = id.toUpper();
        if (upper == "ACPI\\PNP0500" || upper == "ACPI\\PNP0501" || upper == "*PNP0500" || upper == "*PNP0501")
        {
            return true;
        }
    }
    return false;
}
} // namespace

QString PortClassification::Result::label() const
{
    if (origin == Origin::Software)
    {
        return QStringLiteral("Software serial port");
    }
    switch (bus)
    {
        case Bus::Usb:
            return QStringLiteral("USB serial port");
        case Bus::Pci:
            return QStringLiteral("PCI serial port");
        case Bus::Legacy:
            return QStringLiteral("Legacy UART");
        case Bus::Bluetooth:
            return QStringLiteral("Bluetooth serial port");
        case Bus::Other:
            return QStringLiteral("Other serial port");
        case Bus::Unknown:
            return QStringLiteral("Unclassified serial port");
    }
    return QStringLiteral("Unclassified serial port");
}

PortClassification::Result PortClassification::classify(const QList<Node>& leafToRoot)
{
    Result result;
    result.evidence = QStringLiteral("No recognized device metadata");
    if (leafToRoot.isEmpty())
    {
        return result;
    }
    const Node& leaf   = leafToRoot.first();
    result.instanceId  = leaf.instanceId;
    result.service     = leaf.service;
    result.hardwareIds = leaf.hardwareIds;
    bool com0comId     = false;
    for (const QString& id : leaf.hardwareIds)
    {
        const QString upper  = id.toUpper();
        com0comId           |= upper == "COM0COM\\PORT" || upper == "ROOT\\COM0COM";
    }
    if (leaf.enumerator.compare("SWD", Qt::CaseInsensitive) == 0
        || (leaf.service.compare("com0com", Qt::CaseInsensitive) == 0 && com0comId))
    {
        result.origin   = Origin::Software;
        result.evidence = QStringLiteral("Explicit software device metadata");
    }
    if (legacyUart(leaf))
    {
        result.bus = Bus::Legacy;
        if (result.origin == Origin::Unknown)
        {
            result.origin = Origin::Hardware;
        }
        result.evidence = QStringLiteral("Leaf identifies a PNP0500/PNP0501 UART");
        return result;
    }
    const bool usbBridge   = leaf.enumerator.compare("FTDIBUS", Qt::CaseInsensitive) == 0;
    const bool genericRoot = leaf.enumerator.compare("ROOT", Qt::CaseInsensitive) == 0;
    Bus        deferredBus = Bus::Unknown;
    for (qsizetype index = 0; index < qMin<qsizetype>(leafToRoot.size(), 16); ++index)
    {
        const Node& node     = leafToRoot[index];
        bool        conflict = false;
        const Bus   bus      = nodeBus(node, &conflict);
        if (conflict)
        {
            result.evidence = QStringLiteral("Conflicting bus metadata");
            return result;
        }
        if (index > 0 && genericRoot)
        {
            break;
        }
        if (bus == Bus::Other && usbBridge)
        {
            deferredBus = bus;
            continue;
        }
        if (deferredBus == Bus::Other && bus != Bus::Unknown && bus != Bus::Usb)
        {
            break;
        }
        if (bus != Bus::Unknown)
        {
            result.bus = bus;
            // Ancestor host buses do not establish the origin of an arbitrary child.
            if (result.origin == Origin::Unknown && bus != Bus::Other && (index == 0 || (usbBridge && bus == Bus::Usb)))
            {
                result.origin = Origin::Hardware;
            }
            result.evidence = (result.origin == Origin::Software ? result.evidence + QStringLiteral("; ") : QString())
                            + QStringLiteral("Reported bus at ") + node.instanceId;
            return result;
        }
    }
    result.bus = deferredBus;
    if (deferredBus != Bus::Unknown)
    {
        result.evidence = QStringLiteral("Explicit other bus GUID on bridge device");
    }
    return result;
}

#ifdef Q_OS_WIN
namespace
{
constexpr ULONG   maximumPropertyBytes = 65536;
std::vector<BYTE> property(DEVINST device, const DEVPROPKEY& key, DEVPROPTYPE expected)
{
    DEVPROPTYPE type  = 0;
    ULONG       bytes = 0;
    if (CM_Get_DevNode_PropertyW(device, &key, &type, nullptr, &bytes, 0) != CR_BUFFER_SMALL || bytes == 0
        || bytes > maximumPropertyBytes)
    {
        return {};
    }
    std::vector<BYTE> buffer(bytes);
    if (CM_Get_DevNode_PropertyW(device, &key, &type, buffer.data(), &bytes, 0) != CR_SUCCESS || type != expected
        || bytes > buffer.size())
    {
        return {};
    }
    buffer.resize(bytes);
    return buffer;
}
QString stringProperty(DEVINST device, const DEVPROPKEY& key)
{
    const auto bytes = property(device, key, DEVPROP_TYPE_STRING);
    if (bytes.size() < sizeof(wchar_t) || bytes.size() % sizeof(wchar_t) != 0)
    {
        return {};
    }
    const auto*     value  = reinterpret_cast<const wchar_t*>(bytes.data());
    const qsizetype length = static_cast<qsizetype>(bytes.size() / sizeof(wchar_t));
    if (value[length - 1] != L'\0')
    {
        return {};
    }
    return QString::fromWCharArray(value, length - 1);
}
PortClassification::Node readNode(DEVINST device)
{
    PortClassification::Node               node;
    std::array<wchar_t, MAX_DEVICE_ID_LEN> id {};
    if (CM_Get_Device_IDW(device, id.data(), static_cast<ULONG>(id.size()), 0) == CR_SUCCESS)
    {
        node.instanceId = QString::fromWCharArray(id.data());
    }
    node.enumerator      = stringProperty(device, DEVPKEY_Device_EnumeratorName);
    node.service         = stringProperty(device, DEVPKEY_Device_Service);
    const auto guidBytes = property(device, DEVPKEY_Device_BusTypeGuid, DEVPROP_TYPE_GUID);
    if (guidBytes.size() == sizeof(GUID))
    {
        GUID guid {};
        memcpy(&guid, guidBytes.data(), sizeof(guid));
        node.busTypeGuid = QStringLiteral("%1-%2-%3-%4%5-%6%7%8%9%10%11")
                               .arg(guid.Data1, 8, 16, QLatin1Char('0'))
                               .arg(guid.Data2, 4, 16, QLatin1Char('0'))
                               .arg(guid.Data3, 4, 16, QLatin1Char('0'))
                               .arg(guid.Data4[0], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[1], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[2], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[3], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[4], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[5], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[6], 2, 16, QLatin1Char('0'))
                               .arg(guid.Data4[7], 2, 16, QLatin1Char('0'));
    }
    const auto ids = property(device, DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST);
    if (ids.size() >= 2 * sizeof(wchar_t) && ids.size() % sizeof(wchar_t) == 0)
    {
        const auto*     values = reinterpret_cast<const wchar_t*>(ids.data());
        const qsizetype length = static_cast<qsizetype>(ids.size() / sizeof(wchar_t));
        if (values[length - 1] == 0 && values[length - 2] == 0)
        {
            qsizetype start = 0;
            for (qsizetype index = 0; index < length; ++index)
            {
                if (values[index] == 0)
                {
                    if (index == start)
                    {
                        break;
                    }
                    node.hardwareIds.append(QString::fromWCharArray(values + start, index - start));
                    start = index + 1;
                }
            }
        }
    }
    return node;
}
struct DeviceSet
{
    HDEVINFO handle = INVALID_HANDLE_VALUE;
    ~DeviceSet()
    {
        if (handle != INVALID_HANDLE_VALUE)
        {
            SetupDiDestroyDeviceInfoList(handle);
        }
    }
};
struct RegistryKey
{
    HKEY handle = nullptr;
    ~RegistryKey()
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            RegCloseKey(handle);
        }
    }
};
} // namespace
#endif

PortClassification::Result PortClassification::inspect(const QString& portName)
{
    return inspectMany({portName}).value(portName.toUpper(), classify({}));
}

QMap<QString, PortClassification::Result> PortClassification::inspectMany(const QStringList& portNames)
{
    QMap<QString, Result> results;
    for (const QString& name : portNames)
    {
        results.insert(name.toUpper(), classify({}));
    }
    if (results.isEmpty())
    {
        return results;
    }
#ifdef Q_OS_WIN
    DeviceSet devices {SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES)};
    if (devices.handle == INVALID_HANDLE_VALUE)
    {
        return results;
    }
    for (DWORD index = 0; index < 65536; ++index)
    {
        SP_DEVINFO_DATA info {};
        info.cbSize = sizeof(info);
        if (!SetupDiEnumDeviceInfo(devices.handle, index, &info))
        {
            break;
        }
        RegistryKey key {SetupDiOpenDevRegKey(devices.handle, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE)};
        if (key.handle == INVALID_HANDLE_VALUE)
        {
            continue;
        }
        std::array<wchar_t, 256> value {};
        DWORD                    bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        DWORD                    type  = 0;
        if (RegQueryValueExW(key.handle, L"PortName", nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes)
                != ERROR_SUCCESS
            || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > value.size() * sizeof(wchar_t)
            || bytes % sizeof(wchar_t) != 0 || value[bytes / sizeof(wchar_t) - 1] != 0)
        {
            continue;
        }
        const QString port = QString::fromWCharArray(value.data(), bytes / sizeof(wchar_t) - 1).toUpper();
        if (!results.contains(port))
        {
            continue;
        }
        QList<Node> nodes;
        DEVINST     device = info.DevInst;
        for (int depth = 0; depth < 16; ++depth)
        {
            nodes.append(readNode(device));
            DEVINST parent = 0;
            if (CM_Get_Parent(&parent, device, 0) != CR_SUCCESS || parent == device)
            {
                break;
            }
            device = parent;
        }
        results[port] = classify(nodes);
    }
#endif
    return results;
}
