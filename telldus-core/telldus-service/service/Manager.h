#ifndef MANAGER_H
#define MANAGER_H

#include "Message.h"
#include <QThread>
#include <QVariant>

class ManagerPrivate;
class Socket;

class Manager : public QThread {
	Q_OBJECT
public:
	Manager(Socket *s, QObject *parent);
	~Manager(void);

protected:
	QVariant parseMessage(const std::string &message);
	void run();

signals:
	void done();
	
private:
	ManagerPrivate *d;
};

#endif //MANAGER_H
