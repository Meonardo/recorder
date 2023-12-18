#include "TestMainWindow.h"

#include <Windows.h>

#include <QScreen>
#include <qwindow.h>
#include <QShowEvent>
#include <QGuiApplication>

#include "util/profiler.hpp"

#include "../core/app.h"
#include "../core/output.h"
#include "../core/display-helpers.h"
#include "../core/source-preview.h"

TestMainWindow::TestMainWindow(QWidget* parent)
  : QMainWindow(parent),
    ui(new Ui::TestMainWindowClass()) {

	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_DeleteOnClose);

	ui->setupUi(this);

	auto displayResize = [this]() {
		struct obs_video_info ovi;

		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}

		auto dpi = devicePixelRatioF();
		ui->preview->UpdateDPI(dpi);
	};
	auto dpi = devicePixelRatioF();
	ui->preview->UpdateDPI(dpi);

	// resize preview when the window is resized
	connect(windowHandle(), &QWindow::screenChanged, displayResize);
	connect(ui->preview, &OBSQTDisplay::DisplayResized, displayResize);

	ConfigureUI();
}

TestMainWindow::~TestMainWindow() {
	// remove draw callback
	obs_display_remove_draw_callback(ui->preview->GetDisplay(), TestMainWindow::RenderMain,
					 this);

	delete ui;
}

void TestMainWindow::Prepare() {
	ProfileScope("MainWindow::Prepare");

	// check if preview is enabled
	previewEnabled = CoreApp->IsPreviewEnabled();
	QMetaObject::invokeMethod(this, "EnablePreviewDisplay", Qt::QueuedConnection,
				  Q_ARG(bool, true));
	obs_display_set_enabled(ui->preview->GetDisplay(), previewEnabled);
	ui->preview->setVisible(previewEnabled);

	auto addDisplay = [this](OBSQTDisplay* window) {
		obs_display_add_draw_callback(window->GetDisplay(), TestMainWindow::RenderMain,
					      this);

		struct obs_video_info ovi;
		if (obs_get_video_info(&ovi)) {
			ResizePreview(ovi.base_width, ovi.base_height);
		}
	};
	connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDisplay);

	// 显示窗口
	show();
	activateWindow();
	ui->attachedSourceComboBox->clear();

	attachedSources.clear();

	// 加载已经保存的数据源
	auto sources = core::Source::GetAttachedSources();
	for (const auto& item : sources) {
		attachedSources.push_back(item);
		ui->attachedSourceComboBox->addItem(item.Name().c_str());
	}

	// std::thread([this]() { LoadLocalSources(); }).detach();
}

bool TestMainWindow::nativeEvent(const QByteArray&, void* message, qintptr*) {
	const MSG& msg = *static_cast<MSG*>(message);
	switch (msg.message) {
	case WM_MOVE:
		for (OBSQTDisplay* const display : findChildren<OBSQTDisplay*>()) {
			display->OnMove();
		}
		break;
	case WM_DISPLAYCHANGE:
		for (OBSQTDisplay* const display : findChildren<OBSQTDisplay*>()) {
			display->OnDisplayChange();
		}
	}

	return false;
}

void TestMainWindow::changeEvent(QEvent* event) {
	if (event->type() == QEvent::WindowStateChange) {
		QWindowStateChangeEvent* stateEvent = (QWindowStateChangeEvent*)event;

		if (isMinimized()) {
			if (previewEnabled)
				EnablePreviewDisplay(false);
		} else if (stateEvent->oldState() & Qt::WindowMinimized && isVisible()) {
			if (previewEnabled)
				EnablePreviewDisplay(true);
		}
	}
}

void TestMainWindow::EnablePreviewDisplay(bool enable) {
	obs_display_set_enabled(ui->preview->GetDisplay(), enable);
	ui->preview->setVisible(enable);
}

void TestMainWindow::ResizePreview(uint32_t cx, uint32_t cy) {
	auto targetSize = GetPixelSize(ui->preview);

	if (ui->preview->IsFixedScaling()) {
		ui->preview->ClampScrollingOffsets();
		previewScale = ui->preview->GetScalingAmount();
		GetCenterPosFromFixedScale(int(cx), int(cy),
					   targetSize.width() - PREVIEW_EDGE_SIZE * 2,
					   targetSize.height() - PREVIEW_EDGE_SIZE * 2, previewX,
					   previewY, previewScale);
		previewX += ui->preview->GetScrollX();
		previewY += ui->preview->GetScrollY();

	} else {
		GetScaleAndCenterPos(int(cx), int(cy), targetSize.width() - PREVIEW_EDGE_SIZE * 2,
				     targetSize.height() - PREVIEW_EDGE_SIZE * 2, previewX,
				     previewY, previewScale);
		ui->preview->UpdateScale(previewScale);
	}

	previewX += float(PREVIEW_EDGE_SIZE);
	previewY += float(PREVIEW_EDGE_SIZE);

	ui->preview->UpdatePosition(previewX, previewY);
}

void TestMainWindow::RenderMain(void* data, uint32_t, uint32_t) {
	GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "RenderMain");

	TestMainWindow* window = static_cast<TestMainWindow*>(data);

	obs_video_info ovi;
	obs_get_video_info(&ovi);

	window->previewCX = int(window->previewScale * float(ovi.base_width));
	window->previewCY = int(window->previewScale * float(ovi.base_height));

	gs_viewport_push();
	gs_projection_push();

	obs_display_t* display = window->ui->preview->GetDisplay();
	uint32_t width, height;
	obs_display_size(display, &width, &height);
	float right = float(width) - window->previewX;
	float bottom = float(height) - window->previewY;

	gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f, 100.0f);

	/* --------------------------------------- */

	gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height), -100.0f, 100.0f);
	gs_set_viewport(window->previewX, window->previewY, window->previewCX, window->previewCY);
	obs_render_main_texture_src_color_only();
	gs_load_vertexbuffer(nullptr);

	/* --------------------------------------- */

	gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f, 100.0f);
	gs_reset_viewport();

	/* --------------------------------------- */

	gs_projection_pop();
	gs_viewport_pop();

	GS_DEBUG_MARKER_END();
}

void TestMainWindow::ConfigureUI() {
	connect(ui->sourceAddButton, &QPushButton::clicked, this, [&]() {
		auto idx = ui->sourceTypeComboBox->currentIndex();
		core::SourceType type = core::SourceType(idx + 1);

		if (type == core::kSourceTypeRTSP) {
			auto url = ui->sourceComboBox->currentText().toStdString();
			if (url.empty()) {
				return;
			}
			auto rtspSorce = core::RTSPSource(url, url);
			rtspSorce.Attach();
		} else {
			auto source = localSources[ui->sourceComboBox->currentIndex()].get();

			if (source->Attach()) {
				attachedSources.push_back(*source);
				ui->attachedSourceComboBox->addItem(source->Name().c_str());
			}

			if (type == core::kSourceTypeScreenCapture) {
				auto screenSource = dynamic_cast<core::ScreenSource*>(source);
				if (screenSource)
					screenSource->ScaleFitOutputSize();
			}
		}
	});

	connect(ui->sourcePreviewButton, &QPushButton::clicked, this, [&]() {
		auto idx = ui->sourceTypeComboBox->currentIndex();
		core::SourceType type = core::SourceType(idx + 1);
		if (type == core::kSourceTypeCamera || type == core::kSourceTypeRTSP) {
			if (type == core::kSourceTypeRTSP) {
				auto url = ui->sourceComboBox->currentText().toStdString();
				if (url.empty()) {
					return;
				}

				std::unique_ptr<core::RTSPSource> source =
				  std::make_unique<core::RTSPSource>(url, url);
        std::string sourceName(source->Name());

				auto preview = new core::ui::SourcePreview(std::move(source));

        preview->setMinimumSize(480, 270);
        preview->setWindowTitle(QString::fromStdString(sourceName));
				preview->show();
			} else {
				auto& source = localSources[ui->sourceComboBox->currentIndex()];
        std::string sourceName(source->Name());

				auto preview = new core::ui::SourcePreview(std::move(source));

        preview->setMinimumSize(480, 270);
        preview->setWindowTitle(QString::fromStdString(sourceName));
				preview->show();
			}
		}
	});

	connect(ui->sourceRemoveButton, &QPushButton::clicked, this, [&]() {
		auto sourceName = ui->attachedSourceComboBox->currentText().toStdString();
		if (core::Source::RemoveAttachedByName(sourceName)) {
			ui->attachedSourceComboBox->removeItem(
			  ui->attachedSourceComboBox->currentIndex());
			attachedSources.erase(attachedSources.begin() +
					      ui->attachedSourceComboBox->currentIndex());
		}
	});

	connect(ui->startRecordButton, &QPushButton::clicked, this,
		[&]() { CoreApp->GetOutputManager()->StartRecording(); });

	connect(ui->stopRecordButton, &QPushButton::clicked, this,
		[&]() { CoreApp->GetOutputManager()->StopRecording(); });

	connect(ui->settingsButton, &QPushButton::clicked, this, [&]() {

	});

	connect(ui->sourceTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[&](int idx) {
			core::SourceType type = core::SourceType(idx + 1);

			localSources.clear();
			ui->sourceComboBox->clear();
			ui->sourceComboBox->setEditable(false);

			switch (type) {
			case core::kSourceTypeUnknow: break;
			case core::kSourceTypeAudioCapture: {
				auto audioInputSources =
				  core::AudioSource::GetAudioSources(core::kSourceTypeAudioCapture);
				for (auto& item : audioInputSources) {
					ui->sourceComboBox->addItem(item.Name().c_str());

					auto source =
					  std::make_unique<core::AudioSource>(std::move(item));
					localSources.emplace_back(std::move(source));
				}
				break;
			}

			case core::kSourceTypeAudioPlayback: {
				auto audioOutputSources = core::AudioSource::GetAudioSources(
				  core::kSourceTypeAudioPlayback);
				for (auto& item : audioOutputSources) {
					ui->sourceComboBox->addItem(item.Name().c_str());

					auto source =
					  std::make_unique<core::AudioSource>(std::move(item));
					localSources.emplace_back(std::move(source));
				}
				break;
			}
			case core::kSourceTypeScreenCapture: {
				auto screenSources = core::ScreenSource::GetScreenSources();
				for (auto& item : screenSources) {
					ui->sourceComboBox->addItem(item.Name().c_str());

					auto source =
					  std::make_unique<core::ScreenSource>(std::move(item));
					localSources.emplace_back(std::move(source));
				}
				break;
			}

			case core::kSourceTypeCamera: {
				auto cameraSources = core::CameraSource::GetCameraSources();
				for (auto& item : cameraSources) {
					ui->sourceComboBox->addItem(item.Name().c_str());

					auto source =
					  std::make_unique<core::CameraSource>(std::move(item));
					localSources.emplace_back(std::move(source));
				}
				break;
			}

			case core::kSourceTypeRTSP: {
				ui->sourceComboBox->setEditable(true);
				break;
			}
			default: break;
			}
		});
}
