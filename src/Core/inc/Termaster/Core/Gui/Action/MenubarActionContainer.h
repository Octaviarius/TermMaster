#pragma once

#include <QMenuBar>
#include <Termaster/Core/Gui/Action/ActionContainerAdapter.h>

/** Borrows the QMenuBar; the widget owns the wrapper unless another owner is supplied. */
class MenubarActionContainer final : public ActionContainerAdapter
{
public:
    explicit MenubarActionContainer(QMenuBar* menubar, QObject* owner = nullptr);
    QMenuBar* menubar() const;

protected:
    QAction* _menuAction() const override;
};
