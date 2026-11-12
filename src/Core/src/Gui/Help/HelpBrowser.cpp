#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHelpContentWidget>
#include <QHelpEngine>
#include <QHelpLink>
#include <QHelpSearchEngine>
#include <QHelpSearchQueryWidget>
#include <QHelpSearchResultWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QSaveFile>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <Termaster/Core/Gui/Help/HelpBrowser.h>

namespace
{
constexpr qsizetype markdownLimit  = 256 * 1024;
constexpr qsizetype generatedLimit = 16 * 1024 * 1024;
constexpr qsizetype generatedCount = 64;
constexpr qint64    packageLimit   = 64 * 1024 * 1024;
constexpr qsizetype packageCount   = 128;

QUrl documentUrl(QUrl url)
{
    url.setFragment({});
    return url;
}

QUrl generatedUrl(const QString& id)
{
    QUrl url;
    url.setScheme(QStringLiteral("help-generated"));
    url.setPath(QStringLiteral("/") + id);
    return url;
}

/** Never delegates resource retrieval to QTextBrowser's filesystem loader. */
class HelpText final : public QTextBrowser
{
public:
    explicit HelpText(QHelpEngine& engine, QWidget* parent) : QTextBrowser(parent), _engine(engine)
    {
        setOpenLinks(false);
        setOpenExternalLinks(false);
    }

    void putGenerated(const QUrl& url, const QByteArray& html)
    {
        _generated.remove(url);
        _order.removeAll(url);
        _generated.insert(url, QByteArray(html.constData(), html.size()));
        _order.append(url);

        qsizetype total = 0;
        for (const auto& bytes : _generated)
        {
            total += bytes.size();
        }
        while (_generated.size() > generatedCount || total > generatedLimit)
        {
            const auto oldest  = _order.takeFirst();
            total             -= _generated.value(oldest).size();
            _generated.remove(oldest);
        }
    }

    QVariant loadResource(int type, const QUrl& name) override
    {
        const auto url = documentUrl(name);
        if (url.scheme() == QStringLiteral("qthelp"))
        {
            const auto bytes = _engine.fileData(url);
            if (!bytes.isEmpty() && bytes.size() <= generatedLimit)
            {
                return bytes;
            }
        }
        else if (url.scheme() == QStringLiteral("help-generated"))
        {
            const auto found = _generated.constFind(url);
            if (found != _generated.cend())
            {
                return found.value();
            }
        }
        if (type == QTextDocument::HtmlResource
            && (url.scheme() == QStringLiteral("qthelp") || url.scheme() == QStringLiteral("help-generated")))
        {
            return QByteArrayLiteral("<html><body><h1>Help page unavailable</h1>"
                                     "<p>The requested help page is no longer available.</p></body></html>");
        }
        return QByteArray();
    }

protected:
    void doSetSource(const QUrl& url, QTextDocument::ResourceType type) override
    {
        if (url.scheme() != QStringLiteral("qthelp") && url.scheme() != QStringLiteral("help-generated"))
        {
            return;
        }
        QTextBrowser::doSetSource(url, type);
    }

private:
    QHelpEngine&           _engine;
    QMap<QUrl, QByteArray> _generated;
    QList<QUrl>            _order;
};

bool fail(QString* error, const QString& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

bool copyPackage(const QString& original, const QString& destination, QString* error)
{
    QFile     source(original);
    QSaveFile snapshot(destination);
    if (!source.open(QIODevice::ReadOnly))
    {
        return fail(error, QStringLiteral("Cannot read compiled help: %1").arg(source.errorString()));
    }
    if (source.size() > packageLimit)
    {
        return fail(error, QStringLiteral("Compiled help package exceeds 64 MiB"));
    }
    if (!snapshot.open(QIODevice::WriteOnly))
    {
        return fail(error, QStringLiteral("Cannot create help snapshot: %1").arg(snapshot.errorString()));
    }

    qint64 total = 0;
    while (!source.atEnd())
    {
        const auto bytes = source.read(64 * 1024);
        if (source.error() != QFileDevice::NoError || bytes.isEmpty())
        {
            return fail(error, QStringLiteral("Cannot read compiled help: %1").arg(source.errorString()));
        }
        total += bytes.size();
        if (total > packageLimit)
        {
            return fail(error, QStringLiteral("Compiled help package exceeds 64 MiB"));
        }
        if (snapshot.write(bytes) != bytes.size())
        {
            return fail(error, QStringLiteral("Cannot write help snapshot: %1").arg(snapshot.errorString()));
        }
    }
    if (!snapshot.commit())
    {
        return fail(error, QStringLiteral("Cannot commit help snapshot: %1").arg(snapshot.errorString()));
    }
    return true;
}
} // namespace

struct HelpBrowser::Data
{
    // Engine and its workers must be destroyed before the temporary collection directory.
    QTemporaryDir                directory;
    std::unique_ptr<QHelpEngine> engine;
    HelpText*                    text       = nullptr;
    QTabWidget*                  navigation = nullptr;
    QLabel*                      status     = nullptr;
    QMap<QUrl, QString>          topicIds;
    QMap<QString, QString>       originalPaths;
    QMap<QString, QString>       snapshotPaths;
    QStringList                  retiredSnapshots;
    QString                      currentTopic;
    QString                      error;
    bool                         indexing = false;

    void _cleanupRetiredSnapshots()
    {
        for (auto it = retiredSnapshots.begin(); it != retiredSnapshots.end();)
        {
            QFile snapshot(*it);
            if (!snapshot.exists() || snapshot.remove())
            {
                it = retiredSnapshots.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
};

HelpBrowser::HelpBrowser(QWidget* parent) : QWidget(parent), _data(std::make_unique<Data>())
{
    setObjectName(QStringLiteral("helpBrowser"));
    _data->engine = std::make_unique<QHelpEngine>(_data->directory.filePath(QStringLiteral("collection.qhc")));
    _data->engine->setReadOnly(false);
    if (!_data->directory.isValid() || !_data->engine->setupData())
    {
        _data->error = tr("Cannot initialize offline help: %1").arg(_data->engine->error());
    }

    auto* layout  = new QVBoxLayout(this);
    auto* toolbar = new QHBoxLayout;
    auto* back    = new QToolButton(this);
    back->setObjectName(QStringLiteral("helpBack"));
    back->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    back->setToolTip(tr("Back"));
    back->setEnabled(false);
    auto* forward = new QToolButton(this);
    forward->setObjectName(QStringLiteral("helpForward"));
    forward->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    forward->setToolTip(tr("Forward"));
    forward->setEnabled(false);
    auto* find = new QLineEdit(this);
    find->setObjectName(QStringLiteral("helpFind"));
    find->setPlaceholderText(tr("Find in current page"));
    find->setClearButtonEnabled(true);
    find->setMaxLength(256);
    toolbar->addWidget(back);
    toolbar->addWidget(forward);
    toolbar->addStretch();
    toolbar->addWidget(find);
    layout->addLayout(toolbar);

    auto* splitter    = new QSplitter(this);
    _data->navigation = new QTabWidget(splitter);
    _data->navigation->setObjectName(QStringLiteral("helpNavigation"));
    auto* contents = _data->engine->contentWidget();
    _data->navigation->addTab(contents, tr("Contents"));
    auto* searchPage   = new QWidget(_data->navigation);
    auto* searchLayout = new QVBoxLayout(searchPage);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    auto* search  = _data->engine->searchEngine();
    auto* query   = search->queryWidget();
    auto* results = search->resultWidget();
    searchLayout->addWidget(query);
    searchLayout->addWidget(results, 1);
    _data->navigation->addTab(searchPage, tr("Search"));
    _data->text = new HelpText(*_data->engine, splitter);
    _data->text->setObjectName(QStringLiteral("helpText"));
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 700});
    layout->addWidget(splitter, 1);
    _data->status = new QLabel(this);
    _data->status->setObjectName(QStringLiteral("helpStatus"));
    _data->status->setWordWrap(true);
    layout->addWidget(_data->status);

    connect(back, &QToolButton::clicked, _data->text, &QTextBrowser::backward);
    connect(forward, &QToolButton::clicked, _data->text, &QTextBrowser::forward);
    connect(_data->text, &QTextBrowser::backwardAvailable, back, &QToolButton::setEnabled);
    connect(_data->text, &QTextBrowser::forwardAvailable, forward, &QToolButton::setEnabled);
    connect(_data->text, &QTextBrowser::anchorClicked, this, &HelpBrowser::_openUrl);
    connect(contents, &QHelpContentWidget::linkActivated, this, &HelpBrowser::_openUrl);
    connect(results, &QHelpSearchResultWidget::requestShowLink, this, &HelpBrowser::_openUrl);
    connect(query, &QHelpSearchQueryWidget::search, this, [search, query] {
        search->search(query->searchInput().left(256));
    });
    connect(search, &QHelpSearchEngine::indexingStarted, this, [this] {
        _data->indexing = true;
        _updateStatus();
    });
    connect(search, &QHelpSearchEngine::indexingFinished, this, [this] {
        _data->_cleanupRetiredSnapshots();
        _data->indexing = false;
        _updateStatus();
    });
    connect(search, &QHelpSearchEngine::searchingFinished, this, [this] { _data->_cleanupRetiredSnapshots(); });
    connect(_data->text, &QTextBrowser::sourceChanged, this, [this](const QUrl& url) {
        _data->currentTopic = _data->topicIds.value(documentUrl(url), url.toString());
        _updateStatus();
    });
    connect(find, &QLineEdit::returnPressed, this, [this, find] {
        if (find->text().isEmpty())
        {
            return;
        }
        if (!_data->text->find(find->text()))
        {
            auto cursor = _data->text->textCursor();
            cursor.movePosition(QTextCursor::Start);
            _data->text->setTextCursor(cursor);
            _data->text->find(find->text());
        }
    });
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [find] {
        find->setFocus();
        find->selectAll();
    });
    _updateStatus();
}

HelpBrowser::~HelpBrowser()
{
    auto* search = _data->engine->searchEngine();
    search->cancelSearching();
    search->cancelIndexing();
    disconnect(search, nullptr, this, nullptr);
    // Delete consumers before their engine/model and let Qt Help join owned workers.
    delete _data->navigation;
    delete _data->text;
    _data->engine.reset();
    _data->_cleanupRetiredSnapshots();
}

bool HelpBrowser::addDocumentation(const QString& qch, QString* error)
{
    if (!QFileInfo(qch).isAbsolute() || !QFileInfo(qch).isFile())
    {
        return fail(error, tr("Compiled help file must exist at an absolute path: %1").arg(qch));
    }
    if (!_data->directory.isValid())
    {
        return fail(error, tr("The owned help snapshot directory is unavailable"));
    }
    const auto originalPath = QFileInfo(qch).canonicalFilePath();
    if (QFileInfo(originalPath).size() > packageLimit)
    {
        return fail(error, tr("Compiled help package exceeds 64 MiB"));
    }
    const auto namespaceName = QHelpEngine::namespaceName(qch);
    if (namespaceName.isEmpty())
    {
        return fail(error, tr("Invalid compiled help file: %1").arg(qch));
    }
    if (_data->engine->registeredDocumentations().contains(namespaceName))
    {
        if (QFileInfo(_data->originalPaths.value(namespaceName)) != QFileInfo(originalPath))
        {
            return fail(error, tr("Help namespace is already registered: %1").arg(namespaceName));
        }
        if (error)
        {
            error->clear();
        }
        return true;
    }
    _data->_cleanupRetiredSnapshots();
    if (_data->snapshotPaths.size() + _data->retiredSnapshots.size() >= packageCount)
    {
        return fail(error, tr("The limit of 128 active or retiring help snapshots has been reached"));
    }

    const auto snapshot = _data->directory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                                    + QStringLiteral(".qch"));
    if (!copyPackage(originalPath, snapshot, error))
    {
        return false;
    }
    if (QHelpEngine::namespaceName(snapshot) != namespaceName)
    {
        _data->retiredSnapshots.append(snapshot);
        _data->_cleanupRetiredSnapshots();
        return fail(error, tr("Compiled help changed while its snapshot was being created"));
    }
    if (!_data->engine->registerDocumentation(snapshot))
    {
        _data->retiredSnapshots.append(snapshot);
        _data->_cleanupRetiredSnapshots();
        return fail(error, _data->engine->error());
    }
    _data->originalPaths.insert(namespaceName, originalPath);
    _data->snapshotPaths.insert(namespaceName, snapshot);
    _data->engine->contentModel()->createContentsForCurrentFilter();
    _data->engine->searchEngine()->scheduleIndexDocumentation();
    if (error)
    {
        error->clear();
    }
    _updateStatus();
    return true;
}

bool HelpBrowser::removeDocumentation(const QString& namespaceName, QString* error)
{
    if (!_data->engine->unregisterDocumentation(namespaceName))
    {
        return fail(error, _data->engine->error());
    }
    _data->engine->searchEngine()->cancelSearching();
    _data->engine->searchEngine()->cancelIndexing();
    _data->originalPaths.remove(namespaceName);
    const auto snapshot = _data->snapshotPaths.take(namespaceName);
    if (!snapshot.isEmpty())
    {
        _data->retiredSnapshots.append(snapshot);
    }
    // Native workers may still be closing their independent SQLite readers. Retry on completion;
    // active and retiring files share the same bound, and final cleanup follows worker shutdown.
    _data->_cleanupRetiredSnapshots();
    // An already rendered QTextDocument remains an owned copy. Stale navigation is discarded.
    _data->text->clearHistory();
    _data->engine->contentModel()->createContentsForCurrentFilter();
    _data->engine->searchEngine()->cancelSearching();
    _data->engine->searchEngine()->scheduleIndexDocumentation();
    for (auto it = _data->topicIds.begin(); it != _data->topicIds.end();)
    {
        if (it.key().host() == namespaceName)
        {
            it = _data->topicIds.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (error)
    {
        error->clear();
    }
    _updateStatus();
    return true;
}

QStringList HelpBrowser::namespaces() const
{
    return _data->engine->registeredDocumentations();
}

bool HelpBrowser::openTopic(const QString& id)
{
    const auto links = _data->engine->documentsForIdentifier(id);
    if (links.isEmpty())
    {
        openGenerated(
            id,
            tr("Help topic unavailable"),
            tr("This help topic is unavailable. Its documentation may be missing or its provider has been removed."));
        return false;
    }
    const auto url = links.first().url;
    _data->topicIds.insert(documentUrl(url), QString(id.constData(), id.size()));
    while (_data->topicIds.size() > 256)
    {
        _data->topicIds.erase(_data->topicIds.begin());
    }
    _openUrl(url);
    return true;
}

void HelpBrowser::openGenerated(const QString& id, const QString& title, const QString& markdown)
{
    QTextDocument document;
    document.setResourceProvider([](const QUrl&) -> QVariant { return QByteArray(); });
    const auto content = markdown.toUtf8().size() <= markdownLimit ?
                             markdown :
                             tr("This generated help page exceeds the supported size limit.");
    document.setMarkdown(QStringLiteral("# %1\n\n%2").arg(title.left(1024), content),
                         QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                             | QTextDocument::MarkdownNoHTML);
    document.setMetaInformation(QTextDocument::DocumentTitle, title.left(1024));
    const auto url = generatedUrl(id.left(256));
    _data->text->putGenerated(url, document.toHtml().toUtf8());
    _data->topicIds.insert(documentUrl(url), QString(id.constData(), id.size()));
    while (_data->topicIds.size() > 256)
    {
        _data->topicIds.erase(_data->topicIds.begin());
    }
    if (_data->text->source() == url)
    {
        _data->text->reload();
    }
    else
    {
        _data->text->setSource(url, QTextDocument::HtmlResource);
    }
    _data->currentTopic = QString(id.constData(), id.size());
}

QString HelpBrowser::currentTopic() const
{
    return _data->currentTopic;
}

QHelpEngine& HelpBrowser::engine()
{
    return *_data->engine;
}

void HelpBrowser::_openUrl(const QUrl& requested)
{
    auto url = requested;
    if (url.isRelative())
    {
        url = _data->text->source().resolved(url);
    }
    if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https"))
    {
        emit externalLinkRequested(url);
        return;
    }
    if (url.scheme() == QStringLiteral("help"))
    {
        openTopic(url.path());
        return;
    }
    if (url.scheme() == QStringLiteral("qthelp"))
    {
        if (_data->engine->findFile(url).isEmpty())
        {
            openGenerated(url.toString(), tr("Help page unavailable"), tr("The requested help page is unavailable."));
            return;
        }
        _data->text->setSource(url, QTextDocument::HtmlResource);
        return;
    }
    if (url.scheme() == QStringLiteral("help-generated"))
    {
        _data->text->setSource(url, QTextDocument::HtmlResource);
    }
}

void HelpBrowser::_updateStatus()
{
    auto       message = _data->error;
    const auto source  = _data->text->source();
    if (source.scheme() == QStringLiteral("qthelp") && !namespaces().contains(source.host()))
    {
        message = tr("This provider is unavailable. The displayed page is a retained copy.");
    }
    if (_data->indexing)
    {
        if (!message.isEmpty())
        {
            message += QLatin1Char(' ');
        }
        message += tr("Updating help search index…");
    }
    _data->status->setText(message);
    _data->status->setVisible(!message.isEmpty());
}
