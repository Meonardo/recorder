#include "../core/app.h"

#include "TestMainWindow.h"

typedef std::function<void(bool finished)> VoidFunc;

class TestApp : public QApplication, public core::UIApplication {
public:
	TestApp(int& argc, char** argv) : QApplication(argc, argv) {}
	~TestApp() {}

	void AddConfigureCallback(VoidFunc cb, TestMainWindow* mainWindow) {
    connect(mainWindow, &TestMainWindow::destroyed, this, &TestApp::quit);
    this->cb = cb;
  }

	virtual int Execute() override { return exec(); }

  virtual void OnConfigureBegin() override {
    if (cb) {
      cb(false);
    }
  }
	virtual void OnConfigureFinished() override {
		if (cb) {
			cb(true);
		}
	}

private:
	VoidFunc cb;
};

int main(int argc, char* argv[]) {
	TestApp app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  TestMainWindow* mainWindow = new TestMainWindow;

	app.AddConfigureCallback([mainWindow](bool finished) {
    if (finished) {
      mainWindow->Prepare();
    }
	}, mainWindow);

  auto ret = CoreApp->Run(argc, argv, &app);

  return ret;
}
