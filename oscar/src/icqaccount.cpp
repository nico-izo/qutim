/****************************************************************************
 *  icqaccount.cpp
 *
 *  Copyright (c) 2010 by Nigmatullin Ruslan <euroelessar@gmail.com>
 *                        Prokhin Alexey <alexey.prokhin@yandex.ru>
 *
 ***************************************************************************
 *                                                                         *
 *   This library is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************
 *****************************************************************************/

#include "icqaccount_p.h"
#include "icqprotocol.h"
#include "icqcontact_p.h"
#include "oscarconnection.h"
#include "roster_p.h"
#include "buddycaps.h"
#include "oscarstatus.h"
#include "inforequest_p.h"
#include <qutim/status.h>
#include <qutim/systeminfo.h>
#include <qutim/objectgenerator.h>
#include <QTimer>

namespace qutim_sdk_0_3 {

namespace oscar {

PasswordValidator::PasswordValidator(QObject *parent) :
	QValidator(parent)
{
}

PasswordValidator::State PasswordValidator::validate(QString &input, int &pos) const
{
	if (input.isEmpty())
		return Intermediate;
	if (input.size() > 8)
		return Invalid;
	else
		return Acceptable;
}


QString IcqAccountPrivate::password()
{
	Q_Q(IcqAccount);
	QString password;
	if (!passwd.isEmpty()) {
		password = passwd;
		passwd.clear();
	} else {
		Config cfg = q->config("general");
		password = cfg.value("passwd", QString(), Config::Crypted);
		if (password.isEmpty()) {
			PasswordDialog *dialog = PasswordDialog::request(q);
			dialog->setValidator(new PasswordValidator(dialog));
			QObject::connect(dialog, SIGNAL(entered(QString,bool)), q, SLOT(onPasswordEntered(QString,bool)));
			QObject::connect(dialog, SIGNAL(rejected()), dialog, SLOT(deleteLater()));
		}
	}
	return password;
}

void IcqAccount::onPasswordEntered(const QString &password, bool remember)
{
	Q_D(IcqAccount);
	PasswordDialog *dialog = qobject_cast<PasswordDialog*>(sender());
	if (!dialog)
		return;
	if (remember) {
		Config cfg = config("general");
		cfg.setValue("passwd", password, Config::Crypted);
	}
	dialog->deleteLater();
	d->passwd = password;
	setStatus(d->lastStatus);
}

IcqAccount::IcqAccount(const QString &uin) :
	Account(uin, IcqProtocol::instance()), d_ptr(new IcqAccountPrivate)
{
	Q_D(IcqAccount);
	d->q_ptr = this;
	d->reconnectTimer.setSingleShot(true);
	connect(&d->reconnectTimer, SIGNAL(timeout()), SLOT(onReconnectTimeout()));
	Config cfg = config("general");
	d->conn = new OscarConnection(this);
	d->conn->registerHandler(d->feedbag = new Feedbag(this));
	foreach(const ObjectGenerator *gen, moduleGenerators<FeedbagItemHandler>())
		d->feedbag->registerHandler(gen->generate<FeedbagItemHandler>());
	d->conn->registerHandler(d->buddyPicture = new BuddyPicture(this, this));
	{
		Config statusCfg = cfg.group("lastStatus");
		int type = statusCfg.value("type", static_cast<int>(Status::Offline));
		if (type >= Status::Online && type <= Status::Offline) {
			OscarStatus status(Status::Offline);
			OscarStatus lastStatus;
			lastStatus.setType(static_cast<Status::Type>(type));
			lastStatus.setSubtype(statusCfg.value("subtype", 0));
			statusCfg.beginGroup("capabilities");
			foreach (const QString &type, statusCfg.childKeys()) {
				Capability cap(statusCfg.value("subtype", QString()));
				lastStatus.setCapability(cap, type);
			}
			statusCfg.endGroup();
			statusCfg.beginArray("extendedStatuses");
			for (int i = 0; i < statusCfg.arraySize(); ++i) {
				statusCfg.setArrayIndex(i);
				QString key = statusCfg.value("id").toString();
				if (key.isEmpty())
					continue;
				QVariantMap extendedStatus;
				statusCfg.beginGroup("data");
				foreach (const QString &key, statusCfg.childKeys())
					extendedStatus.insert(key, statusCfg.value(key));
				statusCfg.endGroup();
				lastStatus.setExtendedStatus(key, extendedStatus);
				status.setExtendedStatus(key, extendedStatus);
			}
			statusCfg.endArray();
			d->lastStatus = lastStatus;
			Account::setStatus(status);
		}
	}


	// ICQ UTF8 Support
	d->caps.append(ICQ_CAPABILITY_UTF8);
	// Buddy Icon
	d->caps.append(ICQ_CAPABILITY_AIMICON);
	// RTF messages
	//d->caps.append(ICQ_CAPABILITY_RTFxMSGS);
	// qutIM some shit
	d->caps.append(Capability(0x69716d75, 0x61746769, 0x656d0000, 0x00000000));
	d->caps.append(Capability(0x09461343, 0x4c7f11d1, 0x82224445, 0x53540000));
	// HTML messages
	d->caps.append(ICQ_CAPABILITY_HTMLMSGS);
	// ICQ typing
	d->caps.append(ICQ_CAPABILITY_TYPING);
	// Xtraz
	d->caps.append(ICQ_CAPABILITY_XTRAZ);
	// Messages on channel 2
	d->caps.append(ICQ_CAPABILITY_SRVxRELAY);
	// Short capability support
	d->caps.append(ICQ_CAPABILITY_SHORTCAPS);

	// qutIM version info
	DataUnit version;
	version.append(QByteArray("qutim"));
	version.append<quint8>(SystemInfo::getSystemTypeID());
	version.append<quint32>(qutimVersion());
	version.append<quint8>(0x00);
	version.append<quint32>(SystemInfo::getSystemVersionID());
	version.append<quint8>(0x00); // 5 bytes more to 16
	d->caps.append(Capability(version.data()));

	foreach(const ObjectGenerator *gen, moduleGenerators<RosterPlugin>()) {
		RosterPlugin *plugin = gen->generate<RosterPlugin>();
		d->rosterPlugins << plugin;
	}

	if (cfg.value("autoconnect", false))
		setStatus(d->lastStatus);
}

IcqAccount::~IcqAccount()
{
}

Feedbag *IcqAccount::feedbag()
{
	Q_D(IcqAccount);
	return d->feedbag;
}

AbstractConnection *IcqAccount::connection()
{
	return d_func()->conn;
}

const AbstractConnection *IcqAccount::connection() const
{
	return d_func()->conn;
}

void IcqAccount::setStatus(Status status_helper)
{
	Q_D(IcqAccount);
	OscarStatus status(status_helper);
	Status current = this->status();
	if (current.type() == status.type() && status.type() == Status::Offline) {
		emit statusAboutToBeChanged(status, current);
		d->reconnectTimer.stop();
		emit statusChanged(status);
		Account::setStatus(status);
		return;
	}
	emit statusAboutToBeChanged(status, current);
	if (status.type() == Status::Offline) {
		if (d->conn->isConnected()) {
			d->conn->disconnectFromHost(false);
			d->lastStatus = status;
		} else if (d->conn->error() == AbstractConnection::NoError ||
				   d->conn->error() == AbstractConnection::ReservationLinkError ||
				   d->conn->error() == AbstractConnection::ReservationMapError)
		{
			Config config = protocol()->config().group("reconnect");
			if (config.value("enabled", true)) {
				quint32 time = config.value("time", 3000);
				d->reconnectTimer.start(time);
			}
		} else if (d->conn->error() == AbstractConnection::MismatchNickOrPassword) {
			Account::setStatus(status);
			config().group("general").setValue("passwd", QString(), Config::Crypted);
			setStatus(d->lastStatus);
			return;
		}
		OscarStatus stat;
		foreach(IcqContact *contact, d->contacts) {
			contact->setStatus(stat);
			foreach (RosterPlugin *plugin, d->rosterPlugins)
				plugin->statusChanged(contact, status, TLVMap());
		}
	} else {
		d->lastStatus = status;
		if (current == Status::Offline) {
			d->reconnectTimer.stop();
			QString pass = d->password();
			if (!pass.isEmpty()) {
				status = Status::Connecting;
				d->conn->connectToLoginServer(pass);
			} else {
				status = Status::Offline;
			}
		} else {
			d->conn->sendStatus(status);
		}
	}
	{
		Config statusCfg = config().group("general/lastStatus");
		statusCfg.setValue("type", d->lastStatus.type());
		statusCfg.setValue("subtype", d->lastStatus.subtype());
		statusCfg.remove("capabilities");
		statusCfg.beginGroup("capabilities");
		QHashIterator<QString, Capability> itr(d->lastStatus.capabilities());
		while (itr.hasNext()) {
			itr.next();
			statusCfg.setValue(itr.key(), itr.value().toString());
		}
		statusCfg.endGroup();
		statusCfg.remove("extendedStatuses");
		statusCfg.beginArray("extendedStatuses");
		int i = 0;
		QHashIterator<QString, QVariant> extStatusItr(d->lastStatus.extendedStatuses());
		while (extStatusItr.hasNext()) {
			extStatusItr.next();
			statusCfg.setArrayIndex(i);
			statusCfg.setValue("id", extStatusItr.key());
			statusCfg.beginGroup("data");
			QVariantMap extStatus = extStatusItr.value().toMap();
			QMapIterator<QString, QVariant> itr(extStatus);
			while (itr.hasNext()) {
				itr.next();
				statusCfg.setValue(itr.key(), itr.value());
			}
			statusCfg.endGroup();
		}
		statusCfg.endArray();
	}
	emit statusChanged(status);
	Account::setStatus(status);
}

void IcqAccount::setStatus(OscarStatusEnum status)
{
	setStatus(OscarStatus(status));
}

QString IcqAccount::name() const
{
	Q_D(const IcqAccount);
	if (!d->name.isEmpty())
		return d->name;
	else
		return id();
}

QString IcqAccount::avatar() const
{
	return d_func()->avatar;
}

void IcqAccount::setAvatar(const QString &avatar)
{
	d_func()->buddyPicture->setAccountAvatar(avatar);
}

ChatUnit *IcqAccount::getUnit(const QString &unitId, bool create)
{
	return getContact(unitId, create);
}

IcqContact *IcqAccount::getContact(const QString &id, bool create)
{
	Q_D(IcqAccount);
	IcqContact *contact = d->contacts.value(id);
	if (create && !contact) {
		contact = new IcqContact(id, this);
		d->contacts.insert(id, contact);
		connect(contact, SIGNAL(destroyed()), SLOT(onContactRemoved()));
		emit contactCreated(contact);
		//if (ContactList::instance())
		//	ContactList::instance()->addContact(contact);
	}
	return contact;
}

const QHash<QString, IcqContact*> &IcqAccount::contacts() const
{
	Q_D(const IcqAccount);
	return d->contacts;
}

void IcqAccount::setCapability(const Capability &capability, const QString &type)
{
	Q_D(IcqAccount);
	if (type.isEmpty())
		d->caps.push_back(capability);
	else
		d->typedCaps.insert(type, capability);
}

bool IcqAccount::removeCapability(const Capability &capability)
{
	Q_D(IcqAccount);
	return d->caps.removeOne(capability);
}

bool IcqAccount::removeCapability(const QString &type)
{
	Q_D(IcqAccount);
	return d->typedCaps.remove(type) > 0;
}

bool IcqAccount::containsCapability(const Capability &capability) const
{
	Q_D(const IcqAccount);
	if (d->caps.contains(capability))
		return true;
	foreach (const Capability &cap, d->typedCaps) {
		if (cap == capability)
			return true;
	}
	return false;
}

bool IcqAccount::containsCapability(const QString &type) const
{
	Q_D(const IcqAccount);
	return d->typedCaps.contains(type);
}

QList<Capability> IcqAccount::capabilities() const
{
	Q_D(const IcqAccount);
	QList<Capability> caps = d->caps;
	foreach (const Capability &cap, d->typedCaps)
		caps << cap;
	return caps;
}

void IcqAccount::registerRosterPlugin(RosterPlugin *plugin)
{
	Q_D(IcqAccount);
	d->rosterPlugins << plugin;
}

void IcqAccount::updateSettings()
{
	emit settingsUpdated();
}

void IcqAccount::onReconnectTimeout()
{
	Q_D(IcqAccount);
	if (status() == Status::Offline)
		setStatus(d->lastStatus);
}

void IcqAccount::onContactRemoved()
{
	Q_D(IcqAccount);
	IcqContact *contact = reinterpret_cast<IcqContact*>(sender());
	QHash<QString, IcqContact *>::iterator itr = d->contacts.begin();
	QHash<QString, IcqContact *>::iterator endItr = d->contacts.end();
	while (itr != endItr) {
		if (*itr == contact) {
			d->contacts.erase(itr);
			break;
		}
		++itr;
	}
	Q_ASSERT(itr != endItr);
}

bool IcqAccount::event(QEvent *ev)
{
	if (ev->type() == InfoRequestCheckSupportEvent::eventType()) {
		Status::Type status = this->status().type();
		if (status >= Status::Online && status <= Status::Invisible) {
			InfoRequestCheckSupportEvent *event = static_cast<InfoRequestCheckSupportEvent*>(ev);
			event->setSupportType(InfoRequestCheckSupportEvent::ReadWrite);
			event->accept();
		} else {
			ev->ignore();
		}
	} else if (ev->type() == InfoRequestEvent::eventType()) {
		InfoRequestEvent *event = static_cast<InfoRequestEvent*>(ev);
		event->setRequest(new IcqInfoRequest(this));
		event->accept();
	} else if (ev->type() == InfoItemUpdatedEvent::eventType()) {
		InfoItemUpdatedEvent *event = static_cast<InfoItemUpdatedEvent*>(ev);
		MetaInfoValuesHash values = MetaInfoField::dataItemToHash(event->infoItem(), true);
		UpdateAccountInfoMetaRequest *request = new UpdateAccountInfoMetaRequest(this, values);
		connect(request, SIGNAL(infoUpdated()), request, SLOT(deleteLater()));
		request->send();
		event->accept();
	}
	return Account::event(ev);
}

} } // namespace qutim_sdk_0_3::oscar
