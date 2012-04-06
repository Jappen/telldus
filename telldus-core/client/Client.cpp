#include "Client.h"
#include "CallbackDispatcher.h"
#include "CallbackMainDispatcher.h"
#include "Socket.h"
#include "Strings.h"
#include "Mutex.h"

#include <list>

using namespace TelldusCore;

class Client::PrivateData {
public:
	Socket eventSocket;
	bool running, sensorCached, controllerCached;
	std::wstring sensorCache, controllerCache;
	TelldusCore::Mutex mutex;
	CallbackMainDispatcher callbackMainDispatcher;

	std::wstring eventEndpoint, clientEndpoint;
};

Client *Client::instance = 0;

/**
 * Initialize a new client instance.
 * If event endpoint is blank, no event loop is started!
 */
Client::Client(std::wstring client, std::wstring event)
	: Thread()
{
	d = new PrivateData;
	d->running = true;
	d->sensorCached = false;
	d->controllerCached = false;
	d->callbackMainDispatcher.start();
	d->clientEndpoint = client;
	d->eventEndpoint = event;

	if(d->eventEndpoint.size() > 0)
		start();
}

Client::~Client(void) {
	stopThread();
	wait();
	{
		TelldusCore::MutexLocker locker(&d->mutex);
	}
	delete d;
}

void Client::close() {
	if (Client::instance != 0) {
		delete Client::instance;
		Client::instance = 0;
	}
}

/**
 * Get current instance, if not initialized yet, create one with
 * default endpoints.
 */
Client *Client::getInstance() {
	return getInstance(TelldusCore::charToWstring(DEFAULT_CLIENT_ENDPOINT),
			TelldusCore::charToWstring(DEFAULT_EVENT_ENDPOINT));
}

/**
 * Get current instance, if not initialized yet, create one
 * with the specified endpoints.
 * Use getInstance() for initializing with default endpoints.
 */
Client *Client::getInstance(std::wstring client, std::wstring event) {
	if (Client::instance == 0) {
		Client::instance = new Client(client, event);
	}
	return Client::instance;
}

bool Client::getBoolFromService(const Message &msg) {
	return getIntegerFromService(msg) == TELLSTICK_SUCCESS;
}

int Client::getIntegerFromService(const Message &msg) {
	std::wstring response = sendToService(msg);
	if (response.compare(L"") == 0) {
		return TELLSTICK_ERROR_COMMUNICATING_SERVICE;
	}
	return Message::takeInt(&response);
}

std::wstring Client::getWStringFromService(const Message &msg) {
	std::wstring response = sendToService(msg);
	return Message::takeString(&response);
}

int Client::registerEvent( CallbackStruct::CallbackType type, void *eventFunction, void *context ) {
	return d->callbackMainDispatcher.registerCallback(type, eventFunction, context );
}

void Client::run(){
	//listen here
	d->eventSocket.connect(d->eventEndpoint);

	while(d->running){

		if(!d->eventSocket.isConnected()){
			d->eventSocket.connect(d->eventEndpoint);	//try to reconnect to service
			if(!d->eventSocket.isConnected()){
				//reconnect didn't succeed, wait a while and try again
				msleep(2000);
				continue;
			}
		}

		std::wstring clientMessage = d->eventSocket.read(1000);	//testing 5 second timeout

		while(clientMessage != L""){
			//a message arrived
			std::wstring type = Message::takeString(&clientMessage);
			if(type == L"TDDeviceChangeEvent"){
				DeviceChangeEventCallbackData *data = new DeviceChangeEventCallbackData();
				data->deviceId = Message::takeInt(&clientMessage);
				data->changeEvent = Message::takeInt(&clientMessage);
				data->changeType = Message::takeInt(&clientMessage);
				d->callbackMainDispatcher.retrieveCallbackEvent()->signal(data);

			} else if(type == L"TDDeviceEvent"){
				DeviceEventCallbackData *data = new DeviceEventCallbackData();
				data->deviceId = Message::takeInt(&clientMessage);
				data->deviceState = Message::takeInt(&clientMessage);
				data->deviceStateValue = TelldusCore::wideToString(Message::takeString(&clientMessage));
				d->callbackMainDispatcher.retrieveCallbackEvent()->signal(data);

			} else if(type == L"TDRawDeviceEvent"){
				RawDeviceEventCallbackData *data = new RawDeviceEventCallbackData();
				data->data = TelldusCore::wideToString(Message::takeString(&clientMessage));
				data->controllerId = Message::takeInt(&clientMessage);
				d->callbackMainDispatcher.retrieveCallbackEvent()->signal(data);

			} else if(type == L"TDSensorEvent"){
				SensorEventCallbackData *data = new SensorEventCallbackData();
				data->protocol = TelldusCore::wideToString(Message::takeString(&clientMessage));
				data->model = TelldusCore::wideToString(Message::takeString(&clientMessage));
				data->id = Message::takeInt(&clientMessage);
				data->dataType = Message::takeInt(&clientMessage);
				data->value = TelldusCore::wideToString(Message::takeString(&clientMessage));
				data->timestamp = Message::takeInt(&clientMessage);
				d->callbackMainDispatcher.retrieveCallbackEvent()->signal(data);

			} else if(type == L"TDControllerEvent") {
				ControllerEventCallbackData *data = new ControllerEventCallbackData();
				data->controllerId = Message::takeInt(&clientMessage);
				data->changeEvent = Message::takeInt(&clientMessage);
				data->changeType = Message::takeInt(&clientMessage);
				data->newValue = TelldusCore::wideToString(Message::takeString(&clientMessage));
				d->callbackMainDispatcher.retrieveCallbackEvent()->signal(data);

			} else {
				clientMessage = L"";  //cleanup, if message contained garbage/unhandled data
			}
		}
	}
}

std::wstring Client::sendToService(const Message &msg) {

	int tries = 0;
	std::wstring readData;
	while(tries < 20){
		tries++;
		if(tries == 20){
			TelldusCore::Message msg;
			msg.addArgument(TELLSTICK_ERROR_CONNECTING_SERVICE);
			return msg;
		}
		Socket s;
		s.connect(d->clientEndpoint);
		if (!s.isConnected()) { //Connection failed
			msleep(500);
			continue; //retry
		}
		s.write(msg.data());
		if (!s.isConnected()) { //Connection failed sometime during operation... (better check here, instead of 5 seconds timeout later)
			msleep(500);
			continue; //retry
		}
		readData = s.read(8000);  //TODO changed to 10000 from 5000, how much does this do...?
		if(readData == L""){
			msleep(500);
			continue; //TODO can we be really sure it SHOULD be anything?
			//TODO perhaps break here instead?
		}

		if (!s.isConnected()) { //Connection failed sometime during operation...
			msleep(500);
			continue; //retry
		}
		break;
	}

	return readData;
}

void Client::stopThread(){
	d->running = false;
	d->eventSocket.stopReadWait();
}

bool Client::unregisterCallback( int callbackId ) {
	return d->callbackMainDispatcher.unregisterCallback(callbackId);
}

int Client::getSensor(char *protocol, int protocolLen, char *model, int modelLen, int *sensorId, int *dataTypes) {
	if (!d->sensorCached) {
		Message msg(L"tdSensor");
		std::wstring response = Client::getWStringFromService(msg);
		int count = Message::takeInt(&response);
		d->sensorCached = true;
		d->sensorCache = L"";
		if (count > 0) {
			d->sensorCache = response;
		}
	}

	if (d->sensorCache == L"") {
		d->sensorCached = false;
		return TELLSTICK_ERROR_DEVICE_NOT_FOUND;
	}

	std::wstring p = Message::takeString(&d->sensorCache);
	std::wstring m = Message::takeString(&d->sensorCache);
	int id = Message::takeInt(&d->sensorCache);
	int dt = Message::takeInt(&d->sensorCache);

	if (protocol && protocolLen) {
		strncpy(protocol, TelldusCore::wideToString(p).c_str(), protocolLen);
	}
	if (model && modelLen) {
		strncpy(model, TelldusCore::wideToString(m).c_str(), modelLen);
	}
	if (sensorId) {
		(*sensorId) = id;
	}
	if (dataTypes) {
		(*dataTypes) = dt;
	}

	return TELLSTICK_SUCCESS;
}

int Client::getController(int *controllerId, int *controllerType, char *name, int nameLen, int *available) {
	if (!d->controllerCached) {
		Message msg(L"tdController");
		std::wstring response = Client::getWStringFromService(msg);
		int count = Message::takeInt(&response);
		d->controllerCached = true;
		d->controllerCache = L"";
		if (count > 0) {
			d->controllerCache = response;
		}
	}

	if (d->controllerCache == L"") {
		d->controllerCached = false;
		return TELLSTICK_ERROR_NOT_FOUND;
	}

	int id = Message::takeInt(&d->controllerCache);
	int type = Message::takeInt(&d->controllerCache);
	std::wstring n = Message::takeString(&d->controllerCache);
	int a = Message::takeInt(&d->controllerCache);

	if (controllerId) {
		(*controllerId) = id;
	}
	if (controllerType) {
		(*controllerType) = type;
	}
	if (name && nameLen) {
		strncpy(name, TelldusCore::wideToString(n).c_str(), nameLen);
	}
	if (available) {
		(*available) = a;
	}

	return TELLSTICK_SUCCESS;
}
