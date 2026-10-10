#include "pilink.h"

#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr int kRetryMs = 2000;          // neuer Verbindungsversuch nach Abbruch
constexpr int kTickMs = 10;             // Ausgänge prüfen und senden
constexpr int kInputTimeoutMs = 1500;   // so lange ohne IN → Verbindung gilt als verloren

}  // namespace

PiLink::PiLink(QObject* parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , tickTimer_(new QTimer(this))
    , retryTimer_(new QTimer(this))
{
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket_, &QTcpSocket::connected, this, &PiLink::onConnected);
    connect(socket_, &QTcpSocket::readyRead, this, &PiLink::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, [this] { onSocketLost(tr("Verbindung getrennt")); });
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError err) {
        switch (err) {
        case QAbstractSocket::RemoteHostClosedError: onSocketLost(tr("s5io hat die Verbindung beendet")); break;
        case QAbstractSocket::ConnectionRefusedError:
            onSocketLost(tr("Verbindung abgelehnt – läuft s5io auf %1?").arg(host_));
            break;
        case QAbstractSocket::HostNotFoundError: onSocketLost(tr("Rechner %1 nicht gefunden").arg(host_)); break;
        case QAbstractSocket::SocketTimeoutError: onSocketLost(tr("Zeitüberschreitung")); break;
        case QAbstractSocket::NetworkError: onSocketLost(tr("Netzwerkfehler")); break;
        default: onSocketLost(socket_->errorString()); break;
        }
    });

    tickTimer_->setInterval(kTickMs);
    connect(tickTimer_, &QTimer::timeout, this, &PiLink::tick);
    retryTimer_->setSingleShot(true);
    connect(retryTimer_, &QTimer::timeout, this, &PiLink::connectNow);
    status_ = tr("aus");
}

PiLink::~PiLink()
{
    stop();
}

void PiLink::setTarget(const QString& host, quint16 port)
{
    if (host == host_ && port == port_) return;
    host_ = host;
    port_ = port;
    if (running_) reconnect();
}

void PiLink::setChannels(const std::vector<s5io::InputConfig>& in, const std::vector<s5io::OutputConfig>& out)
{
    in_ = in;
    out_ = out;
    if (helloOk_ && socket_->state() == QAbstractSocket::ConnectedState) sendConfig();
}

void PiLink::start()
{
    running_ = true;
    if (socket_->state() == QAbstractSocket::UnconnectedState) connectNow();
}

void PiLink::stop()
{
    running_ = false;
    retryTimer_->stop();
    tickTimer_->stop();
    socket_->blockSignals(true);
    if (socket_->state() == QAbstractSocket::ConnectedState) {
        // sauber abmelden: s5io schaltet daraufhin die Ausgänge ab
        sendLine(QStringLiteral("BYE"));
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) socket_->waitForDisconnected(300);
    }
    socket_->abort();
    socket_->blockSignals(false);
    helloOk_ = false;
    setInputs(0);
    setState(State::Off, tr("aus"));
}

void PiLink::reconnect()
{
    socket_->blockSignals(true);
    socket_->abort();
    socket_->blockSignals(false);
    helloOk_ = false;
    setInputs(0);
    if (running_) connectNow();
}

void PiLink::connectNow()
{
    if (!running_) return;
    if (host_.isEmpty()) {
        setState(State::Failed, tr("keine Adresse eingestellt"));
        return;
    }
    helloOk_ = false;
    setState(State::Connecting, tr("verbinde mit %1:%2 …").arg(host_).arg(port_));
    socket_->connectToHost(host_, port_);
}

void PiLink::onConnected()
{
    setState(State::Configuring, tr("verbunden, warte auf s5io …"));
    sendLine(QString::fromStdString(s5io::helloClient()));
    sinceIn_.start();
    tickTimer_->start();
}

void PiLink::onReadyRead()
{
    while (socket_->canReadLine()) handleLine(QString::fromUtf8(socket_->readLine()).trimmed());
}

void PiLink::handleLine(const QString& text)
{
    const auto line = s5io::parseLine(text.toStdString());
    if (!line) return;
    const std::string& cmd = line->command;

    if (cmd == "HELLO") {
        if (line->args.size() < 2 || line->args[0] != "S5IO" || line->args[1] != std::to_string(s5io::kProtocolVersion)) {
            setState(State::Failed, tr("unpassende Gegenstelle oder Protokollversion"));
            socket_->abort();
            return;
        }
        model_ = QString::fromStdString(line->value("MODEL"));
        simulated_ = line->value("MODE") == "SIM";
        helloOk_ = true;
    } else if (cmd == "PINS") {
        if (helloOk_) sendConfig();
    } else if (cmd == "OK") {
        sentOutputs_ = ~outputs_;   // Ausgänge sofort senden
        sinceIn_.start();
        setState(State::Online, simulated_ ? tr("verbunden (Simulation)") : tr("verbunden"));
    } else if (cmd == "ERR") {
        QString msg = text.mid(3).trimmed();
        if (msg.contains(QStringLiteral("bereits")))
            onSocketLost(tr("s5io ist schon mit einem anderen S5Sim verbunden"));
        else
            setState(State::Failed, tr("s5io meldet: %1").arg(msg));
    } else if (cmd == "IN") {
        uint32_t mask = 0;
        if (!line->args.empty() && s5io::parseMask(line->args[0], mask)) {
            sinceIn_.start();
            if (state_ == State::Online) setInputs(mask);
        }
    }
}

void PiLink::sendConfig()
{
    setState(State::Configuring, tr("übertrage Konfiguration …"));
    setInputs(0);
    sendLine(QStringLiteral("RESET"));
    for (const auto& c : in_) sendLine(QString::fromStdString(s5io::configInputLine(c)));
    for (const auto& c : out_) sendLine(QString::fromStdString(s5io::configOutputLine(c)));
    sendLine(QStringLiteral("COMMIT"));
}

void PiLink::sendLine(const QString& line)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    socket_->write((line + QLatin1Char('\n')).toUtf8());
}

void PiLink::setOutputs(uint32_t mask)
{
    outputs_ = mask;
}

void PiLink::tick()
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    if (state_ == State::Configuring && sinceIn_.isValid() && sinceIn_.elapsed() > 3000) {
        onSocketLost(tr("keine Antwort von s5io"));
        return;
    }
    if (state_ != State::Online) return;
    if (sinceIn_.isValid() && sinceIn_.elapsed() > kInputTimeoutMs) {
        onSocketLost(tr("keine Antwort von s5io"));
        return;
    }
    if (outputs_ != sentOutputs_ || !sinceOut_.isValid() || sinceOut_.elapsed() >= s5io::kKeepAliveMs) {
        sendLine(QString::fromStdString(s5io::maskLine("OUT", outputs_)));
        sentOutputs_ = outputs_;
        sinceOut_.start();
    }
}

void PiLink::onSocketLost(const QString& reason)
{
    if (state_ == State::Off && !running_) return;
    tickTimer_->stop();
    helloOk_ = false;
    setInputs(0);
    socket_->blockSignals(true);
    socket_->abort();
    socket_->blockSignals(false);
    if (running_) {
        setState(State::Failed, tr("%1 – neuer Versuch in %2 s").arg(reason).arg(kRetryMs / 1000));
        retryTimer_->start(kRetryMs);
    } else {
        setState(State::Off, reason);
    }
}

void PiLink::setState(State state, const QString& text)
{
    if (state == state_ && text == status_) return;
    state_ = state;
    status_ = text;
    emit stateChanged();
}

void PiLink::setInputs(uint32_t mask)
{
    if (mask == inputs_) return;
    inputs_ = mask;
    emit inputsChanged(mask);
}
