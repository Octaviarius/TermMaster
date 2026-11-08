#pragma once

#include <QToolBar>
#include <Termaster/Core/gui/action/ActionContainerAdapter.h>

/** Borrows the toolbar and supports nested menus through native drop-down buttons. */
class ToolbarActionContainer final : public ActionContainerAdapter
{
public:
    explicit ToolbarActionContainer(QToolBar* toolbar, QObject* owner = nullptr);
    QToolBar* toolbar() const;

protected:
    QAction* _menuAction() const override;
    void     _configureAction(QAction* action) override;
};
