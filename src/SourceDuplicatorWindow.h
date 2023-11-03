#pragma once

#include <QWidget>
#include "ui_SourceDuplicatorWindow.h"
#include "MainWindow.h"

#include "qt-display.hpp"
#include <obs.hpp>

QT_BEGIN_NAMESPACE
namespace Ui {
class SourceDuplicatorWindowClass;
};
QT_END_NAMESPACE

class SourceDuplicatorWindow : public QWidget {
	Q_OBJECT

public:
	SourceDuplicatorWindow(OBSSource source, QWidget* parent = nullptr);
	~SourceDuplicatorWindow();
	void Init();

	const char* SourceName();

protected:
	virtual void closeEvent(QCloseEvent* event) override;
	virtual bool nativeEvent(const QByteArray& eventType, void* message,
				 qintptr* result) override;

private:
	Ui::SourceDuplicatorWindowClass* ui;
	MainWindow* main;
	bool acceptClicked;

	OBSSource source;
	OBSSourceAutoRelease sourceA;
	OBSSourceAutoRelease sourceB;
	OBSSourceAutoRelease sourceClone;

	OBSSignal removedSignal;
	OBSSignal renamedSignal;
	OBSSignal updatePropertiesSignal;

	void Cleanup();

	static void SourceRemoved(void* data, calldata_t* params);
	static void SourceRenamed(void* data, calldata_t* params);
	static void UpdateProperties(void* data, calldata_t* params);
	static void DrawPreview(void* data, uint32_t cx, uint32_t cy);
	static void DrawTransitionPreview(void* data, uint32_t cx, uint32_t cy);
};
