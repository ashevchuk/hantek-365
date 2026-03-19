#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QButtonGroup>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QValueAxis>
#include <QDateTimeAxis>
#include <QTimer>
#include <QVector>

#include "hantekdevice.h"

struct DataPoint {
    qint64 timestamp; // ms since epoch
    double siValue;
};

// ── Mode definitions ────────────────────────────────────────────────────────
struct ModeEntry {
    QString name;
    quint8  code;
};
struct ModeGroup {
    QString            label;
    QVector<ModeEntry> modes;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnectClicked();
    void onMeasurement(const Measurement &m);
    void onConnectionChanged(bool connected);
    void onModeChanged(const QString &modeName);
    void onStatusMessage(const QString &msg);
    void onModeButtonClicked(quint8 code, const QString &name);
    void onRelativeToggled(bool checked);
    void onPauseToggled(bool paused);
    void onToggleModes();
    void onClearClicked();
    void refreshPortList();
    void updateStats();

private:
    void setupUI();
    void setupChart();
    void setupModeButtons();
    void updateChart();
    QString formatSI(double val, const QString &baseUnit) const;

    void resizeEvent(QResizeEvent *e) override;

    // ── Widgets ─────────────────────────────────────────────────────────────
    QFrame       *m_toolbar         = nullptr; // top toolbar frame
    QComboBox    *m_portCombo       = nullptr;
    QPushButton  *m_btnConnect      = nullptr;
    QPushButton  *m_btnPause        = nullptr; // checkable, pause measurements
    QPushButton  *m_btnToggleModes  = nullptr; // hide/show mode panel
    QPushButton  *m_cbRelative      = nullptr; // checkable REL button
    QLabel       *m_valueLabel      = nullptr;
    QLabel       *m_statsLabel      = nullptr;
    QButtonGroup *m_modeGroup       = nullptr;
    QFrame       *m_modesFrame      = nullptr; // bottom mode panel
    bool          m_modesVisible    = true;    // user-requested modes visibility

    // ── Chart ───────────────────────────────────────────────────────────────
    QChart        *m_chart     = nullptr;
    QChartView    *m_chartView = nullptr;
    QLineSeries   *m_series    = nullptr;
    QDateTimeAxis *m_axisX     = nullptr;
    QValueAxis    *m_axisY     = nullptr;

    // ── Data ────────────────────────────────────────────────────────────────
    static constexpr int MAX_POINTS = 500;
    QVector<DataPoint> m_data;
    QString m_currentBaseUnit;
    double  m_yScale  = 1.0;
    QString m_yPrefix;

    // ── Statistics ──────────────────────────────────────────────────────────
    QTimer *m_statsTimer   = nullptr;
    int     m_readingCount = 0;

    // ── Device ──────────────────────────────────────────────────────────────
    HantekDevice *m_device = nullptr;
};
