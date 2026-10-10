// Verbindung von S5Sim zum E/A-Dienst s5io auf einem Raspberry Pi (TCP, Protokoll siehe s5io/protocol.h).
// Baut die Verbindung auf, überträgt die Kanalkonfiguration, sendet die Ausgänge zyklisch und
// verbindet sich nach einem Abbruch selbst neu.
#pragma once

#include "protocol.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <vector>

class QTcpSocket;
class QTimer;

class PiLink : public QObject {
    Q_OBJECT
public:
    enum class State { Off, Connecting, Configuring, Online, Failed };

    explicit PiLink(QObject* parent = nullptr);
    ~PiLink() override;

    void setTarget(const QString& host, quint16 port);
    void setChannels(const std::vector<s5io::InputConfig>& in, const std::vector<s5io::OutputConfig>& out);
    void start();
    void stop();
    void reconnect();

    void setOutputs(uint32_t mask);
    uint32_t inputs() const { return inputs_; }

    State state() const { return state_; }
    QString statusText() const { return status_; }
    QString model() const { return model_; }
    bool simulated() const { return simulated_; }

signals:
    void stateChanged();
    void inputsChanged(uint32_t mask);

private:
    void connectNow();
    void onConnected();
    void onReadyRead();
    void onSocketLost(const QString& reason);
    void handleLine(const QString& text);
    void sendConfig();
    void sendLine(const QString& line);
    void setState(State state, const QString& text);
    void setInputs(uint32_t mask);
    void tick();

    QTcpSocket* socket_;
    QTimer* tickTimer_;
    QTimer* retryTimer_;
    QString host_;
    quint16 port_ = s5io::kDefaultPort;
    std::vector<s5io::InputConfig> in_;
    std::vector<s5io::OutputConfig> out_;
    bool running_ = false;
    bool helloOk_ = false;

    State state_ = State::Off;
    QString status_;
    QString model_;
    bool simulated_ = false;

    uint32_t inputs_ = 0;
    uint32_t outputs_ = 0;
    uint32_t sentOutputs_ = 0;
    QElapsedTimer sinceOut_;
    QElapsedTimer sinceIn_;
};
