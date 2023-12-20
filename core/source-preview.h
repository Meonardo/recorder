#pragma once

#include "scene-source.h"

#include <QWidget>
#include <qboxlayout.h>
#include <QCloseEvent>

class OBSQTDisplay;

namespace core::ui {
class SourcePreview : public QWidget {
	Q_OBJECT
public:
	explicit SourcePreview(std::unique_ptr<core::Source> source, QWidget* parent = nullptr);

  // duplicate the source preview
  SourcePreview(const SourcePreview&);

	~SourcePreview();

	std::string GetSourceName() const { return source->Name(); }

	OBSSource GetNativeSource() const { return nativeSource; }

protected:
	virtual bool nativeEvent(const QByteArray& eventType, void* message,
				 qintptr* result) override;

private:
	std::unique_ptr<core::Source> source;
	OBSSource nativeSource;

	OBSQTDisplay* display;
	QBoxLayout* mainLayout;

  int copyCount = 1;

	void CleanUp();
};
} // namespace core::ui
