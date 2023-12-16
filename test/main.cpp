#include "../core/app.h"

#include "test.h"
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

#if TEST
int main(int argc, char* argv[]) {
#else
int main2(int argc, char* argv[]) {
#endif
	TestApp app(argc, argv);

  TestMainWindow mainWindow;

	app.AddCallback([&mainWindow]() {
    mainWindow.Prepare();
	});

	return CoreApp->Run(argc, argv, &app);
}
