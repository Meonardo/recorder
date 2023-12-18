#include "source-preview.h"

#include "app.h"

#include "display-helpers.h"
#include "qt-display.h"

namespace core::ui {
static void DrawPreview(void* data, uint32_t cx, uint32_t cy) {
	SourcePreview* window = static_cast<SourcePreview*>(data);

	if (!window->GetNativeSource())
		return;

	uint32_t sourceCX = max(obs_source_get_width(window->GetNativeSource()), 1u);
	uint32_t sourceCY = max(obs_source_get_height(window->GetNativeSource()), 1u);

	int x, y;
	int newCX, newCY;
	float scale;

	GetScaleAndCenterPos(sourceCX, sourceCY, cx, cy, x, y, scale);

	newCX = int(scale * float(sourceCX));
	newCY = int(scale * float(sourceCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sourceCX), 0.0f, float(sourceCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(window->GetNativeSource());

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

SourcePreview::SourcePreview(std::unique_ptr<core::Source> source, QWidget* parent)
  : QWidget(parent),
    source(std::move(source)) {
	mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	setLayout(mainLayout);

	display = new OBSQTDisplay(this);
	mainLayout->addWidget(display);

	nativeSource = this->source->GetNativeSource();

	enum obs_source_type type = obs_source_get_type(nativeSource);
	obs_source_inc_showing(nativeSource);

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(display->GetDisplay(), DrawPreview, this);
	};
	uint32_t caps = obs_source_get_output_flags(nativeSource);
	bool drawable_type = type == OBS_SOURCE_TYPE_INPUT || type == OBS_SOURCE_TYPE_SCENE;
	bool drawable_preview = (caps & OBS_SOURCE_VIDEO) != 0;

	if (drawable_preview && drawable_type) {
		display->show();
		connect(display, &OBSQTDisplay::DisplayCreated, addDrawCallback);
	} else {
		display->hide();
	}
}

SourcePreview::~SourcePreview() {
	CleanUp();
}

void SourcePreview::closeEvent(QCloseEvent* event) {
	QWidget::closeEvent(event);
	if (!event->isAccepted())
		return;

	CleanUp();
}

bool SourcePreview::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
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

void SourcePreview::CleanUp() {
	if (nativeSource) {
		obs_source_dec_showing(nativeSource);
	}

	obs_display_remove_draw_callback(display->GetDisplay(), DrawPreview, this);
}

} // namespace core::ui
