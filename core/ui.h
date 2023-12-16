#pragma once

#include <obs-frontend-internal.hpp>

namespace core {
class UIApplication {
public:
	virtual ~UIApplication() = default;

	virtual int Execute() = 0;
	virtual void OnConfigureFinished() = 0;
};

class UIWindow {
public:
	UIWindow();
	~UIWindow();

private:
};
} // namespace core
