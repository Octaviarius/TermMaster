#include "HelpController.h"

#include "../Processing/LineflowEditorDialog.h"
#include "../Settings/SettingsDialog.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHelpEngine>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLoggingCategory>
#include <QPluginLoader>
#include <QScreen>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <Termaster/Core/CommandRegistry.h>
#include <Termaster/Core/Gui/Help/HelpBrowser.h>
#include <Termaster/Core/Gui/Icons.h>
#include <Termaster/Core/Help/HelpSource.h>
#include <Termaster/Core/Plugin/PluginManager.h>

namespace
{
Q_LOGGING_CATEGORY(helpLog, "termaster.app.help")
} // namespace

HelpController::HelpController(CommandRegistry& commands, PluginManager& plugins, QWidget* window, QObject* parent) :
    QObject(parent), _commands(commands), _plugins(plugins), _window(window)
{
    qApp->installEventFilter(this);
    connect(&_plugins, &QAbstractItemModel::modelReset, this, &HelpController::_scheduleRefresh);
    connect(&_plugins, &QAbstractItemModel::rowsRemoved, this, &HelpController::_scheduleRefresh);
    connect(&_plugins, &QAbstractItemModel::rowsInserted, this, &HelpController::_scheduleRefresh);
}

HelpController::~HelpController()
{
    qApp->removeEventFilter(this);
    delete _dialog.data();
}

void HelpController::showContext(QWidget* context)
{
    HelpSource::Topic topic;
    for (auto* widget = context; widget; widget = widget->parentWidget())
    {
        if (auto* settings = qobject_cast<SettingsDialog*>(widget))
        {
            topic = settings->helpContext();
            break;
        }
        if (auto* lineflow = qobject_cast<LineflowEditorDialog*>(widget))
        {
            topic = lineflow->helpContext();
            break;
        }
        if (widget == _dialog)
        {
            return;
        }
    }
    _ensureWindow();
    if (topic.id.isEmpty())
    {
        _browser->openTopic("index");
    }
    else if (!_browser->openTopic(topic.id) && !topic.markdown.isEmpty())
    {
        _browser->openGenerated(topic.id, topic.title, topic.markdown);
    }
    _dialog->show();
    _dialog->raise();
    _dialog->activateWindow();
}

void HelpController::showTopic(const QString& id)
{
    _ensureWindow();
    _browser->openTopic(id);
    _dialog->show();
    _dialog->raise();
    _dialog->activateWindow();
}

bool HelpController::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Show)
    {
        if (auto* settings = qobject_cast<SettingsDialog*>(watched);
            settings && !settings->property("helpConnected").toBool())
        {
            settings->setProperty("helpConnected", true);
            connect(settings, &SettingsDialog::helpRequested, this, [this, settings] { showContext(settings); });
        }
        if (auto* lineflow = qobject_cast<LineflowEditorDialog*>(watched);
            lineflow && !lineflow->property("helpConnected").toBool())
        {
            lineflow->setProperty("helpConnected", true);
            connect(lineflow, &LineflowEditorDialog::helpRequested, this, [this, lineflow] { showContext(lineflow); });
        }
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress
        && event->type() != QEvent::KeyRelease)
    {
        return QObject::eventFilter(watched, event);
    }
    auto* key = static_cast<QKeyEvent*>(event);
    if (event->type() == QEvent::KeyRelease && _releaseKey && key->key() == _releaseKey)
    {
        _releaseKey = 0;
        return true;
    }
    const auto* command = _commands.find("Help.Contents");
    if (!command || command->shortcut().isEmpty() || command->shortcut().count() != 1
        || command->shortcut() != QKeySequence(key->keyCombination()))
    {
        return QObject::eventFilter(watched, event);
    }
    for (auto* widget = qobject_cast<QWidget*>(watched); widget; widget = widget->parentWidget())
    {
        if (qobject_cast<QKeySequenceEdit*>(widget))
        {
            // Shortcut recording must receive F1 instead of opening documentation.
            return QObject::eventFilter(watched, event);
        }
    }
    if (event->type() == QEvent::ShortcutOverride)
    {
        key->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress)
    {
        _releaseKey = key->key();
        if (!key->isAutoRepeat())
        {
            showContext(QApplication::focusWidget());
        }
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void HelpController::_ensureWindow()
{
    if (_dialog)
    {
        _syncPackages();
        return;
    }
    _packages.clear();
    _packageVersions.clear();
    _dialog = new QDialog(_window);
    _dialog->setObjectName("helpDialog");
    _dialog->setWindowTitle(tr("Termaster help"));
    _dialog->setWindowIcon(QApplication::style()->standardIcon(QStyle::SP_DialogHelpButton));
    _dialog->setAttribute(Qt::WA_DeleteOnClose);
    const auto available = _window->screen()->availableGeometry().size() - QSize(40, 80);
    _dialog->resize(QSize(1100, 760).boundedTo(available));
    auto* layout = new QVBoxLayout(_dialog);
    _browser     = new HelpBrowser(_dialog);
    layout->addWidget(_browser, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, _dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, _dialog, &QDialog::close);
    connect(_browser, &HelpBrowser::externalLinkRequested, this, [](const QUrl& url) {
        if (url.scheme() == "https" || url.scheme() == "http")
        {
            QDesktopServices::openUrl(url);
        }
    });
    _syncPackages();
}

void HelpController::_syncPackages()
{
    if (!_browser)
    {
        return;
    }
    QStringList desired {QDir(QCoreApplication::applicationDirPath()).filePath("help/Termaster.qch")};
    for (const auto& plugin : _plugins.plugins())
    {
        QPluginLoader metadata(plugin.path);
        const auto    relative = metadata.metaData().value("MetaData").toObject().value("help").toString();
        const QDir    root(QFileInfo(plugin.path).absolutePath());
        if (relative.isEmpty() || relative.size() > 256 || relative.contains(QChar::Null)
            || QDir::isAbsolutePath(relative) || relative.split('/').contains("..") || relative.contains('\\'))
        {
            continue;
        }
        const auto file          = QFileInfo(root.filePath(relative)).canonicalFilePath();
        const auto canonicalRoot = QFileInfo(root.absolutePath()).canonicalFilePath() + '/';
        if (!file.isEmpty() && file.startsWith(canonicalRoot) && file.endsWith(".qch", Qt::CaseInsensitive))
        {
            desired.append(file);
        }
    }
    QMap<QString, QString> resolved;
    QMap<QString, QString> versions;
    for (const auto& path : desired)
    {
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty())
        {
            resolved.insert(canonical, QHelpEngineCore::namespaceName(canonical));
            const QFileInfo file(canonical);
            versions.insert(canonical,
                            QString::number(file.size()) + ':'
                                + QString::number(file.lastModified().toMSecsSinceEpoch()));
        }
    }
    for (auto it = _packages.cbegin(); it != _packages.cend(); ++it)
    {
        if (!resolved.contains(it.key()) || _packageVersions.value(it.key()) != versions.value(it.key()))
        {
            _browser->removeDocumentation(it.value());
        }
    }
    QString                error;
    QMap<QString, QString> registered;
    for (auto it = resolved.cbegin(); it != resolved.cend(); ++it)
    {
        if ((_packages.contains(it.key()) && _packages.value(it.key()) == it.value()
             && _packageVersions.value(it.key()) == versions.value(it.key()))
            || _browser->addDocumentation(it.key(), &error))
        {
            registered.insert(it.key(), it.value());
        }
        else
        {
            qCWarning(helpLog).noquote() << error;
        }
    }
    _packages        = std::move(registered);
    _packageVersions = std::move(versions);
    if (!error.isEmpty())
    {
        _dialog->findChild<QLabel*>("helpStatus")->setText(error);
    }
}

void HelpController::_scheduleRefresh()
{
    if (!_dialog || _refreshPending)
    {
        return;
    }
    _refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        _refreshPending = false;
        _syncPackages();
    });
}
