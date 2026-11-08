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
    layout->addStretch();
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
}

void WorkspacePage::showChoice(const QString& error)
{
    _title->setText(tr("Choose a workspace"));
    _message->setText(error.isEmpty() ? tr("Open an existing workspace or create a new one in any folder.") : error);
    _file->clear();
    _file->hide();
}
