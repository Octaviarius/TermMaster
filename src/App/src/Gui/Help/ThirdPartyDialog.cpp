#include "ThirdPartyDialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QSet>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Icons.h>

namespace
{
const QString ResourceRoot = QStringLiteral(":/Termaster/App/ThirdParty/");

bool safeNoticePath(const QString& path)
{
    return !path.isEmpty() && !QDir::isAbsolutePath(path) && !path.contains(':') && !path.contains('\\')
        && !path.split('/').contains("..") && !path.contains(QChar::Null);
}

void openWebsite(const QUrl& url)
{
    if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http"))
    {
        QDesktopServices::openUrl(url);
    }
}
} // namespace

ThirdPartyDialog::ThirdPartyDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName("thirdPartyDialog");
    setWindowTitle(tr("Third-party components"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 720);

    auto* layout = new QVBoxLayout(this);
    auto* introduction
        = new QLabel(tr("Software and artwork used by Termaster. Thanks to their authors and contributors."), this);
    introduction->setWordWrap(true);
    layout->addWidget(introduction);

    auto* search = new QLineEdit(this);
    search->setObjectName("thirdPartySearch");
    search->setPlaceholderText(tr("Filter by component, author or license…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);

    auto* splitter = new QSplitter(this);
    _tree          = new QTreeView(splitter);
    _tree->setObjectName("thirdPartyComponents");
    _tree->setRootIsDecorated(false);
    _tree->setAlternatingRowColors(true);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setIconSize(QSize(20, 20));
    _model = new QStandardItemModel(this);
    _model->setHorizontalHeaderLabels({tr("Component"), tr("Version")});
    _filter = new QSortFilterProxyModel(this);
    _filter->setSourceModel(_model);
    _filter->setFilterRole(SearchRole);
    _filter->setFilterKeyColumn(0);
    _filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    _filter->setSortCaseSensitivity(Qt::CaseInsensitive);
    _tree->setModel(_filter);
    _tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto* tabs = new QTabWidget(splitter);
    _overview  = new QTextBrowser(tabs);
    _overview->setObjectName("thirdPartyOverview");
    _overview->setOpenLinks(false);
    tabs->addTab(_overview, tr("Component"));
    auto* licensePage   = new QWidget(tabs);
    auto* licenseLayout = new QVBoxLayout(licensePage);
    licenseLayout->setContentsMargins(0, 0, 0, 0);
    _notices = new QComboBox(licensePage);
    _notices->setObjectName("thirdPartyNoticeSelector");
    licenseLayout->addWidget(_notices);
    _license = new QTextBrowser(licensePage);
    _license->setObjectName("thirdPartyLicense");
    _license->setOpenLinks(false);
    licenseLayout->addWidget(_license);
    tabs->addTab(licensePage, tr("License texts"));
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({380, 580});
    layout->addWidget(splitter, 1);

    _count            = new QLabel(this);
    auto countPalette = _count->palette();
    countPalette.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
    _count->setPalette(countPalette);
    layout->addWidget(_count);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(search, &QLineEdit::textChanged, this, &ThirdPartyDialog::_filterChanged);
    connect(_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        _select(current);
    });
    connect(_notices, &QComboBox::currentIndexChanged, this, &ThirdPartyDialog::_showNotice);
    connect(_overview, &QTextBrowser::anchorClicked, this, openWebsite);

    QString error;
    if (!_load(error))
    {
        search->setEnabled(false);
        _tree->setEnabled(false);
        _model->removeRows(0, _model->rowCount());
        _components.clear();
        _overview->setPlainText(error);
        _count->setText(tr("Component information is unavailable."));
        return;
    }
    _filter->sort(0);
    _filterChanged({});
}

bool ThirdPartyDialog::_load(QString& error)
{
    QFile file(ResourceRoot + "manifest.json");
    if (!file.open(QIODevice::ReadOnly))
    {
        error = tr("Cannot open the bundled component manifest: %1").arg(file.errorString());
        return false;
    }
    if (file.size() > 4 * 1024 * 1024)
    {
        error = tr("The component manifest exceeds its size limit.");
        return false;
    }
    QJsonParseError parseError;
    const auto      document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto      root     = document.object();
    const auto      content  = root.value("content").toObject();
    if (parseError.error != QJsonParseError::NoError || root.value("version").toInt() != 1
        || root.value("type").toString() != "termaster.third-party-components" || !content.value("components").isArray()
        || content.value("components").toArray().size() > 512)
    {
        error = tr("The bundled component manifest has an invalid format.");
        return false;
    }

    QSet<QString> ids;
    for (const auto& value : content.value("components").toArray())
    {
        const auto item = value.toObject();
        Component  component;
        component.id       = item.value("id").toString();
        component.name     = item.value("name").toString();
        component.version  = item.value("version").toString();
        component.authors  = item.value("authors").toString();
        component.homepage = item.value("homepage").toString();
        component.license  = item.value("license").toString();
        component.icon     = item.value("icon").toString();
        if (component.id.isEmpty() || component.name.isEmpty() || component.license.isEmpty()
            || ids.contains(component.id) || !item.value("notices").isArray()
            || item.value("notices").toArray().isEmpty() || item.value("notices").toArray().size() > 256)
        {
            error = tr("The manifest contains an incomplete or duplicate component.");
            return false;
        }
        for (const auto& noticeValue : item.value("notices").toArray())
        {
            const auto notice = noticeValue.toObject();
            const auto path   = notice.value("path").toString();
            if (!safeNoticePath(path))
            {
                error = tr("The manifest contains an invalid license path.");
                return false;
            }
            component.notices.append({notice.value("title").toString(path), path});
        }
        ids.insert(component.id);
        QIcon icon;
        if (safeNoticePath(component.icon))
        {
            icon = QIcon(ResourceRoot + component.icon);
        }
        auto* name = new QStandardItem(icon.isNull() ? Icons::icon("blocks") : icon, component.name);
        name->setData(_components.size(), ComponentRole);
        name->setData(component.name + '\n' + component.version + '\n' + component.authors + '\n' + component.license,
                      SearchRole);
        name->setToolTip(component.license);
        _model->appendRow(
            {name, new QStandardItem(component.version.size() == 40 ? component.version.left(12) : component.version)});
        _components.append(std::move(component));
    }
    return true;
}

void ThirdPartyDialog::_select(const QModelIndex& index)
{
    _current = index.isValid() ? _filter->mapToSource(index.siblingAtColumn(0)).data(ComponentRole).toInt() : -1;
    const QSignalBlocker blocker(_notices);
    _notices->clear();
    _license->clear();
    if (_current < 0 || _current >= _components.size())
    {
        _overview->setPlainText(tr("No matching components."));
        return;
    }

    const auto& component = _components[_current];
    QString html = "<h2>" + component.name.toHtmlEscaped() + "</h2><p><b>" + tr("Version:").toHtmlEscaped() + "</b> "
                 + component.version.toHtmlEscaped() + "</p><p><b>" + tr("Authors:").toHtmlEscaped() + "</b><br>"
                 + component.authors.toHtmlEscaped().replace('\n', "<br>") + "</p><p><b>"
                 + tr("License:").toHtmlEscaped() + "</b> " + component.license.toHtmlEscaped() + "</p>";
    if (safeNoticePath(component.icon) && !QIcon(ResourceRoot + component.icon).isNull())
    {
        html.prepend("<p><img src=\"qrc" + (ResourceRoot + component.icon).toHtmlEscaped()
                     + "\" width=\"48\" height=\"48\"></p>");
    }
    const QUrl website(component.homepage);
    if (website.isValid() && (website.scheme() == "https" || website.scheme() == "http"))
    {
        const auto address  = website.toString().toHtmlEscaped();
        html               += "<p><a href=\"" + address + "\">" + address + "</a></p>";
    }
    _overview->setHtml(html);
    for (const auto& notice : component.notices)
    {
        _notices->addItem(notice.title);
    }
    _notices->setCurrentIndex(0);
    _showNotice(0);
}

void ThirdPartyDialog::_showNotice(int index)
{
    if (_current < 0 || _current >= _components.size() || index < 0 || index >= _components[_current].notices.size())
    {
        return;
    }
    QFile file(ResourceRoot + _components[_current].notices[index].path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
    {
        _license->setPlainText(tr("Cannot read the bundled license text: %1").arg(file.fileName()));
        return;
    }
    _license->setPlainText(QString::fromUtf8(file.readAll()));
    _license->moveCursor(QTextCursor::Start);
}

void ThirdPartyDialog::_filterChanged(const QString& text)
{
    _filter->setFilterFixedString(text.trimmed());
    _count->setText(tr("%1 of %2 components").arg(_filter->rowCount()).arg(_components.size()));
    const auto first = _filter->index(0, 0);
    _tree->setCurrentIndex(first);
    _select(first);
}
