#include "EventUpdateManager.h"

#include "ConnectionListener.h"
#include "EventHandler.h"
#include "Message.h"
#include "Socket.h"
#include "Settings.h"

#include <list>
#include <memory>

typedef std::list<TelldusCore::Socket *> SocketList;

class EventUpdateManager::PrivateData {
public:
	TelldusCore::EventHandler eventHandler;
	TelldusCore::EventRef stopEvent, updateEvent, clientConnectEvent;
	SocketList clients;
	ConnectionListener *eventUpdateClientListener;
};

EventUpdateManager::EventUpdateManager()
	:Thread()
{
	d = new PrivateData;
	d->stopEvent = d->eventHandler.addEvent();
	d->updateEvent = d->eventHandler.addEvent();
	d->clientConnectEvent = d->eventHandler.addEvent();

	Settings set;
	d->eventUpdateClientListener = new ConnectionListener(set.getSetting(L"eventSocket"), d->clientConnectEvent);
}

EventUpdateManager::~EventUpdateManager(void) {
	d->stopEvent->signal();
	wait();
	delete d->eventUpdateClientListener;

	for (SocketList::iterator it = d->clients.begin(); it != d->clients.end(); ++it) {
		delete(*it);
	}

	delete d;
}

TelldusCore::EventRef EventUpdateManager::retrieveUpdateEvent(){

	return d->updateEvent;
}

void EventUpdateManager::run(){

	while(!d->stopEvent->isSignaled()){
		if (!d->eventHandler.waitForAny()) {
			continue;
		}

		if(d->clientConnectEvent->isSignaled()){
			//new client added
			TelldusCore::EventDataRef eventData = d->clientConnectEvent->takeSignal();
			ConnectionListenerEventData *data = reinterpret_cast<ConnectionListenerEventData*>(eventData.get());
			if(data){
				d->clients.push_back(data->socket);
			}
		}
		else if(d->updateEvent->isSignaled()){
			//device event, signal all clients
			TelldusCore::EventDataRef eventData = d->updateEvent->takeSignal();
			EventUpdateData *data = reinterpret_cast<EventUpdateData*>(eventData.get());
			if(data){
				sendMessageToClients(data);
			}
		}
	}
}

void EventUpdateManager::sendMessageToClients(EventUpdateData *data){

	int connected = 0;
	for(SocketList::iterator it = d->clients.begin(); it != d->clients.end();){
		if((*it)->isConnected()){
			connected++;
			TelldusCore::Message msg;

			if(data->messageType == L"TDDeviceEvent"){
				msg.addArgument("TDDeviceEvent");
				msg.addArgument(data->deviceId);
				msg.addArgument(data->eventState);
				msg.addArgument(data->eventValue);	//string
			}
			else if(data->messageType == L"TDDeviceChangeEvent"){
				msg.addArgument("TDDeviceChangeEvent");
				msg.addArgument(data->deviceId);
				msg.addArgument(data->eventDeviceChanges);
				msg.addArgument(data->eventChangeType);
			}
			else if(data->messageType == L"TDRawDeviceEvent"){
				msg.addArgument("TDRawDeviceEvent");
				msg.addArgument(data->eventValue);	//string
				msg.addArgument(data->controllerId);
			}
			else if(data->messageType == L"TDSensorEvent"){
				msg.addArgument("TDSensorEvent");
				msg.addArgument(data->protocol);
				msg.addArgument(data->model);
				msg.addArgument(data->sensorId);
				msg.addArgument(data->dataType);
				msg.addArgument(data->value);
				msg.addArgument(data->timestamp);
			}
			else if(data->messageType == L"TDControllerEvent") {
				msg.addArgument("TDControllerEvent");
				msg.addArgument(data->controllerId);
				msg.addArgument(data->eventState);
				msg.addArgument(data->eventChangeType);
				msg.addArgument(data->eventValue);
			}

			(*it)->write(msg);

			it++;
		}
		else{
			//connection is dead, remove it
			delete *it;
			it = d->clients.erase(it);
		}
	}
}
