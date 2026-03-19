#pragma once

#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QString>
#include <cstdint>

// Result of parsing one measurement packet
struct Measurement {
    bool    valid    = false;
    double  siValue  = 0.0;   // value in SI base units (V, A, Ohm, °C...)
    QString display;          // display string: "+1.234 mV  DC AUTO"
    QString baseUnit;         // base unit without prefix: "V", "A", "Ω", "°C"...
};

class HantekDevice : public QObject
{
    Q_OBJECT

public:
    // ── Protocol constants ─────────────────────────────────────────────────
    static constexpr uint8_t CMD_MODE   = 0x03;
    static constexpr uint8_t ACK_MODE   = 0xDD;
    static constexpr uint8_t PKT_OK     = 0xA0;
    static constexpr uint8_t PKT_NODATA = 0xAA;
    static constexpr int     PKT_LEN    = 15;

    explicit HantekDevice(QObject *parent = nullptr);
    ~HantekDevice() override;

    bool    isConnected()      const { return m_port && m_port->isOpen(); }
    QString currentModeName()  const { return m_modeName; }

public slots:
    void connectDevice(const QString &portName);
    void disconnectDevice();
    void setMode(uint8_t modeCode, const QString &modeName);
    void setRelative();
    void setPaused(bool paused);

signals:
    void measurementReceived(const Measurement &m);
    void connectionChanged(bool connected);
    void modeChanged(const QString &modeName);
    void statusMessage(const QString &msg);

private slots:
    void onReadyRead();
    void onPollTimer();

private:
    enum class State {
        Disconnected,
        Polling,            // sent CMD_GET, waiting for response
        WaitingPollResp,    // (alias, guards against double-send)
        ModeChanging,       // cycling through sub-ranges to set mode
    };

    void sendPollRequest();
    void sendModeStep();
    Measurement parsePacket(const QByteArray &pkt) const;

    QSerialPort *m_port      = nullptr;
    QTimer      *m_pollTimer = nullptr;   // 200 ms, repeating
    QTimer      *m_modeTimer = nullptr;   // timeout for mode change acknowledgement

    QByteArray m_readBuf;
    State      m_state = State::Disconnected;

    uint8_t m_targetMode = 0;
    QString m_modeName;
    int     m_modeStep   = 0;
};
