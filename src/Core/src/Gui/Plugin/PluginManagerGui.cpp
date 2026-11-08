#include "PluginManagerController.h"
#include "PluginManagerWindow.h"

#include <Termaster/Core/Gui/Plugin/PluginManagerGui.h>

QDialog* createPluginManagerWindow(PluginManager& manager, QWidget* parent)
{
    auto window = new PluginManagerWindow(manager, parent);
    new PluginManagerController(manager, *window);
    return window;
}
