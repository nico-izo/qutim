/****************************************************************************
 *  filetransfer.cpp
 *
 *  Copyright (c) 2010 by Prokhin Alexey <alexey.prokhin@yandex.ru>
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

#include "filetransfer.h"
#include "buddycaps.h"
#include "icqcontact.h"
#include "tlv.h"
#include "icqaccount.h"
#include "oscarconnection.h"
#include <QHostAddress>

#include <QTimer>

namespace qutim_sdk_0_3 {

namespace oscar {

const int BUFFER_SIZE = 4096;
using namespace Util;

OftHeader::OftHeader()
{
	m_state = ReadHeader;
	encrypt = false;
	compress = false;
	checksum = 0xFFFF0000;
	resourceForkChecksum = 0xFFFF0000;
	receivedChecksum = 0xFFFF0000;
	identification = "Cool FileXfer";
	bytesReceived = 0;
	flags = 0x20;
	totalFiles = 1;
	filesLeft = 1;
	totalParts = 1;
	partsLeft = 1;
	totalSize = 0;
}

void OftHeader::readData(QIODevice *dev)
{
	if (m_state == ReadHeader) {
		DataUnit data(dev->read(6));
		/*quint32 protVersion = */data.read<quint32>();  // TODO: test it
		m_length = data.read<quint16>() - 6;
		m_state = ReadData;
		m_data.resize(m_length);
	}
	if (m_state == ReadData) {
		char *data = m_data.data() + m_data.size() - m_length;
		int readed = dev->read(data, m_length);
		m_length -= readed;
	}
	if (m_length == 0) {
		DataUnit data(m_data);
		type = static_cast<OftPacketType>(data.read<quint16>());
		cookie = data.read<quint64>();
		encrypt = data.read<quint16>();
		compress = data.read<quint16>();
		totalFiles = data.read<quint16>();
		filesLeft = data.read<quint16>();
		totalParts = data.read<quint16>();
		partsLeft = data.read<quint16>();
		totalSize = data.read<quint32>();
		size = data.read<quint32>();
		modTime = data.read<quint32>();
		checksum = data.read<quint32>();
		receivedResourceForkChecksum = data.read<quint32>();
		resourceForkSize = data.read<quint32>();
		creationTime = data.read<quint32>();
		resourceForkChecksum = data.read<quint32>();
		bytesReceived = data.read<quint32>();
		receivedChecksum = data.read<quint32>();
		identification = QString::fromLatin1(data.readData(32));
		flags = data.read<quint8>();
		data.skipData(71); // skipped List Name Offset, List Size Offset and "Dummy" block
		macFileInfo = data.readData(16);
		quint16 encoding = data.read<quint16>();
		//quint16 encodingSubcode = data.read<quint16>();
		data.skipData(2);
		QTextCodec *codec = 0;
		if (encoding == CodecUtf16Be)
			codec = utf16Codec();
		else
			codec = asciiCodec();
		QByteArray name = data.readAll();
		if (name.size() == 64) {
			uint c = qstrnlen(name.constData(), name.size());
			name.resize(c);
		} else if (encoding == CodecUtf16Be) {
			name.chop(2);
		} else {
			name.chop(1);
		}
		fileName = codec->toUnicode(name);
		m_state = Finished;
	}
}

void OftHeader:: writeData(QIODevice *dev)
{
	DataUnit data;
	debug() << "Outgoing oft message with type" << hex << type;
	data.append<quint16>(type);
	data.append<quint64>(cookie);
	data.append<quint16>(encrypt);
	data.append<quint16>(compress);
	data.append<quint16>(totalFiles);
	data.append<quint16>(filesLeft);
	data.append<quint16>(totalParts);
	data.append<quint16>(partsLeft);
	data.append<quint32>(totalSize);
	data.append<quint32>(size);
	data.append<quint32>(modTime);
	data.append<quint32>(checksum);
	data.append<quint32>(receivedResourceForkChecksum);
	data.append<quint32>(resourceForkSize);
	data.append<quint32>(creationTime);
	data.append<quint32>(resourceForkChecksum);
	data.append<quint32>(bytesReceived);
	data.append<quint32>(receivedChecksum);
	{
		QByteArray ident = identification.toLatin1();
		ident.resize(32);
		data.append(ident);
	}
	data.append<quint8>(flags);
	data.append<quint8>(0x1C);
	data.append<quint8>(0x11);
	{
		QByteArray dummy;
		dummy.resize(69);
		data.append(dummy);
	}
	macFileInfo.resize(16);
	data.append(macFileInfo);
	data.append<quint16>(CodecAnsi);
	data.append<quint16>(0);
	{
		QByteArray name = defaultCodec()->fromUnicode(fileName);
		if (name.size() < 64)
			name.resize(64);
		else
			name.append("\0");
		data.append(name);
	}
	DataUnit header;
	header.append<quint32>(0x4F465432); // Protocol version: "OFT2"
	header.append<quint16>(data.dataSize() + 6);
	header.append(data);
	dev->write(header.data());
}

OftSocket::OftSocket(QObject *parent) :
	QTcpSocket(parent)
{
	m_state = ReadHeader;
	connect(this, SIGNAL(readyRead()), SLOT(readData()));
	m_len = 0;
}

OftSocket::OftSocket(int socketDescriptor, QObject *parent) :
	QTcpSocket(parent)
{
	m_state = ReadHeader;
	setSocketDescriptor(socketDescriptor);
	connect(this, SIGNAL(readyRead()), SLOT(readData()));
	m_len = 0;
}

void OftSocket::directConnect(const QHostAddress &addr, quint16 port)
{
	m_state = ReadHeader;
	QObject::disconnect(this, SIGNAL(connected()), this, SLOT(connected()));
	QObject::connect(this, SIGNAL(connected()), this, SIGNAL(initialized()));
	connectToHost(addr, port);
}

void OftSocket::proxyConnect(const QString &uin, quint64 cookie, const QHostAddress &addr,
							 quint16 port, quint16 clientPort)
{
	m_state = clientPort == 0 ? ProxyInit : ProxyReceive;
	m_lastHeader = OftHeader();
	m_len = 0;
	QObject::disconnect(this, SIGNAL(connected()), this, SIGNAL(initialized()));
	QObject::connect(this, SIGNAL(connected()), this, SLOT(connected()));
	m_uin = uin;
	m_cookie = cookie;
	m_proxyPort = clientPort;
	connectToHost(addr, port);
	debug().nospace() << "Trying to connect to the proxy "
			<< addr.toString().toLocal8Bit().constData()
			<< ":" << 5190;
}

void OftSocket::dataReaded()
{
	m_state = ReadHeader;
	m_lastHeader = OftHeader();
	if (bytesAvailable() > 0)
		readData();
}

void OftSocket::readData()
{
	if (m_state & Proxy) {
		DataUnit data;
		if (m_len == 0) {
			if (bytesAvailable() <= 4)
				return;
			data.setData(read(4));
			m_len = data.read<quint16>() - 2;
			if (data.read<quint16>() != 0x044A)
				debug() << "Unknown proxy protocol version";
		}
		if (bytesAvailable() <= m_len) {
			data.setData(read(m_len));
			m_len = 0;
		} else {
			return;
		}
		quint16 type = data.read<quint16>();
		data.skipData(4); // unknown
		quint16 flags = data.read<quint16>();
		Q_UNUSED(flags);
		debug() << "Rendezvous proxy packet. Type" << type;
		switch (type) {
		case 0x0001 : { // error
			quint16 code = data.read<quint16>();
			QString str;
			if (code == 0x000d || code == 0x000e)
				str = "Bad Request";
			else if (code == 0x0010)
				str = "Initial Request Timed Out";
			else if (code == 0x001a)
				str = "Accept Period Timed Out";
			else
				str = QString("Unknown rendezvous proxy error: %1").arg(code);
			setSocketError(QAbstractSocket::ProxyProtocolError);
			setErrorString(str);
			emit error(QAbstractSocket::ProxyProtocolError);
			break;
		}
		case 0x0003 : { // Acknowledge
			if (m_state != ProxyInit) {
				setSocketError(QAbstractSocket::ProxyProtocolError);
				setErrorString("Rendezvous proxy acknowledge packets are forbidden");
				emit error(QAbstractSocket::ProxyProtocolError);
				break;
			}
			m_proxyPort = data.read<quint16>();
			m_proxyIP.setAddress(data.read<quint32>());
			emit proxyInitialized();
			break;
		}
		case 0x0005 : { // Ready
			m_state = ReadHeader;
			emit initialized();
			break;
		}
		default:
			setSocketError(QAbstractSocket::ProxyProtocolError);
			setErrorString(QString("Unknown rendezvous proxy request").arg(type));
			emit error(QAbstractSocket::ProxyProtocolError);
		}
	} else {
		if (m_state == ReadHeader) {
			m_lastHeader.readData(this);
			if (m_lastHeader.isFinished()) {
				m_state = ReadData;
				emit headerReaded(m_lastHeader);
			}
		}
		if (m_state == ReadData && bytesAvailable() > 0)
			emit newData();
	}
}

void OftSocket::connected()
{
	Q_ASSERT(m_state & Proxy);
	DataUnit data;
	data.append<quint8>(m_uin, asciiCodec()); // uin or screen name
	if (m_state == ProxyReceive)
		data.append<quint16>(m_proxyPort);
	data.append<quint64>(m_cookie);
	data.appendTLV(0x0001, ICQ_CAPABILITY_AIMSENDFILE); // capability
	DataUnit header;
	header.append<quint16>(10 + data.dataSize());
	header.append<quint16>(0x044A); // proto version
	header.append<quint16>(m_state == ProxyInit ? 0x0002 : 0x0004); // request cmd
	header.append<quint32>(0); // unknown
	header.append<quint16>(0); // flags
	header.append(data.data());
	write(header.data());
	flush();
}

OftServer::OftServer(OftConnection *conn) :
	m_conn(conn)
{
}

void OftServer::incomingConnection(int socketDescriptor)
{
	OftSocket *socket = new OftSocket(socketDescriptor);
	debug().nospace() << "Incoming oscar transfer connection from "
			<< socket->peerAddress().toString().toLatin1().constData()
			<< ":" << socket->peerPort();
	m_conn->setSocket(socket);
	m_conn->connected();
	close();
}

OftConnection::OftConnection(IcqContact *contact, quint64 cookie, FileTransfer *transfer) :
	m_contact(contact), m_cookie(cookie), m_transfer(transfer), m_server(this), m_proxy(false)
{
	m_receiving = true;
}

void OftConnection::sendFile(const QFileInfo &file)
{
	m_currentFile = file;
	m_receiving = false;
	m_stage = 1;
	if (!m_proxy) {
		//m_server.listen();
		sendFileRequest();
	} else {
		setSocket(new OftSocket(this));
		initProxyConnection();
	}
}

void OftConnection::initProxyConnection()
{
	// Connect to ars.oscar.aol.com
	m_socket->proxyConnect(m_contact->account()->id(), m_cookie, QHostAddress("64.12.201.185"), 5190);
}

void OftConnection::handleRendezvous(quint16 reqType, const TLVMap &tlvs)
{
	if (reqType == MsgRequest) {
		debug() << m_contact->id() << "sent file transfer request";
		m_stage = tlvs.value<quint16>(0x000A);
		m_proxyIP.setAddress(tlvs.value<quint32>(0x0002));
		m_clientIP.setAddress(tlvs.value<quint32>(0x0003));
		m_verifiedIP.setAddress(tlvs.value<quint32>(0x0004));
		quint16 port = tlvs.value<quint16>(0x0005);
		m_proxy = tlvs.value<quint8>(0x0010);
		DataUnit tlv2711(tlvs.value(0x2711));
		bool multipleFiles = tlv2711.read<quint16>() > 1;
		quint16 files = tlv2711.read<quint16>();
		quint32 bytes = tlv2711.read<quint32>();
		Q_UNUSED(multipleFiles);
		Q_UNUSED(files);
		Q_UNUSED(bytes);
		QTextCodec *codec;
		{
			QByteArray encoding = tlvs.value(0x2722);
			if (!encoding.isEmpty())
				codec = QTextCodec::codecForName(encoding.constData());
			if (!codec)
				codec = defaultCodec();
		}
		QString name = codec->toUnicode(tlv2711.readAll());
		name.chop(1);
		QString errorStr;
		if (m_stage == 1) {
			if (m_receiving) {
				setSocket(new OftSocket(this));
				if (!m_proxy) {
					m_socket->directConnect(m_clientIP, port);
					debug().nospace() << "Trying to connect to the receiver "
							<< m_clientIP.toString().toLocal8Bit().constData()
							<< ":" << port;
				} else {
					m_socket->proxyConnect(m_contact->id(), m_cookie, m_proxyIP, 5190, port);
				}
			} else {
				errorStr = "Stage 1 oscar file transfer request is forbidden during file sending";
			}
		} else if (m_stage == 2) {
			if (!m_receiving) {
				m_server.close();
				if (m_socket) {
					debug() << "Sender has sent the request for reverse connection (stage 2)"
							<< "but the connection already initialized at stage 1";
					return;
				}
				setSocket(new OftSocket(this));
				if (!m_proxy) {
					m_socket->directConnect(m_clientIP, port);
					debug().nospace() << "Trying to connect to the sender "
							<< m_clientIP.toString().toLocal8Bit().constData()
							<< ":" << port;
				} else {
					m_socket->proxyConnect(m_contact->id(), m_cookie, m_proxyIP, 5190);
				}
			} else {
				errorStr = "Stage 2 oscar file transfer request is forbidden during file receiving";
			}
		} else if (m_stage == 3) {
			if (m_receiving) {
				m_proxy = true;
				setSocket(new OftSocket(this));
				m_socket->proxyConnect(m_contact->id(), m_cookie, m_proxyIP, 5190, port);
			} else {
				errorStr = "Stage 3 oscar file transfer request is forbidden during file sending";
			}
		} else {
			errorStr = QString("Unknown file transfer request at stage %1").arg(m_stage);
		}
		if (!errorStr.isEmpty()) {
			debug() << errorStr;
			close();
			return;
		}
	} else if (reqType == MsgAccept) {
		debug() << m_contact->id() << "accepted file transfing";
	} else if (reqType == MsgCancel) {
		debug() << m_contact->id() << "canceled file transfing";
		close();
	}
}

void OftConnection::setSocket(OftSocket *socket)
{
	if (!m_socket) {
		m_socket = socket;
		m_socket->setParent(this);
		connect(m_socket.data(), SIGNAL(proxyInitialized()), SLOT(sendFileRequest()));
		connect(m_socket.data(), SIGNAL(initialized()), SLOT(connected()));
		connect(m_socket.data(), SIGNAL(error(QAbstractSocket::SocketError)),
				SLOT(error(QAbstractSocket::SocketError)));
		connect(m_socket.data(), SIGNAL(headerReaded(OftHeader)), SLOT(headerReaded()));
		connect(m_socket.data(), SIGNAL(newData()), SLOT(newData()));
		if (m_socket->readingState() == OftSocket::ReadData) {
			headerReaded();
			if (m_socket->bytesAvailable() > 0)
				newData();
		}
	} else {
		debug() << "Cannot change socket in an initialized oscar file transfer connection";
	}
}

void OftConnection::sendFileRequest(bool fileinfo)
{
	IcqAccount *account = contact()->account();
	Channel2BasicMessageData data(MsgRequest, ICQ_CAPABILITY_AIMSENDFILE, m_cookie);
	quint32 clientAddr = 0;
	quint32 proxyAddr;
	quint16 port;
	if (m_proxy) {
		proxyAddr = m_socket->proxyIP().toIPv4Address();
		port = m_socket->proxyPort();
	} else {
		m_server.listen(); // ???
		clientAddr = account->localAddress().toIPv4Address();
		proxyAddr = clientAddr;
		port = m_server.serverPort();
	}
	data.appendTLV<quint16>(0x000A, m_stage);
	data.appendTLV<quint32>(0x0002, proxyAddr);
	data.appendTLV<quint32>(0x0016, proxyAddr ^ 0x0FFFFFFFF);
	data.appendTLV<quint32>(0x0003, clientAddr);
	data.appendTLV<quint16>(0x0005, port);
	data.appendTLV<quint16>(0x0017, port ^ 0x0FFFF);
	if (m_proxy)
		data.appendTLV<quint8>(0x0010, 1);
	if (fileinfo) {
		{
			// file info
			DataUnit tlv2711;
			tlv2711.append<quint16>(1); // Multiple files flag (1 - false, 2 - true)
			tlv2711.append<quint16>(1); // count
			tlv2711.append<quint32>(m_currentFile.size());
			tlv2711.append(m_currentFile.fileName(), defaultCodec());
			tlv2711.append<quint8>(0);
			data.appendTLV(0x2711, tlv2711);
		}
		{
			// file name encoding
			DataUnit tlv2722;
			tlv2722.append(defaultCodec()->name());
			data.appendTLV(0x2722, tlv2722);
		}
	}
	ServerMessage message(m_contact, data);
	m_contact->account()->connection()->send(message);
}

void OftConnection::connected()
{
	if (m_receiving) {
		Channel2BasicMessageData data(MsgAccept, ICQ_CAPABILITY_AIMSENDFILE, m_cookie);
		ServerMessage message(m_contact, data);
		m_contact->account()->connection()->send(message);
	} else {
		startFileTransfer(m_currentFile);
	}
}

void OftConnection::error(QAbstractSocket::SocketError error)
{
	bool criticalError = (error == QAbstractSocket::NetworkError) ||
						 (error == QAbstractSocket::RemoteHostClosedError);
	if (m_stage == 1 && m_receiving && !criticalError) {
		m_stage = 2;
		m_socket->deleteLater();
		sendFileRequest(false);
	} else if (m_stage == 2 && !m_receiving && !criticalError) {
		m_stage = 3;
		m_proxy = true;
		m_socket->close();
		initProxyConnection();
		sendFileRequest(false);
	} else {
		debug() << "File transfer connection error" << m_socket->errorString();
		close();
	}
}

void OftConnection::close()
{
	if (m_socket)
		m_socket->deleteLater();
	if (m_data)
		m_data.reset();
	if (m_proxy) {
		Channel2BasicMessageData data(MsgCancel, ICQ_CAPABILITY_AIMSENDFILE, m_cookie);
		ServerMessage message(m_contact, data);
		m_contact->account()->connection()->send(message);
	}
	m_transfer->removeConnection(m_cookie);
}

void OftConnection::newData()
{
	if (!m_data) {
		debug() << "File transfer data has been received when the output file is not initialized";
		return;
	}
	if (m_socket->bytesAvailable() <= 0)
		return;
	QByteArray buf = m_socket->read(m_header.size);
	m_header.receivedChecksum = OftConnection::chunkChecksum(buf.constData(), buf.size(),
											m_header.receivedChecksum,
											m_header.bytesReceived);
	m_header.bytesReceived += buf.size();
	m_data->write(buf);
	if (m_header.bytesReceived == m_header.size) {
		m_data.reset();
		m_header.type = OftDone;
		m_header.writeData(m_socket.data());
		m_socket->dataReaded();
		// close();
	}
}

void OftConnection::sendData()
{
	if (!m_data && m_socket->bytesToWrite())
		return;
	QByteArray buf = m_data->read(BUFFER_SIZE);
	m_header.receivedChecksum = OftConnection::chunkChecksum(buf.constData(), buf.size(),
											m_header.receivedChecksum,
											m_header.bytesReceived);
	m_header.bytesReceived += buf.size();
	m_socket->write(buf);
	if (m_header.bytesReceived == m_header.size) {
		disconnect(m_socket.data(), SIGNAL(bytesWritten(qint64)), this, SLOT(sendData()));
		m_data.reset();
	}
}

void OftConnection::startFileTransfer(const QFileInfo &file)
{
	m_data.reset(new QFile(file.absoluteFilePath()));
	m_header.type = OftPrompt;
	m_header.cookie = m_cookie;
	m_header.modTime = file.lastModified().toTime_t();
	m_header.size = file.size();
	m_header.fileName = file.fileName();
	m_header.checksum = fileChecksum(m_data.data(), m_header.size);
	m_header.receivedChecksum = 0xFFFF0000;
	m_header.bytesReceived = 0;
	m_header.writeData(m_socket.data());
}

void OftConnection::headerReaded()
{
	if (m_socket->lastHeader().isFinished()) {
		m_header = m_socket->lastHeader();
		QString error;
		if (m_receiving) {
			if (m_header.type & OftReceiver)
				error = QString("Oft message type %1 is not allowed during receiving");
		} else {
			if (m_header.type & OftSender)
				error = QString("Oft message type %1 is not allowed during sending");
		}
		if (!error.isEmpty()) {
			debug() << error.arg(m_header.type);
			close();
			return;
		}
		debug() << "Incoming oft message with type" << hex << m_header.type;
		switch (m_header.type) {
		case OftPrompt: { // Sender has sent us info about file transfer
			Q_ASSERT(!m_data);
			// TODO: don't hardcode the saving path
			QFile *file = new QFile(QString("/home/sokol/%1").arg(m_header.fileName));
			m_data.reset(file);
			bool exist = file->exists() && file->size() <= m_header.size;
			if (!m_data->open(exist ? QFile::ReadOnly : QFile::WriteOnly)) {
				close();
				return;
			}
			if (exist) {
				m_header.receivedChecksum = fileChecksum(file);
				m_header.bytesReceived = file->size();
				m_header.type = OftReceiverResume;
				m_socket->dataReaded();
				m_data->close();
			} else {
				m_header.type = OftAcknowledge;
			}
			m_header.cookie = m_cookie;
			m_header.writeData(m_socket.data());
			break;
		}
		case OftDone: { // Receiver has informed us about received file
			close(); // TODO: don't close connection if some files are still waiting to be sent
			break;
		}
		case OftReceiverResume: { // Receiver wants to resume old file transfer
			if (!m_data) {
				debug() << "Sender sent OftReceiverResume filetransfer request before OftPrompt";
				close();
				return;
			}
			quint32 checksum = fileChecksum(m_data.data(), m_header.bytesReceived);
			if (checksum != m_header.receivedChecksum) { // receiver's file is corrupt
				m_header.receivedChecksum = 0xffff0000;
				m_header.bytesReceived = 0;
			}
			m_header.type = OftSenderResume;
			m_header.writeData(m_socket.data());
			break;
		}
		case OftSenderResume: { // Sender responded at our resuming request
			if (!m_data) {
				debug() << "The sender had sent OftReceiverResume filetransfer request"
						<< "before the receiver sent OftPromt";
				close();
				return;
			}
			QIODevice::OpenMode flags;
			m_header.type = OftResumeAcknowledge;
			if (m_header.bytesReceived) { // ok. resume receiving
				flags = QIODevice::WriteOnly | QIODevice::Append;
				debug() << "Receiving of file" << m_header.fileName << "will be resumed";
			} else { // sender said that our local file is corrupt
				flags = QIODevice::WriteOnly;
				m_header.receivedChecksum = 0xffff0000;
				m_header.bytesReceived = 0;
				debug() << "File" << m_header.fileName << "will be rewritten";
			}
			if (m_data->open(flags)) {
				m_header.writeData(m_socket.data());
			} else {
				close();
			}
			break;
		}
		case OftResumeAcknowledge:
		case OftAcknowledge: {	// receiver are waiting file
			m_socket->dataReaded();
			if (m_data->open(QFile::ReadOnly)) {
				connect(m_socket.data(), SIGNAL(bytesWritten(qint64)), this, SLOT(sendData()));
				sendData();
			} else {
				debug() << "File" << m_currentFile.filePath() << "is not opened";
				close();
			}
			break;
		}
		default:
			debug() << "Unknown oft message type" << hex << m_header.type;
			m_socket->dataReaded();
		}
	}
}

quint32 OftConnection::chunkChecksum(const char *buffer, int len, quint32 oldChecksum, int offset)
{
	// code adapted from miranda's oft_calc_checksum
	quint16 checksum = (oldChecksum >> 16) & 0xffff;
	for (int i = 0; i < len; i++)
	{
		quint16 val = buffer[i];
		//quint32 oldchecksum = checksum;
		if (((i + offset) & 1) == 0)
			val = val << 8;
		if (checksum < val)
			checksum -= val + 1;
		else // simulate carry
			checksum -= val;
	}
	checksum = ((checksum & 0x0000ffff) + (checksum >> 16));
	checksum = ((checksum & 0x0000ffff) + (checksum >> 16));
	return (quint32)checksum << 16;
}

quint32 OftConnection::fileChecksum(QIODevice* file, int bytes)
{
	quint32 checksum = 0xFFFF0000;
	QByteArray data;
	data.reserve(BUFFER_SIZE);
	int totalRead = 0;
	if (bytes <= 0)
		bytes = file->size();
	bool isOpen = file->isOpen();
	if (!isOpen)
		file->open(QIODevice::ReadOnly);
	while (totalRead < bytes) {
		data = file->read(qMin(BUFFER_SIZE, bytes - totalRead));
		checksum = chunkChecksum(data.constData(), data.size(), checksum, totalRead);
		totalRead += data.size();
	}
	if (!isOpen)
		file->close();
	return checksum;
}

FileTransfer::FileTransfer(QObject *parent):
	QObject(parent)
{
	m_capabilities << ICQ_CAPABILITY_AIMSENDFILE;
}

void FileTransfer::processMessage(IcqContact *contact, const Capability &guid,
									 const QByteArray &data, quint16 reqType, quint64 cookie)
{
	Q_UNUSED(guid);
	TLVMap tlvs = DataUnit(data).read<TLVMap>();
	OftConnectionPtr conn = m_connections.value(cookie);
	if (!conn.isNull() && conn->contact() != contact) {
		debug() << "Cannot create two oscar file transfer with the same cookie" << cookie;
		return;
	}
	if (conn.isNull()) {
		conn = OftConnectionPtr(new OftConnection(contact, cookie, this));
		m_connections.insert(cookie, conn);
	}
	conn->handleRendezvous(reqType, tlvs);
}

void FileTransfer::removeConnection(quint64 cookie)
{
	// Memory should be released after all slots will be invoked.
	QMetaObject::invokeMethod(
			this,
			SLOT(removeConnectionImpl(quint64)),
			Qt::QueuedConnection,
			Q_ARG(quint64, cookie));
}

void FileTransfer::removeConnectionImpl(quint64 cookie)
{
	m_connections.remove(cookie);
}

} } // namespace qutim_sdk_0_3::oscar
