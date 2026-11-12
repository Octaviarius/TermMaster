#pragma once

#include <QStringList>
#include <QUrl>
#include <QWidget>
#include <memory>

class QHelpEngine;

/** Offline Qt Help navigation. Collection and search workers belong to this widget. */
class HelpBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit HelpBrowser(QWidget* parent = nullptr);
    ~HelpBrowser() override;

    bool         addDocumentation(const QString& qch, QString* error = nullptr);
    bool         removeDocumentation(const QString& namespaceName, QString* error = nullptr);
    QStringList  namespaces() const;
    bool         openTopic(const QString& id);
    void         openGenerated(const QString& id, const QString& title, const QString& markdown);
    QString      currentTopic() const;
    QHelpEngine& engine();

signals:
    void externalLinkRequested(const QUrl& url);

private:
    struct Data;
    std::unique_ptr<Data> _data;

    void _openUrl(const QUrl& url);
    void _updateStatus();
};
