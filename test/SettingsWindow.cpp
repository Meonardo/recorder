#include "SettingsWindow.h"

#include "../core/app.h"
#include "../core/output.h"

#include <map>

static std::map<std::string, std::string> model_map = {
  {"TCMono Depth", "tcmonodepth_tcsmallnet_192x320.onnx"},
  {"SINet", "SINet_Softmax_simple.onnx"},
  {"MediaPipe", "mediapipe.onnx"},
  {"Selfie Segmentation", "selfie_segmentation.onnx"},
  {"PPHumanSeg", "pphumanseg_fp32.onnx"},
  {"Robust Video Matting", "rvm_mobilenetv3_fp32.onnx"},
};

SettingsWindow::SettingsWindow(const core::Source& source, QWidget* parent)
  : QWidget(parent),
    ui(new Ui::SettingsWindowClass()),
    source(source) {
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose);

	for (auto& [k, v] : model_map) { ui->modelComboBox->addItem(QString::fromStdString(k)); }

	connect(ui->confirmButton, &QPushButton::clicked, this, [&]() {
		bool useCpu = ui->useCpuCheckBox->isChecked();
		std::string model = model_map[ui->modelComboBox->currentText().toStdString()];

		if (this->source.Type() == core::kSourceTypeCamera) {
			core::CameraSource cameraSource(source.Name(), source.ID());
			cameraSource.ApplyBackgroundRemoval(model, useCpu, true);
		} else if (this->source.Type() == core::kSourceTypeRTSP) {
			core::RTSPSource rtspSource(source.Name(), source.ID());
			rtspSource.ApplyBackgroundRemoval(model, useCpu, true);
		} else {
			return;
		}

		this->close();
	});

	connect(ui->cancelButton, &QPushButton::clicked, this, [&]() {
		if (this->source.Type() == core::kSourceTypeCamera) {
			core::CameraSource cameraSource(source.Name(), source.ID());
			cameraSource.ApplyBackgroundRemoval("model", false, false);
		} else if (this->source.Type() == core::kSourceTypeRTSP) {
			core::RTSPSource rtspSource(source.Name(), source.ID());
			rtspSource.ApplyBackgroundRemoval("model", false, false);
		} else {
			return;
		}
		this->close();
	});

	connect(ui->ratio169CheckBox, &QCheckBox::clicked, this,
		[&]() { ui->ratio329CheckBox->setChecked(!ui->ratio169CheckBox->isChecked()); });

	connect(ui->ratio329CheckBox, &QCheckBox::clicked, this,
		[&]() { ui->ratio169CheckBox->setChecked(!ui->ratio329CheckBox->isChecked()); });

	connect(ui->saveButton, &QPushButton::clicked, this, [&]() {
		if (ui->ratio169CheckBox->isChecked()) {
			CoreApp->ResetVideo(1920, 1080);
		} else if (ui->ratio329CheckBox->isChecked()) {
			CoreApp->ResetVideo(3840, 1080);
		}

		CoreApp->ResetOutputs();

		emit OnOutputSizeChanged();

		this->close();
	});

	connect(ui->startVirtualButton, &QPushButton::clicked, this,
		[this]() { CoreApp->GetOutputManager()->StartVirtualCam(); });

	connect(ui->stopVirtualButton, &QPushButton::clicked, this,
		[this]() { CoreApp->GetOutputManager()->StopVirtualCam(); });
}

SettingsWindow::~SettingsWindow() {
	delete ui;
}
