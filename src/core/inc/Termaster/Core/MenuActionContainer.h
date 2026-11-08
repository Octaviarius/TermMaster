#pragma once

#include <QMenu>
#include <Termaster/Core/ActionContainerAdapter.h>

/** Owns the QMenu; attaching the wrapper transfers ownership of the entire subtree. */
class MenuActionContainer final : public ActionContainerAdapter
{
public:
    explicit MenuActionContainer(QString name, QString title = {}, QObject* owner = nullptr);
    QMenu* menu() const;

protected:
    QAction* _menuAction() const override;
};
