#include "../core/app.h"

#include "TestMainWindow.h"

typedef std::function<void()> VoidFunc;

class TestApp : public QApplication, public core::UIApplication {
public:
	TestApp(int& argc, char** argv) : QApplication(argc, argv) {}
	~TestApp() {}

	void AddCallback(VoidFunc cb) { this->cb = cb; }

	virtual int Execute() override { return exec(); }
	virtual void OnConfigureFinished() override {
		if (cb) {
			cb();
		}
	}

private:
	VoidFunc cb;
};

int main(int argc, char* argv[]) {
	TestApp app(argc, argv);

  TestMainWindow mainWindow;

	app.AddCallback([&mainWindow]() {
    mainWindow.Prepare();
		mainWindow.show();
	});

	return CoreApp->Run(argc, argv, &app);
}
