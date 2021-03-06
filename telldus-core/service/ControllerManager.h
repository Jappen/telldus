#ifndef CONTROLLERMANAGER_H
#define CONTROLLERMANAGER_H

#include "Event.h"
class Controller;

#include <string>

class ControllerManager {
public:
	ControllerManager(TelldusCore::EventRef event, TelldusCore::EventRef updateEvent);
	~ControllerManager(void);

	void deviceInsertedOrRemoved(int vid, int pid, const std::string &serial, bool inserted);

	Controller *getBestControllerById(int id);
	void loadControllers();
	void loadStoredControllers();
	void queryControllerStatus();
	int resetController(Controller *controller);

	std::wstring getControllers() const;
	std::wstring getControllerValue(int id, const std::wstring &name);
	int removeController(int id);
	int setControllerValue(int id, const std::wstring &name, const std::wstring &value);

private:
	void signalControllerEvent(int controllerId, int changeEvent, int changeType, const std::wstring &newValue);
	class PrivateData;
	PrivateData *d;
};

#endif //CONTROLLERMANAGER_H
