#include "usbbulkport.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>

static QString readSysfsAttr(const QString &dir, const char *name)
{
    QFile f(dir + '/' + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(f.readAll()).trimmed();
}

UsbBulkPort::UsbBulkPort(QObject *parent)
    : QIODevice(parent)
    , m_urb(std::make_unique<usbdevfs_urb>())
{
}

UsbBulkPort::~UsbBulkPort()
{
    close();
}

QStringList UsbBulkPort::findDevices(const QString &vid, const QString &pid)
{
    QStringList nodes;
    const QDir sysfs("/sys/bus/usb/devices");
    for (const QString &entry : sysfs.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = sysfs.filePath(entry);
        if (readSysfsAttr(dir, "idVendor") != vid || readSysfsAttr(dir, "idProduct") != pid)
            continue;
        nodes << QString("/dev/bus/usb/%1/%2")
                     .arg(readSysfsAttr(dir, "busnum").toInt(), 3, 10, QChar('0'))
                     .arg(readSysfsAttr(dir, "devnum").toInt(), 3, 10, QChar('0'));
    }
    return nodes;
}

bool UsbBulkPort::openNode(const QString &node, quint8 epIn, quint8 epOut)
{
    close();

    m_fd = ::open(QFile::encodeName(node).constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        setErrorString(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    unsigned int iface = 0;
    if (::ioctl(m_fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
        setErrorString(tr("claim interface: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_epIn  = epIn;
    m_epOut = epOut;
    QIODevice::open(QIODevice::ReadWrite | QIODevice::Unbuffered);

    // usbdevfs reports completed URBs as POLLOUT on the device fd
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &UsbBulkPort::onUrbCompleted);

    if (!submitRead()) {
        const QString err = errorString();
        close();
        setErrorString(err);
        return false;
    }
    return true;
}

void UsbBulkPort::close()
{
    if (m_fd < 0)
        return;

    delete m_notifier;
    m_notifier = nullptr;

    // Closing the fd cancels the pending URB; the kernel copies data into
    // m_urbBuf only on reap, so nothing writes into it afterwards.
    ::close(m_fd);
    m_fd = -1;
    m_rx.clear();
    QIODevice::close();
}

bool UsbBulkPort::submitRead()
{
    *m_urb = {};
    m_urb->type          = USBDEVFS_URB_TYPE_BULK;
    m_urb->endpoint      = m_epIn;
    m_urb->buffer        = m_urbBuf;
    m_urb->buffer_length = MAX_PACKET_SIZE;

    if (::ioctl(m_fd, USBDEVFS_SUBMITURB, m_urb.get()) < 0) {
        setErrorString(tr("submit read: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

void UsbBulkPort::onUrbCompleted()
{
    usbdevfs_urb *done = nullptr;
    if (::ioctl(m_fd, USBDEVFS_REAPURBNDELAY, &done) < 0) {
        if (errno != EAGAIN)
            fail(tr("reap: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    if (done->status != 0) {
        fail(tr("bulk read: %1").arg(QString::fromLocal8Bit(std::strerror(-done->status))));
        return;
    }

    if (done->actual_length > 0) {
        m_rx.append(m_urbBuf, done->actual_length);
        emit readyRead();
    }

    if (!submitRead())
        fail(errorString());
}

qint64 UsbBulkPort::readData(char *data, qint64 maxSize)
{
    const qint64 n = std::min<qint64>(maxSize, m_rx.size());
    std::memcpy(data, m_rx.constData(), n);
    m_rx.remove(0, n);
    return n;
}

qint64 UsbBulkPort::writeData(const char *data, qint64 size)
{
    usbdevfs_bulktransfer bulk{};
    bulk.ep      = m_epOut;
    bulk.len     = static_cast<unsigned int>(size);
    bulk.timeout = WRITE_TIMEOUT_MS;
    bulk.data    = const_cast<char *>(data);

    const int n = ::ioctl(m_fd, USBDEVFS_BULK, &bulk);
    if (n < 0) {
        fail(tr("bulk write: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return -1;
    }
    return n;
}

void UsbBulkPort::fail(const QString &what)
{
    if (m_notifier)
        m_notifier->setEnabled(false);
    setErrorString(what);
    emit transportError(what);
}
