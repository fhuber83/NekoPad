#include "ui/single_instance.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace neko::pad::ui {

namespace {

constexpr quint32 kMagic = 0x4E4B5044; // "NKPD"

} // namespace

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent) {
    // Per user: the home directory names them, and keeps the name short and portable.
    const QByteArray id = QCryptographicHash::hash((key + QDir::homePath()).toUtf8(), QCryptographicHash::Sha256);
    serverName_ = QStringLiteral("nekopad-") + QString::fromLatin1(id.toHex().left(16));
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::sendToRunning(const QStringList& files, int timeoutMs) const {
    QLocalSocket socket;
    socket.connectToServer(serverName_);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    QByteArray payload;
    {
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_5);
        out << kMagic << files;
    }
    QDataStream frame(&socket);
    frame.setVersion(QDataStream::Qt_6_5);
    frame << payload;
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return false;
    }
    // The running instance answers once it has the files, so they are not lost if it
    // was just quitting.
    if (!socket.waitForReadyRead(timeoutMs)) {
        return false;
    }
    return socket.readAll().startsWith("ok");
}

bool SingleInstance::listen() {
    if (server_) {
        return true;
    }
    // Someone answering on the name is a running instance. Ask first: with socket options
    // set, QLocalServer on Unix renames its new socket over an existing one instead of
    // failing, which would steal the name from a live instance.
    {
        QLocalSocket probe;
        probe.connectToServer(serverName_);
        if (probe.waitForConnected(200)) {
            return false;
        }
    }
    // Nobody answers: whatever is left of the name is stale (a crashed instance's socket).
    QLocalServer::removeServer(serverName_);
    auto* server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(serverName_)) {
        delete server;
        return false;
    }
    server_ = server;
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                QDataStream frame(socket);
                frame.setVersion(QDataStream::Qt_6_5);
                frame.startTransaction();
                QByteArray payload;
                frame >> payload;
                if (!frame.commitTransaction()) {
                    return; // wait for the rest
                }
                QDataStream in(payload);
                in.setVersion(QDataStream::Qt_6_5);
                quint32 magic = 0;
                QStringList files;
                in >> magic >> files;
                if (magic != kMagic || in.status() != QDataStream::Ok) {
                    socket->disconnectFromServer();
                    return;
                }
                socket->write("ok");
                socket->flush();
                emit filesReceived(files);
            });
        }
    });
    return true;
}

} // namespace neko::pad::ui
