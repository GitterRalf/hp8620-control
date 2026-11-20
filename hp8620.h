#pragma once

#include "gpib.h"

#include <QElapsedTimer>
#include <QObject>

#include <array>

class QTimer;

struct FrequencyBand
{
    double lower;
    double upper;

    bool valid() const;
    bool contains(double mhz) const;
};

inline constexpr std::array<FrequencyBand, 4> defaultBands{{
    {0.1, 2000.0}, {1800.0, 4200.0}, {5900.0, 9000.0}, {0.0, 2400.0}
}};

QByteArray voltageCommand(FrequencyBand band, double mhz);

struct FrequencySweep
{
    double start;
    double stop;
    int durationSeconds;
    bool repeat;
};

class Hp8620 final : public QObject
{
    Q_OBJECT

public:
    explicit Hp8620(QObject *parent = nullptr);

    void connectDevice(const GpibTarget &target, int band);
    void disconnectDevice();
    void applyFrequency(int bandIndex, FrequencyBand band, double mhz);
    void startSweep(int bandIndex, FrequencyBand band, FrequencySweep sweep);
    void stopSweep();

signals:
    void connectionChanged(bool connected);
    void frequencyApplied(double mhz);
    void sweepProgress(int permille);
    void sweepCompleted(bool stopped);
    void failed(const QString &message);

private:
    void selectBand(int band);
    void sendFrequency(FrequencyBand band, double mhz);
    void advanceSweep();
    void releaseDevice();
    void reportFailure(const std::exception &error);

    std::unique_ptr<GpibConnection> device;
    int selectedBand = -1;
    QByteArray lastVoltage;
    QTimer *timer;
    QElapsedTimer elapsed;
    FrequencyBand sweepBand{};
    FrequencySweep sweep{};
    int lastProgress = -1;
};
