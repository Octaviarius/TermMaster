#include "PortClassification.h"

#include <QTest>

class PortClassificationTests final : public QObject
{
    Q_OBJECT
private:
    Q_SLOT void initTestCase()
    {
        const auto port = qEnvironmentVariable("TERMASTER_INSPECT_PORT");
        if (!port.isEmpty())
        {
            const auto result = PortClassification::inspect(port);
            QVERIFY2(!result.instanceId.isEmpty(), "Requested port has no matching present device");
            qInfo().noquote() << port << result.label() << result.evidence;
        }
    }
    Q_SLOT void metadata();
};

void PortClassificationTests::metadata()
{
    using P = PortClassification;
    P::Node usb;
    usb.instanceId = "USB\\device";
    usb.enumerator = "USB";
    auto result    = P::classify({usb});
    QCOMPARE(result.bus, P::Bus::Usb);
    QCOMPARE(result.origin, P::Origin::Hardware);

    P::Node pci;
    pci.instanceId = "PCI\\device\\SERIAL123";
    pci.enumerator = "PCI";
    QCOMPARE(P::classify({pci}).bus, P::Bus::Pci);

    P::Node ftdi;
    ftdi.enumerator = "FTDIBUS";
    QCOMPARE(P::classify({ftdi, usb, pci}).bus, P::Bus::Usb);

    P::Node software;
    software.enumerator = "SWD";
    result              = P::classify({software, usb});
    QCOMPARE(result.bus, P::Bus::Usb);
    QCOMPARE(result.origin, P::Origin::Software);

    P::Node root;
    root.enumerator = "ROOT";
    QCOMPARE(P::classify({root}).origin, P::Origin::Unknown);
    root.service = "com0com";
    QCOMPARE(P::classify({root}).origin, P::Origin::Unknown);
    root.hardwareIds = {"COM0COM\\PORT"};
    QCOMPARE(P::classify({root}).origin, P::Origin::Software);
    root.service = "fakecom0com";
    QCOMPARE(P::classify({root}).origin, P::Origin::Unknown);

    P::Node uart;
    uart.enumerator  = "ACPI";
    uart.hardwareIds = {"ACPI\\PNP0501"};
    QCOMPARE(P::classify({uart, pci}).bus, P::Bus::Legacy);
    P::Node unknown;
    QCOMPARE(P::classify({unknown, uart}).bus, P::Bus::Unknown);
    QCOMPARE(P::classify({unknown}).bus, P::Bus::Unknown);
    QCOMPARE(P::classify({}).origin, P::Origin::Unknown);

    P::Node acpi;
    acpi.enumerator = "ACPI";
    QCOMPARE(P::classify({unknown, acpi}).bus, P::Bus::Unknown);
    P::Node bluetooth;
    bluetooth.enumerator = "BTHENUM";
    QCOMPARE(P::classify({bluetooth, usb, pci}).bus, P::Bus::Bluetooth);

    usb.busTypeGuid = "{c8ebdfb0-b510-11d0-80e5-00a0c92542e3}";
    result          = P::classify({usb, pci});
    QCOMPARE(result.bus, P::Bus::Unknown);
    QCOMPARE(result.origin, P::Origin::Unknown);
    QVERIFY(result.evidence.contains("Conflicting"));
    P::Node genericRoot;
    genericRoot.enumerator = "ROOT";
    result                 = P::classify({genericRoot, pci});
    QCOMPARE(result.bus, P::Bus::Unknown);
    QCOMPARE(result.origin, P::Origin::Unknown);
    result = P::classify({unknown, pci});
    QCOMPARE(result.bus, P::Bus::Pci);
    QCOMPARE(result.origin, P::Origin::Unknown);
    P::Node other;
    other.busTypeGuid = "{1530ea73-086b-11d1-a09f-00c04fc340b1}";
    result            = P::classify({other, pci});
    QCOMPARE(result.bus, P::Bus::Other);
    QCOMPARE(result.origin, P::Origin::Unknown);
    ftdi.busTypeGuid = other.busTypeGuid;
    usb.busTypeGuid.clear();
    QCOMPARE(P::classify({ftdi, usb, pci}).bus, P::Bus::Usb);
    QCOMPARE(P::classify({ftdi, pci}).bus, P::Bus::Other);
    other.busTypeGuid = "invalid-guid";
    QCOMPARE(P::classify({other}).bus, P::Bus::Unknown);
    QVERIFY(P::inspectMany({}).isEmpty());
    P::Node guidOnly;
    guidOnly.busTypeGuid = "{9d7debbc-c85d-11d1-9eb4-006008c3a19a}";
    QCOMPARE(P::classify({guidOnly}).bus, P::Bus::Usb);
}

QTEST_APPLESS_MAIN(PortClassificationTests)
#include "PortClassificationTests.moc"
