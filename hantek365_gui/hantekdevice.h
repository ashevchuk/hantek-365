#pragma once

#include <QObject>
#include <QSerialPort>
#include <QStringList>
#include <QTimer>
#include <QDeadlineTimer>
#include <QString>
#include <cstdint>

#include "usbbulkport.h"

// Result of parsing one measurement packet
struct Measurement {
    bool    valid    = false;
    bool    overload = false; // display shows OL; siValue is meaningless
    bool    relative = false; // REL flag reported by the meter
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

    // After a mode or REL change the meter first reports its previous display
    // with the new range, then a half-settled value; readings are clean about
    // 1 s after the acknowledgement.
    static constexpr int     SETTLE_MS  = 1000;

    // Radio dongle: CDC-ACM serial port
    static constexpr quint16     DONGLE_VID = 0x0451;
    static constexpr quint16     DONGLE_PID = 0x16AA;

    // Direct USB cable: vendor-specific bulk interface
    static constexpr const char *USB_VID    = "0483";
    static constexpr const char *USB_PID    = "5722";
    static constexpr uint8_t     USB_EP_IN  = 0x81;
    static constexpr uint8_t     USB_EP_OUT = 0x02;

    static QStringList usbDeviceNodes();

    explicit HantekDevice(QObject *parent = nullptr);
    ~HantekDevice() override;

    bool    isConnected()      const { return m_port && m_port->isOpen(); }
    QString currentModeName()  const { return m_modeName; }

public slots:
    void connectDevice(const QString &portName);
    void disconnectDevice();
    void setMode(uint8_t modeCode, const QString &modeName);
    void setRelative();   // the meter toggles REL on every request
    void setPaused(bool paused);

signals:
    void measurementReceived(const Measurement &m);
    void connectionChanged(bool connected);
    void modeChanged(const QString &modeName);
    void modeChangeFailed();
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

    QSerialPort *m_serial    = nullptr;
    UsbBulkPort *m_usb       = nullptr;
    QIODevice   *m_port      = nullptr;   // the active one of m_serial / m_usb
    QTimer      *m_pollTimer = nullptr;   // 200 ms, repeating
    QTimer      *m_modeTimer = nullptr;   // timeout for mode change acknowledgement

    QByteArray m_readBuf;
    State      m_state = State::Disconnected;

    uint8_t m_targetMode = 0;
    QString m_modeName;
    int     m_modeStep   = 0;
    bool    m_paused     = false;
    QDeadlineTimer m_settle {0};   // readings are dropped until it expires
};
