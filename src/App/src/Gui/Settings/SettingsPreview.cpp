#include "SettingsPreview.h"

#include "SettingsDialog.h"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStyle>
#include <QToolButton>
#include <Termaster/Core/Gui/Settings/SettingsFieldEditor.h>
#include <Termaster/Core/Gui/Settings/SettingsWidgetRegistry.h>
#include <Termaster/Core/SettingsSchema.h>
#include <stdexcept>

namespace
{
class SerialPortPreviewEditor final : public SettingsFieldEditor
{
public:
    explicit SerialPortPreviewEditor(QWidget* parent) : SettingsFieldEditor(parent)
    {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        _ports = new QStandardItemModel(this);
        _combo = new QComboBox(this);
        _combo->setModel(_ports);
        _combo->setObjectName("previewSerialPort");
        auto* refresh = new QToolButton(this);
        refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        refresh->setToolTip(tr("Reload example device list"));
        layout->addWidget(_combo, 1);
        layout->addWidget(refresh);
        _populate();
        connect(refresh, &QToolButton::clicked, this, [this] { _populate(); });
        connect(_combo, &QComboBox::currentIndexChanged, this, [this] { emit valueChanged(); });
    }
    QVariant value() const override
    {
        return _combo->currentData();
    }
    void setValue(const QVariant& value) override
    {
        const QSignalBlocker blocker(_combo);
        auto                 index = _combo->findData(value);
        if (index < 0 && value.isValid())
        {
            auto* item = new QStandardItem(style()->standardIcon(QStyle::SP_MessageBoxWarning),
                                           value.toString() + tr(" (unavailable)"));
            item->setData(value, Qt::UserRole);
            _ports->appendRow(item);
            index = _ports->rowCount() - 1;
        }
        _combo->setCurrentIndex(index);
    }

private:
    QComboBox*          _combo;
    QStandardItemModel* _ports;
    void                _populate()
    {
        const auto           previous = _combo->currentData();
        const QSignalBlocker blocker(_combo);
        _ports->clear();
        for (const auto& entry : {
                 std::pair { "COM3",  "USB serial adapter"},
                 std::pair { "COM7",         "Debug probe"},
                 std::pair {"COM12", "Example UART bridge"}
        })
        {
            auto* item = new QStandardItem(style()->standardIcon(QStyle::SP_ComputerIcon),
                                           QString(entry.first) + "  —  " + entry.second);
            item->setData(QString(entry.first), Qt::UserRole);
            item->setToolTip(tr("Example device; hardware is not accessed."));
            _ports->appendRow(item);
        }
        if (previous.isValid())
        {
            setValue(previous);
        }
    }
};

std::shared_ptr<SettingsSchema> schema(const QString& file)
{
    QFile resource(":/Termaster/App/Settings/" + file + ".yaml");
    if (!resource.open(QIODevice::ReadOnly))
    {
        throw std::runtime_error(resource.errorString().toStdString());
    }
    return std::make_shared<SettingsSchema>(resource.readAll());
}
} // namespace

void registerSettingsPreviewEditors(SettingsWidgetRegistry& registry)
{
    registry.registerWidget("serialPortPreview", [](const SettingsSchema::Field&, QWidget* parent) {
        return new SerialPortPreviewEditor(parent);
    });
}

void addSettingsPreviewPages(SettingsDialog& dialog)
{
    const auto add = [&](const QString& id,
                         const QString& category,
                         const QString& title,
                         const QString& description,
                         const QString& file) {
        auto        descriptor = schema(file);
        auto        target     = dialog.previewSettings().node(id);
        QVariantMap initial;
        for (const auto& field : descriptor->fields())
        {
            if (field.hasDefault)
            {
                initial.insert(field.settingsPath, field.defaultValue);
            }
        }
        QString error;
        if (!target.applyValues(initial, {}, &error))
        {
            throw std::runtime_error(error.toStdString());
        }
        dialog.addPage(id, category, title, description, std::move(descriptor), target, true);
    };
    add("application.behavior",
        "Application",
        "Interface & behavior",
        "Interface, startup and history options.",
        "ApplicationPreview");
    add("workspace.general",
        "Workspace",
        "General",
        "Workspace identity, directories and saving behavior.",
        "WorkspacePreview");
    add("session.layout",
        "Session",
        "Layout & startup",
        "Session identity, pane arrangement and startup behavior.",
        "SessionPreview");
    add("terminal.appearance",
        "Terminals",
        "Text & screen",
        "Font, colors, screen buffer and keyboard behavior.",
        "TerminalPreview");
    add("terminal.serial",
        "Terminals",
        "Serial port",
        "Connection parameters for a COM-port terminal.",
        "SerialPreview");
}
