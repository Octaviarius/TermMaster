#pragma once

class SettingsDialog;
class SettingsWidgetRegistry;

/** Registers model-backed sample pages. Values remain in the dialog's preview cache. */
void registerSettingsPreviewEditors(SettingsWidgetRegistry& registry);
void addSettingsPreviewPages(SettingsDialog& dialog);
