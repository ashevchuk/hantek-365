#include "mainwindow.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDateTime>
#include <QResizeEvent>
#include <QSerialPortInfo>
#include <QStatusBar>
#include <QGraphicsLayout>
#include <algorithm>
#include <limits>
#include <cmath>

// ── Mode table ──────────────────────────────────────────────────────────
static const QVector<ModeGroup> MODE_GROUPS = {
    { "DC V",   { {"VDC",0xa0},{"60mVDC",0xa1},{"600mVDC",0xa2},
                  {"6VDC",0xa3},{"60VDC",0xa4},{"600VDC",0xa5},{"800VDC",0xa6} } },
    { "AC V",   { {"VAC",0xb0},{"60mVAC",0xb1},{"600mVAC",0xb2},
                  {"6VAC",0xb3},{"60VAC",0xb4},{"600VAC",0xb5} } },
    { "DC A",   { {"mADC",0xc0},{"60mADC",0xc1},{"600mADC",0xc2},{"ADC",0xc3} } },
    { "AC A",   { {"mAAC",0xd0},{"60mAAC",0xd1},{"600mAAC",0xd2},{"AAC",0xd3} } },
    { u8"\u03a9", { {"ohm",0xe0},{"600ohm",0xe1},{"6kohm",0xe2},  // Ω
                    {"60kohm",0xe3},{"600kohm",0xe4},{"6Mohm",0xe5},{"60Mohm",0xe6} } },
    { "Misc",   { {"diode",0xf0},{"cap",0xf1},{"cont",0xf2},
                  {"temp°C",0xf5},{"temp°F",0xf6} } },
};

// ── Constructor / destructor ─────────────────────────────────────────────────

MainWindow::~MainWindow()
{
    // Disconnect all device signals before Qt starts deleting child objects.
    // Without this, the HantekDevice destructor emits connectionChanged,
    // and onConnectionChanged accesses the already-deleted m_statsTimer → crash.
    disconnect(m_device, nullptr, this, nullptr);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_device(new HantekDevice(this))
    , m_statsTimer(new QTimer(this))
{
    setWindowTitle(tr("Hantek 365 Monitor"));
    setMinimumSize(200, 80);

    setupUI();
    setupChart();
    setupModeButtons();

    // Populate port list
    refreshPortList();

    // Stats timer: update every 2 seconds
    m_statsTimer->setInterval(2000);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);

    // Connect device signals
    connect(m_device, &HantekDevice::measurementReceived,
            this, &MainWindow::onMeasurement);
    connect(m_device, &HantekDevice::connectionChanged,
            this, &MainWindow::onConnectionChanged);
    connect(m_device, &HantekDevice::modeChanged,
            this, &MainWindow::onModeChanged);
    connect(m_device, &HantekDevice::statusMessage,
            this, &MainWindow::onStatusMessage);

    // Initial UI state
    onConnectionChanged(false);
}

// ── UI setup ──────────────────────────────────────────────────────────────

void MainWindow::setupUI()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(4);
    mainLayout->setContentsMargins(8, 6, 8, 6);

    // ── Toolbar ──────────────────────────────────────────────────
    m_toolbar = new QFrame(central);
    auto *toolbar = m_toolbar;
    toolbar->setFrameShape(QFrame::StyledPanel);
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setSpacing(6);
    tbLayout->setContentsMargins(6, 4, 6, 4);

    tbLayout->addWidget(new QLabel(tr("Port:"), toolbar));

    m_portCombo = new QComboBox(toolbar);
    m_portCombo->setMinimumWidth(140);
    tbLayout->addWidget(m_portCombo);

    auto *btnRefresh = new QPushButton(tr("⟳"), toolbar);
    btnRefresh->setFixedWidth(28);
    btnRefresh->setToolTip(tr("Refresh port list"));
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshPortList);
    tbLayout->addWidget(btnRefresh);

    tbLayout->addSpacing(8);

    m_btnConnect = new QPushButton(tr("Connect"), toolbar);
    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    tbLayout->addWidget(m_btnConnect);

    tbLayout->addSpacing(8);

    auto *btnClear = new QPushButton(tr("Clear"), toolbar);
    btnClear->setToolTip(tr("Clear chart and statistics"));
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::onClearClicked);
    tbLayout->addWidget(btnClear);

    tbLayout->addSpacing(12);

    m_cbRelative = new QPushButton(tr("REL"), toolbar);
    m_cbRelative->setCheckable(true);
    m_cbRelative->setToolTip(tr("Relative measurement mode"));
    connect(m_cbRelative, &QPushButton::toggled, this, &MainWindow::onRelativeToggled);
    tbLayout->addWidget(m_cbRelative);

    tbLayout->addSpacing(8);

    m_btnPause = new QPushButton(tr("⏸ Pause"), toolbar);
    m_btnPause->setCheckable(true);
    m_btnPause->setToolTip(tr("Pause/resume measurements"));
    connect(m_btnPause, &QPushButton::toggled, this, &MainWindow::onPauseToggled);
    tbLayout->addWidget(m_btnPause);

    tbLayout->addStretch();

    m_btnToggleModes = new QPushButton(tr("Modes ▲"), toolbar);
    m_btnToggleModes->setToolTip(tr("Hide/show mode panel"));
    connect(m_btnToggleModes, &QPushButton::clicked, this, &MainWindow::onToggleModes);
    tbLayout->addWidget(m_btnToggleModes);
    mainLayout->addWidget(toolbar);

    // ── Current value display ────────────────────────────────────────
    m_valueLabel = new QLabel(tr("No data"), central);
    QFont vf = m_valueLabel->font();
    vf.setFamily("Monospace");
    vf.setPointSize(28);
    vf.setBold(true);
    m_valueLabel->setFont(vf);
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setStyleSheet("color: #00e676; background: #1a1a1a;"
                                "border-radius: 4px; padding: 8px;");
    m_valueLabel->setFixedHeight(70);
    mainLayout->addWidget(m_valueLabel);

    // ── Chart area (placeholder, filled in setupChart) ─────────────
    // QChartView widget will be added in setupChart()
    // Save layout pointer to add chart later
    central->setProperty("mainLayout", QVariant::fromValue(mainLayout));

    // ── Stats row ─────────────────────────────────────────────────────
    m_statsLabel = new QLabel(tr("—"), central);
    m_statsLabel->setAlignment(Qt::AlignCenter);
    m_statsLabel->setStyleSheet("color: #888; font-size: 11px;");
    // Will be added after chart in setupChart()
    central->setProperty("statsLabel", QVariant::fromValue(m_statsLabel));
}

void MainWindow::setupChart()
{
    // Data series
    m_series = new QLineSeries();
    m_series->setColor(QColor(0, 170, 255));
    m_series->setPen(QPen(QColor(0, 170, 255), 1.5));
    m_series->setName(tr("Value"));

    // Chart object
    m_chart = new QChart();
    m_chart->addSeries(m_series);
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->legend()->hide();
    m_chart->setBackgroundBrush(QColor(0x1a, 0x1a, 0x2e));
    m_chart->setPlotAreaBackgroundBrush(QColor(0x12, 0x12, 0x22));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setContentsMargins(0, 0, 0, 0);
    m_chart->layout()->setContentsMargins(0, 0, 0, 0);  // requires QGraphicsLayout

    // X axis — date/time
    m_axisX = new QDateTimeAxis(m_chart);
    m_axisX->setFormat("hh:mm:ss");
    m_axisX->setTitleText(tr("Time"));
    m_axisX->setLabelsColor(QColor(0xaa, 0xaa, 0xaa));
    m_axisX->setTitleBrush(QColor(0xaa, 0xaa, 0xaa));
    m_axisX->setGridLineColor(QColor(0x33, 0x33, 0x55));
    m_axisX->setMinorGridLineColor(QColor(0x25, 0x25, 0x40));
    m_axisX->setTickCount(6);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_series->attachAxis(m_axisX);

    // Y axis — values
    m_axisY = new QValueAxis(m_chart);
    m_axisY->setLabelFormat("%.4g");
    m_axisY->setTitleText(tr("Value"));
    m_axisY->setLabelsColor(QColor(0xaa, 0xaa, 0xaa));
    m_axisY->setTitleBrush(QColor(0xaa, 0xaa, 0xaa));
    m_axisY->setGridLineColor(QColor(0x33, 0x33, 0x55));
    m_axisY->setMinorGridLineColor(QColor(0x25, 0x25, 0x40));
    m_axisY->setTickCount(6);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_series->attachAxis(m_axisY);

    // Initial axis ranges
    const auto now = QDateTime::currentDateTime();
    m_axisX->setRange(now.addSecs(-30), now);
    m_axisY->setRange(-1, 1);

    // Display widget
    m_chartView = new QChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    // Add chart and stats to main layout
    auto *central   = centralWidget();
    auto *layout    = central->property("mainLayout").value<QVBoxLayout *>();
    auto *statsLbl  = central->property("statsLabel").value<QLabel *>();

    layout->addWidget(m_chartView, /*stretch=*/1);
    layout->addWidget(statsLbl);
}

void MainWindow::setupModeButtons()
{
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);

    m_modesFrame = new QFrame(centralWidget());
    m_modesFrame->setFrameShape(QFrame::StyledPanel);
    auto *modesFrame  = m_modesFrame;
    auto *modesLayout = new QVBoxLayout(modesFrame);
    modesLayout->setSpacing(2);
    modesLayout->setContentsMargins(6, 4, 6, 4);

    int btnId = 0;
    for (const auto &group : MODE_GROUPS) {
        auto *row    = new QWidget(modesFrame);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setSpacing(3);
        rowLay->setContentsMargins(0, 0, 0, 0);

        auto *lbl = new QLabel(group.label + ":", row);
        lbl->setFixedWidth(42);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lbl->setStyleSheet("color: #777; font-size: 11px;");
        rowLay->addWidget(lbl);

        for (const auto &entry : group.modes) {
            auto *btn = new QPushButton(entry.name, row);
            btn->setCheckable(true);
            btn->setFixedHeight(24);
            btn->setStyleSheet("font-size: 11px; padding: 2px 6px;");

            // Capture code and name in lambda
            const quint8  code = entry.code;
            const QString name = entry.name;
            connect(btn, &QPushButton::clicked, this, [this, code, name]() {
                onModeButtonClicked(code, name);
            });

            m_modeGroup->addButton(btn, btnId++);
            rowLay->addWidget(btn);
        }
        rowLay->addStretch();
        modesLayout->addWidget(row);
    }

    auto *central = centralWidget();
    auto *layout  = central->property("mainLayout").value<QVBoxLayout *>();
    layout->addWidget(modesFrame);

    statusBar()->showMessage(tr("Disconnected"));
}

// ── Slots ────────────────────────────────────────────────────────────────────

void MainWindow::onConnectClicked()
{
    if (m_device->isConnected()) {
        m_device->disconnectDevice();
    } else {
        const QString port = m_portCombo->currentText().trimmed();
        if (port.isEmpty()) {
            statusBar()->showMessage(tr("Enter port name"));
            return;
        }
        m_device->connectDevice(port);
    }
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_btnConnect->setText(connected ? tr("Disconnect") : tr("Connect"));
    m_portCombo->setEnabled(!connected);
    m_cbRelative->setEnabled(connected);

    // Enable/disable mode and pause buttons
    for (auto *btn : m_modeGroup->buttons())
        btn->setEnabled(connected);
    m_btnPause->setEnabled(connected);
    if (!connected) {
        m_btnPause->setChecked(false); // clear pause on disconnect
    }

    if (connected) {
        m_statsTimer->start();
        m_valueLabel->setStyleSheet("color: #00e676; background: #1a1a1a;"
                                    "border-radius: 4px; padding: 8px;");
        // Automatically select VDC mode 400 ms after connecting
        QTimer::singleShot(400, this, [this]() {
            if (m_device->isConnected())
                onModeButtonClicked(0xa0, "VDC");
        });
    } else {
        m_statsTimer->stop();
        m_valueLabel->setText(tr("No data"));
        m_valueLabel->setStyleSheet("color: #555; background: #1a1a1a;"
                                    "border-radius: 4px; padding: 8px;");
    }
}

void MainWindow::onModeButtonClicked(quint8 code, const QString &name)
{
    m_device->setMode(code, name);
}

void MainWindow::onModeChanged(const QString &modeName)
{
    // Highlight the active button
    for (auto *btn : m_modeGroup->buttons()) {
        if (btn->text() == modeName) {
            btn->setChecked(true);
            break;
        }
    }
}

void MainWindow::onPauseToggled(bool paused)
{
    m_device->setPaused(paused);
    if (paused) {
        m_btnPause->setText(tr("▶ Resume"));
        m_valueLabel->setStyleSheet("color: #ffb300; background: #1a1a1a;"
                                    "border-radius: 4px; padding: 8px;");
    } else {
        m_btnPause->setText(tr("⏸ Pause"));
        m_valueLabel->setStyleSheet("color: #00e676; background: #1a1a1a;"
                                    "border-radius: 4px; padding: 8px;");
    }
}

void MainWindow::onToggleModes()
{
    m_modesVisible = !m_modesVisible;
    // Only actually show if the window is currently large enough
    if (m_modesVisible)
        m_modesFrame->setVisible(height() >= 400);
    else
        m_modesFrame->setVisible(false);
    m_btnToggleModes->setText(m_modesVisible ? tr("Modes ▲") : tr("Modes ▼"));
}

void MainWindow::onRelativeToggled(bool checked)
{
    Q_UNUSED(checked)
    m_device->setRelative();
}

void MainWindow::onClearClicked()
{
    m_data.clear();
    m_readingCount = 0;
    m_series->clear();
    m_statsLabel->setText("—");
    const auto now = QDateTime::currentDateTime();
    m_axisX->setRange(now.addSecs(-30), now);
    m_axisY->setRange(-1, 1);
}

void MainWindow::onStatusMessage(const QString &msg)
{
    statusBar()->showMessage(msg);
}

// ── Measurement reception ───────────────────────────────────────────────────────────

void MainWindow::onMeasurement(const Measurement &m)
{
    // Reset data when unit changes
    if (!m_currentBaseUnit.isEmpty() && m_currentBaseUnit != m.baseUnit) {
        m_data.clear();
        m_series->clear();
    }
    m_currentBaseUnit = m.baseUnit;

    // Add data point to buffer
    m_data.append({ QDateTime::currentMSecsSinceEpoch(), m.siValue });
    if (m_data.size() > MAX_POINTS)
        m_data.removeFirst();

    ++m_readingCount;

    // Update display
    m_valueLabel->setText(m.display);

    // Redraw chart
    updateChart();
}

// ── Chart update ────────────────────────────────────────────────────────

void MainWindow::updateChart()
{
    if (m_data.isEmpty()) return;

    // Determine SI scale for Y axis
    double maxAbs = 0;
    for (const auto &dp : m_data)
        maxAbs = std::max(maxAbs, std::abs(dp.siValue));

    if      (maxAbs >= 1e6)  { m_yScale = 1e-6; m_yPrefix = "M";           }
    else if (maxAbs >= 1e3)  { m_yScale = 1e-3; m_yPrefix = "k";           }
    else if (maxAbs >= 1.0)  { m_yScale = 1.0;  m_yPrefix = "";            }
    else if (maxAbs >= 1e-3) { m_yScale = 1e3;  m_yPrefix = "m";           }
    else if (maxAbs >= 1e-6) { m_yScale = 1e6;  m_yPrefix = u8"\u03bc";    } // μ
    else if (maxAbs >= 1e-9) { m_yScale = 1e9;  m_yPrefix = "n";           }
    else                     { m_yScale = 1.0;  m_yPrefix = "";            }

    // Build point set (x = ms since epoch, y = scaled value)
    QList<QPointF> points;
    points.reserve(m_data.size());
    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();

    for (const auto &dp : m_data) {
        const double y = dp.siValue * m_yScale;
        points.append({ static_cast<double>(dp.timestamp), y });
        vmin = std::min(vmin, y);
        vmax = std::max(vmax, y);
    }

    m_series->replace(points);

    // Update axes
    m_axisX->setRange(
        QDateTime::fromMSecsSinceEpoch(m_data.front().timestamp),
        QDateTime::fromMSecsSinceEpoch(m_data.back().timestamp)
    );

    const double span = vmax - vmin;
    double pad = span * 0.12;
    if (pad < 1e-15) pad = std::abs(vmax) * 0.1 + 1e-10;
    m_axisY->setRange(vmin - pad, vmax + pad);
    m_axisY->setTitleText(m_yPrefix + m_currentBaseUnit);
}

// ── Statistics ────────────────────────────────────────────────────────────────

void MainWindow::updateStats()
{
    const double rate = m_readingCount / 2.0;
    m_readingCount = 0;

    if (m_data.isEmpty()) {
        m_statsLabel->setText(
            tr("%1 rdg/s").arg(rate, 0, 'f', 1));
        return;
    }

    double vmin = std::numeric_limits<double>::max();
    double vmax = std::numeric_limits<double>::lowest();
    double sum  = 0;
    for (const auto &dp : m_data) {
        vmin = std::min(vmin, dp.siValue);
        vmax = std::max(vmax, dp.siValue);
        sum += dp.siValue;
    }
    const double avg = sum / m_data.size();

    m_statsLabel->setText(
        tr("Min: %1   Max: %2   Avg: %3   |   %4 rdg/s   |   %5 points")
            .arg(formatSI(vmin, m_currentBaseUnit))
            .arg(formatSI(vmax, m_currentBaseUnit))
            .arg(formatSI(avg,  m_currentBaseUnit))
            .arg(rate, 0, 'f', 1)
            .arg(m_data.size()));
}

// ── Responsive layout ────────────────────────────────────────────────────
//
// Height thresholds:
//   h < 140  → only value label
//   h < 400  → toolbar + value label (no chart, no stats, no modes)
//   h >= 400 → full UI

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);

    const int h = e->size().height();

    const bool showAll     = (h >= 400);
    const bool showToolbar = (h >= 140);

    m_toolbar->setVisible(showToolbar);
    m_chartView->setVisible(showAll);
    m_statsLabel->setVisible(showAll);
    m_modesFrame->setVisible(showAll && m_modesVisible);
}

// ── Helpers ──────────────────────────────────────────────────────────

void MainWindow::refreshPortList()
{
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &info : ports)
        m_portCombo->addItem(info.systemLocation());

    if (m_portCombo->count() == 0)
        m_portCombo->addItem("/dev/ttyACM0");

    // Try to restore previous selection or pick the most likely port
    const int prevIdx = m_portCombo->findText(current);
    if (prevIdx >= 0) {
        m_portCombo->setCurrentIndex(prevIdx);
    } else {
        for (int i = 0; i < m_portCombo->count(); ++i) {
            const QString n = m_portCombo->itemText(i);
            if (n.contains("ACM") || n.contains("USB")) {
                m_portCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

QString MainWindow::formatSI(double val, const QString &baseUnit) const
{
    const double abs = std::abs(val);
    double       s;
    QString      pfx;

    if      (abs >= 1e6)  { s = val * 1e-6; pfx = "M";           }
    else if (abs >= 1e3)  { s = val * 1e-3; pfx = "k";           }
    else if (abs >= 1.0)  { s = val;        pfx = "";            }
    else if (abs >= 1e-3) { s = val * 1e3;  pfx = "m";           }
    else if (abs >= 1e-6) { s = val * 1e6;  pfx = u8"\u03bc";    } // μ
    else if (abs >= 1e-9) { s = val * 1e9;  pfx = "n";           }
    else                  { s = val;        pfx = "";            }

    return QString::number(s, 'g', 5) + " " + pfx + baseUnit;
}
