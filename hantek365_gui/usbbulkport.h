#pragma once

#include <QIODevice>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>

class QSocketNotifier;
struct usbdevfs_urb;

// Sequential QIODevice over a Linux usbdevfs node (/dev/bus/usb/BBB/DDD)
// that talks to one bulk IN and one bulk OUT endpoint of interface 0.
class UsbBulkPort : public QIODevice
{
    Q_OBJECT

public:
    explicit UsbBulkPort(QObject *parent = nullptr);
    ~UsbBulkPort() override;

    // usbdevfs nodes of all attached devices with the given VID/PID (hex, e.g. "0483")
    static QStringList findDevices(const QString &vid, const QString &pid);

    static bool isUsbNode(const QString &path) { return path.startsWith("/dev/bus/usb/"); }

    bool openNode(const QString &node, quint8 epIn, quint8 epOut);
    void close() override;
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_rx.size() + QIODevice::bytesAvailable(); }

signals:
    void transportError(const QString &msg);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 size) override;

private:
    static constexpr int MAX_PACKET_SIZE  = 64;
    static constexpr int WRITE_TIMEOUT_MS = 1000;

    bool submitRead();
    void onUrbCompleted();
    void fail(const QString &what);

    int              m_fd       = -1;
    quint8           m_epIn     = 0;
    quint8           m_epOut    = 0;
    std::unique_ptr<usbdevfs_urb> m_urb;
    char             m_urbBuf[MAX_PACKET_SIZE];
    QByteArray       m_rx;
    QSocketNotifier *m_notifier = nullptr;
};
