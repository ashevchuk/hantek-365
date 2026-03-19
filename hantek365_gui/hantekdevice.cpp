#include "hantekdevice.h"

#include <QSerialPortInfo>

HantekDevice::HantekDevice(QObject *parent)
    : QObject(parent)
    , m_port(new QSerialPort(this))
    , m_pollTimer(new QTimer(this))
    , m_modeTimer(new QTimer(this))
{
    m_pollTimer->setInterval(200);
    m_pollTimer->setSingleShot(false);

    m_modeTimer->setInterval(500);   // timeout waiting for 0xDD per step
    m_modeTimer->setSingleShot(true);

    connect(m_port,      &QSerialPort::readyRead,    this, &HantekDevice::onReadyRead);
    connect(m_pollTimer, &QTimer::timeout,           this, &HantekDevice::onPollTimer);
    connect(m_modeTimer, &QTimer::timeout,           this, [this]() {
        // Mode change acknowledgement timed out
        m_state = State::Polling;
        m_pollTimer->start();
        emit statusMessage(tr("Mode change timeout: %1").arg(m_modeName));
    });

    connect(m_port, &QSerialPort::errorOccurred,
            this, [this](QSerialPort::SerialPortError err) {
        if (err != QSerialPort::NoError && isConnected()) {
            emit statusMessage(tr("Port error: %1").arg(m_port->errorString()));
            disconnectDevice();
        }
    });
}

HantekDevice::~HantekDevice()
{
    disconnectDevice();
}

// ── Connection ─────────────────────────────────────────────────────────────

void HantekDevice::connectDevice(const QString &portName)
{
    if (m_port->isOpen())
        m_port->close();

    m_port->setPortName(portName);
    m_port->setBaudRate(QSerialPort::Baud9600);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        emit statusMessage(tr("Cannot open %1: %2")
                               .arg(portName, m_port->errorString()));
        return;
    }

    m_readBuf.clear();
    m_state = State::Polling;
    m_pollTimer->start();

    emit connectionChanged(true);
    emit statusMessage(tr("Connected: %1").arg(portName));
}

void HantekDevice::disconnectDevice()
{
    m_pollTimer->stop();
    m_modeTimer->stop();
    if (m_port->isOpen())
        m_port->close();

    m_state = State::Disconnected;
    emit connectionChanged(false);
    emit statusMessage(tr("Disconnected"));
}

// ── Mode selection ────────────────────────────────────────────────────────

void HantekDevice::setMode(uint8_t modeCode, const QString &modeName)
{
    if (m_state != State::Polling && m_state != State::WaitingPollResp)
        return;

    m_pollTimer->stop();
    m_readBuf.clear();
    m_modeStep   = 0;
    m_targetMode = modeCode;
    m_modeName   = modeName;
    m_state      = State::ModeChanging;

    emit statusMessage(tr("Setting mode %1…").arg(modeName));
    sendModeStep();
}

void HantekDevice::setPaused(bool paused)
{
    if (!isConnected()) return;
    if (paused) {
        m_pollTimer->stop();
        m_readBuf.clear();
        m_state = State::Polling; // reset so resume works cleanly
    } else {
        m_state = State::Polling;
        m_pollTimer->start();
    }
}

void HantekDevice::setRelative()
{
    if (m_state != State::Polling)
        return;
    QByteArray cmd;
    cmd.append(char(CMD_MODE));
    cmd.append(char(0xF3));
    m_port->write(cmd);
    emit statusMessage(tr("REL — request sent"));
}

void HantekDevice::sendModeStep()
{
    const bool    isSpecial = (m_targetMode & 0xF0) == 0xF0;
    const uint8_t current   = isSpecial ? m_targetMode
                                        : ((m_targetMode & 0xF0) | (m_modeStep & 0x0F));
    QByteArray cmd;
    cmd.append(char(CMD_MODE));
    cmd.append(char(current));
    m_port->write(cmd);
    m_modeTimer->start();
}

// ── Measurement polling ─────────────────────────────────────────────────────────

void HantekDevice::sendPollRequest()
{
    const QByteArray cmd = QByteArray("\x01\x0F", 2);
    m_port->write(cmd);
    m_state = State::WaitingPollResp;
}

void HantekDevice::onPollTimer()
{
    // Previous response not received — reset and retry
    if (m_state == State::WaitingPollResp) {
        m_readBuf.clear();
        m_state = State::Polling;
    }
    if (m_state == State::Polling)
        sendPollRequest();
}

// ── Data reception ─────────────────────────────────────────────────────────────

void HantekDevice::onReadyRead()
{
    m_readBuf.append(m_port->readAll());

    // ── Handle mode change response ────────────────────────────────────
    if (m_state == State::ModeChanging) {
        // Look for acknowledgement byte 0xDD
        for (int i = 0; i < m_readBuf.size(); ++i) {
            if (static_cast<uint8_t>(m_readBuf[i]) != ACK_MODE)
                continue;

            m_modeTimer->stop();
            m_readBuf.remove(0, i + 1);

            const bool    isSpecial = (m_targetMode & 0xF0) == 0xF0;
            const uint8_t current   = isSpecial ? m_targetMode
                                                : ((m_targetMode & 0xF0) | (m_modeStep & 0x0F));
            if (current == m_targetMode || isSpecial) {
                // Target mode set successfully
                m_state = State::Polling;
                m_pollTimer->start();
                emit modeChanged(m_modeName);
                emit statusMessage(tr("Mode: %1").arg(m_modeName));
            } else {
                // Move to next sub-range (50 ms delay, as in the original C code)
                ++m_modeStep;
                QTimer::singleShot(50, this, &HantekDevice::sendModeStep);
            }
            return;
        }
        return; // Wait for more bytes
    }

    // ── Handle poll response ────────────────────────────────
    if (m_state != State::WaitingPollResp && m_state != State::Polling)
        return;

    while (!m_readBuf.isEmpty()) {
        const uint8_t first = static_cast<uint8_t>(m_readBuf[0]);

        if (first == PKT_NODATA) {
            m_readBuf.remove(0, 1);
            m_state = State::Polling;
            break;
        }

        if (first == PKT_OK) {
            if (m_readBuf.size() < PKT_LEN)
                break; // Wait for rest of packet

            const QByteArray pkt = m_readBuf.left(PKT_LEN);
            m_readBuf.remove(0, PKT_LEN);

            const Measurement meas = parsePacket(pkt);
            if (meas.valid)
                emit measurementReceived(meas);

            m_state = State::Polling;
            break;
        }

        // Unknown byte — discard
        m_readBuf.remove(0, 1);
    }
}

// ── Packet parsing ────────────────────────────────────────────────────────────
//
// 15-byte packet format:
//   [0]  = 0xA0  — start marker
//   [1]  = sign  (bit2=«−», bit1=«+»)
//   [2..5] = 4 ASCII digits
//   [6]  = reserved
//   [7]  = decimal point position (ASCII mask: 0x31..0x38, value = 2^i)
//   [8]  = AC/DC/AUTO/REL flags
//   [9]  = nano flag (bit1)
//   [10] = multiplier: 0x80=µ, 0x40=m, 0x20=k, 0x10=M, 0x08=beep (continuity)
//   [11] = unit: 0x01=°F, 0x02=°C, 0x04=F(capacitance), 0x20=Ω, 0x40=A, 0x80=V
//
Measurement HantekDevice::parsePacket(const QByteArray &pkt) const
{
    Measurement m;
    if (pkt.size() != PKT_LEN || static_cast<uint8_t>(pkt[0]) != PKT_OK)
        return m;

    const uint8_t sign  = static_cast<uint8_t>(pkt[1]);
    const uint8_t dpos  = static_cast<uint8_t>(pkt[7]);
    const uint8_t acdc  = static_cast<uint8_t>(pkt[8]);
    const uint8_t nano  = static_cast<uint8_t>(pkt[9]);
    const uint8_t mult  = static_cast<uint8_t>(pkt[10]);
    const uint8_t units = static_cast<uint8_t>(pkt[11]);

    // Build value string with decimal point.
    // dpos: numeric value = 2^i, where i is the position after the i-th digit.
    const int dval = dpos - 0x30;
    QString valStr;
    for (int i = 0; i < 4; ++i) {
        valStr += QChar(pkt[2 + i]);
        if (dval && ((dval >> i) == 1))
            valStr += QLatin1Char('.');
    }

    bool ok = false;
    double numVal = valStr.toDouble(&ok);
    if (!ok) return m; // "OL" and similar — skip in graph

    if (sign & 0x04) numVal = -numVal;

    // Multiplier prefix
    double  pfxMult = 1.0;
    QString pfxStr;
    bool    beep    = false;

    if      (mult == 0x80) { pfxStr = u8"\u03bc"; pfxMult = 1e-6; } // µ
    else if (mult == 0x40) { pfxStr = "m";         pfxMult = 1e-3; }
    else if (mult == 0x20) { pfxStr = "k";         pfxMult = 1e3;  }
    else if (mult == 0x10) { pfxStr = "M";         pfxMult = 1e6;  }
    else if (mult == 0x08) { pfxStr = "";           pfxMult = 1.0; beep = true; }
    else if (nano & 0x02)  { pfxStr = "n";         pfxMult = 1e-9; }

    // Unit of measurement
    QString unitDisp, baseUnit;
    if      (units == 0x01) { unitDisp = u8"\u00b0F"; baseUnit = u8"\u00b0F"; }
    else if (units == 0x02) { unitDisp = u8"\u00b0C"; baseUnit = u8"\u00b0C"; }
    else if (units == 0x04) { unitDisp = "F";          baseUnit = "F";         }
    else if (units == 0x20) {
        unitDisp = u8"\u03a9";  // Ω
        baseUnit = u8"\u03a9";
        if (beep) unitDisp += u8" \u266b"; // ♫ continuity beep
    }
    else if (units == 0x40) { unitDisp = "A";  baseUnit = "A"; }
    else if (units == 0x80) { unitDisp = "V";  baseUnit = "V"; }
    else { unitDisp = QString("?%1").arg(units, 2, 16); baseUnit = "?"; }

    // Mode flags
    QStringList flags;
    if (acdc & 0x08) flags << "AC";
    if (acdc & 0x10) flags << "DC";
    if (acdc & 0x20) flags << "AUTO"; else flags << "MANU";
    if (acdc & 0x04) flags << "REL";

    const QString signStr = (sign & 0x04) ? u8"\u2212"  // −
                          : (sign & 0x02) ? "+"
                          :                 " ";

    m.siValue  = numVal * pfxMult;
    m.baseUnit = baseUnit;
    m.display  = signStr + valStr + " " + pfxStr + unitDisp
                 + "   " + flags.join(" ");
    m.valid    = true;
    return m;
}
