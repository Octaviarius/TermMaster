#include "WorkspacePage.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

WorkspacePage::WorkspacePage(QWidget* parent) : QWidget(parent)
{
    setObjectName("workspacePage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    _title = new QLabel(this);
    _title->setObjectName("workspaceTitle");
    _title->setTextFormat(Qt::PlainText);
    _title->setWordWrap(true);
    auto font = _title->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 4);
    _title->setFont(font);
    _message = new QLabel(this);
    _message->setObjectName("workspaceMessage");
    _message->setTextFormat(Qt::PlainText);
    _message->setWordWrap(true);
    _file = new QLineEdit(this);
    _file->setObjectName("workspaceFile");
    _file->setReadOnly(true);
    auto* buttons = new QHBoxLayout;
    auto* create  = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), tr("Create workspace…"), this);
    create->setObjectName("createWorkspace");
    auto* open = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open workspace…"), this);
    open->setObjectName("openWorkspace");
    buttons->addWidget(create);
    buttons->addWidget(open);
    buttons->addStretch();
    layout->addWidget(_title);
    layout->addWidget(_message);
    layout->addWidget(_file);
    layout->addLayout(buttons);
    _sessionTitle = new QLabel(this);
    _sessionTitle->setObjectName("sessionTitle");
    _sessionTitle->setTextFormat(Qt::PlainText);
    _sessionTitle->setWordWrap(true);
    _sessionTitle->setFont(font);
    _sessionSummary = new QLabel(this);
    _sessionSummary->setObjectName("sessionSummary");
    _sessionSummary->setTextFormat(Qt::PlainText);
    _sessionSummary->setWordWrap(true);
    _sessionFile = new QLineEdit(this);
    _sessionFile->setObjectName("sessionFile");
    _sessionFile->setReadOnly(true);
    _createSession = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon), tr("Create session…"), this);
    _createSession->setObjectName("createSession");
    _openSession = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open session…"), this);
    _openSession->setObjectName("openSession");
    auto* sessions = new QHBoxLayout;
    sessions->addWidget(_createSession);
    sessions->addWidget(_openSession);
    sessions->addStretch();
    layout->addSpacing(16);
    layout->addWidget(_sessionTitle);
    layout->addWidget(_sessionSummary);
    layout->addWidget(_sessionFile);
    layout->addLayout(sessions);
    layout->addStretch();
    connect(_createSession, &QPushButton::clicked, this, &WorkspacePage::createSessionRequested);
    connect(_openSession, &QPushButton::clicked, this, &WorkspacePage::openSessionRequested);
    connect(open, &QPushButton::clicked, this, &WorkspacePage::openRequested);
    connect(create, &QPushButton::clicked, this, &WorkspacePage::createRequested);
    showChoice();
}

void WorkspacePage::setWorkspace(const QString& name, const QString& file)
{
    _title->setText(name);
    _message->setText(tr("Workspace is open."));
    _file->setText(file);
    _file->setCursorPosition(0);
    _file->show();
    _createSession->setEnabled(true);
    _openSession->setEnabled(true);
    setSession({}, {}, 0, 0);
}

void WorkspacePage::showChoice(const QString& error)
{
    _title->setText(tr("Choose a workspace"));
    _message->setText(error.isEmpty() ? tr("Open an existing workspace or create a new one in any folder.") : error);
    _file->clear();
    _file->hide();
    _createSession->setEnabled(false);
    _openSession->setEnabled(false);
    setSession({}, {}, 0, 0);
}

void WorkspacePage::setSession(const QString& name, const QString& file, int objects, int links)
{
    const bool open = !file.isEmpty();
    _sessionTitle->setText(open ? name : tr("No session open"));
    _sessionSummary->setText(
        open ? tr("Configuration loaded: %1 objects, %2 links. Connections are not started.").arg(objects).arg(links) :
               tr("Create a session or choose one from this workspace."));
    _sessionFile->setText(file);
    _sessionFile->setCursorPosition(0);
    _sessionFile->setVisible(open);
}
