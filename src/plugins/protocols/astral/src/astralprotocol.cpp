/****************************************************************************
**
** qutIM - instant messenger
**
** Copyright © 2011 Ruslan Nigmatullin <euroelessar@yandex.ru>
**
*****************************************************************************
**
** $QUTIM_BEGIN_LICENSE$
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see http://www.gnu.org/licenses/.
** $QUTIM_END_LICENSE$
**
****************************************************************************/
#include "astralprotocol.h"

struct AstralProtocolPrivate
{
	ConnectionManagerPtr conn_mgr;
	AccountManagerPtr acc_mgr;
	QHash<QString, QPointer<AstralAccount> > accounts;
	QMetaObject* meta;
};

AstralProtocol::AstralProtocol(ConnectionManagerPtr manager, QMetaObject *meta) : p(new AstralProtocolPrivate)
{
	p->conn_mgr = manager;
	p->meta = meta;
	//QObject::d_ptr->metaObject = reinterpret_cast<QDynamicMetaObjectData*>(meta);
}

AstralProtocol::~AstralProtocol()
{
	//QObject::d_ptr->metaObject = 0;
}

QList<qutim_sdk_0_3::Account *> AstralProtocol::accounts() const
{
	QList<qutim_sdk_0_3::Account*> accounts;
	foreach(qutim_sdk_0_3::Account *acc, p->accounts)
	{
		if(acc)
			accounts << acc;
	}
	return accounts;
}

qutim_sdk_0_3::Account *AstralProtocol::account(const QString &id) const
{
	return p->accounts.value(id);
}

AccountCreationWizard *AstralProtocol::accountCreationWizard()
{
	return 0;
}

ConnectionManagerPtr AstralProtocol::connectionManager()
{
	return p->conn_mgr;
}

AccountManagerPtr AstralProtocol::accountManager()
{
	return p->acc_mgr;
}

const QMetaObject *AstralProtocol::metaObject() const
{
	return p->meta;
}

void AstralProtocol::loadAccounts()
{
	QStringList accounts = config("general").value("accounts", QStringList());
	foreach(const QString &id, accounts) {
		qDebug() << "accountCreated!" << id;
		auto a = new AstralAccount(this, id);
		p->accounts.insert(id, a);
		emit accountCreated(a);
		//connect(a, SIGNAL(destroyed(QObject*)), this, SLOT(onAccountRemoved(QObject*)));
	}
}

// from Qt5's qmetaobjectbuilder.cpp
static void writeString(char *out, int i, const QByteArray &str,
		const int offsetOfStringdataMember, int &stringdataOffset)
{
	int size = str.size();
	qptrdiff offset = offsetOfStringdataMember + stringdataOffset
		- i * sizeof(QByteArrayData);
	const QByteArrayData data =
		Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(size, offset);
	memcpy(out + i * sizeof(QByteArrayData), &data, sizeof(QByteArrayData));
	memcpy(out + offsetOfStringdataMember + stringdataOffset, str.constData(), size);
	out[offsetOfStringdataMember + stringdataOffset + size] = '\0';
	stringdataOffset += size + 1;
}

/**
 * Magic happens here.
 * \see QuetzalMetaObject::QuetzalMetaObject(PurplePlugin *protocol)
 */
AstralMetaObject::AstralMetaObject(ConnectionManagerPtr manager, ProtocolInfo *protocol)
{
	uint *data = (uint*) calloc(17, sizeof(uint));

	data[ 0] = 7;  // revision
	data[ 1] = 0;  // classname (the first string)

	data[ 2] = 1;  // classinfo count
	data[ 3] = 14; // classinfo data

	data[ 4] = 0;  // methods count
	data[ 5] = 0;  // methods data

	data[ 6] = 0;  // properties count
	data[ 7] = 0;  // properties data

	data[ 8] = 0;  // enums/sets count
	data[ 9] = 0;  // enums/sets data

	data[10] = 0;  // constructors count
	data[11] = 0;  // constructors data

	data[12] = 0;  // flags

	data[13] = 0;  // signal count

	QByteArray className("astral::");
	className += manager->name().toLatin1();
	className += "::";
	className += protocol->name().replace('-', '_').toLatin1();
	const QByteArray keyStr("Protocol");
	const QByteArray valueStr(protocol->name().toLatin1());

	// because we have ONE classname, ONE classInfoName and ONE classInfoValue
	int offsetOfStringdataMember = 3 * sizeof(QByteArrayData);
	int stringdataOffset = 0;
	char* stringData = new char[offsetOfStringdataMember + className.size() + 1 + keyStr.size() + 1 + valueStr.size() + 1];
	writeString(stringData, /*index*/0, className, offsetOfStringdataMember, stringdataOffset);
	writeString(stringData, 1, keyStr, offsetOfStringdataMember, stringdataOffset);
	writeString(stringData, 2, valueStr, offsetOfStringdataMember, stringdataOffset);

	data[14] = 1; // because 0 is ClassName
	data[15] = 2; //

	data[16] = 0; // eods

	d.superdata = &AstralProtocol::staticMetaObject;
	d.stringdata = reinterpret_cast<const QByteArrayData*>(stringData);
	d.data = data;
	d.relatedMetaObjects = 0;
	d.extradata = 0;
}

