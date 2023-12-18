#pragma once

#include <QWidget>
#include "ui_SettingsWindow.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class SettingsWindowClass;
};
QT_END_NAMESPACE

#include "../core/scene-source.h"

class SettingsWindow : public QWidget {
	Q_OBJECT

public:
	SettingsWindow(const core::Source& source, QWidget* parent = nullptr);
	~SettingsWindow();

private:
	Ui::SettingsWindowClass* ui;
  core::Source source;
};
