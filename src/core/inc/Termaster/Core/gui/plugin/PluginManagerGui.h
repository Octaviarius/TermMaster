#pragma once

class QDialog;
class QWidget;
class PluginManager;

/** Create a modeless management window. The caller owns the returned dialog.
 * The manager is borrowed; destroying it disables the window safely.
 * Create and use both objects on the GUI thread.
 */
QDialog* createPluginManagerWindow(PluginManager& manager, QWidget* parent = nullptr);
