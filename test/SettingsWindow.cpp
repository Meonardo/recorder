#include "SettingsWindow.h"

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
		} else {
			core::RTSPSource rtspSource(source.Name(), source.ID());
			rtspSource.ApplyBackgroundRemoval(model, useCpu, true);
		}

		this->close();
	});

	connect(ui->cancelButton, &QPushButton::clicked, this, [&]() {
		if (this->source.Type() == core::kSourceTypeCamera) {
			core::CameraSource cameraSource(source.Name(), source.ID());
			cameraSource.ApplyBackgroundRemoval("model", false, false);
		} else {
			core::RTSPSource rtspSource(source.Name(), source.ID());
			rtspSource.ApplyBackgroundRemoval("model", false, false);
		}
		this->close();
	});
}

SettingsWindow::~SettingsWindow() {
	delete ui;
}
