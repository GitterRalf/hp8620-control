#include "hp8620.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <stdexcept>

bool FrequencyBand::valid() const
{
    return std::isfinite(lower) && std::isfinite(upper) && lower >= 0.0 && upper > lower;
}

bool FrequencyBand::contains(double mhz) const
{
    return valid() && mhz >= lower && mhz <= upper;
}

QByteArray voltageCommand(FrequencyBand band, double mhz)
{
    if (!band.contains(mhz)) {
        const auto message = QCoreApplication::translate(
            "Hp8620", "Die Sollfrequenz liegt außerhalb des aktiven Bands.");
        throw std::out_of_range(message.toStdString());
    }
    const auto millivolts = std::llround(10000.0 * ((mhz - band.lower) / (band.upper - band.lower)));
    // The HP 8620C Option 011 represents the leading digit 10 by ':'.
    if (millivolts == 10000) {
        return "V:000E";
    }
    const auto fraction = QByteArray::number(millivolts % 1000).rightJustified(3, '0');
    return "V" + QByteArray::number(millivolts / 1000) + '.' + fraction + 'E';
}

Hp8620::Hp8620(QObject *parent) : QObject(parent), timer(new QTimer(this))
{
    timer->setInterval(10);
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, &Hp8620::advanceSweep);
}

void Hp8620::connectDevice(const GpibTarget &target, int band)
{
    try {
        device = openGpib(target);
        device->remote();
        QThread::msleep(10);
        device->send("M1");
        selectBand(band);
        emit connectionChanged(true);
    } catch (const std::exception &error) {
        reportFailure(error);
    }
}

void Hp8620::disconnectDevice()
{
    try {
        releaseDevice();
        emit connectionChanged(false);
    } catch (const std::exception &error) {
        reportFailure(error);
    }
}

void Hp8620::selectBand(int band)
{
    if (band != selectedBand) {
        device->send("B" + QByteArray::number(band + 1));
        selectedBand = band;
        lastVoltage.clear();
    }
}

void Hp8620::sendFrequency(FrequencyBand band, double mhz)
{
    const auto command = voltageCommand(band, mhz);
    if (command != lastVoltage) {
        device->send(command);
        lastVoltage = command;
    }
}

void Hp8620::applyFrequency(int bandIndex, FrequencyBand band, double mhz)
{
    if (!device || timer->isActive()) {
        return;
    }
    try {
        selectBand(bandIndex);
        // Explicit tuning also reasserts the setpoint after local changes on the instrument.
        lastVoltage.clear();
        sendFrequency(band, mhz);
        emit frequencyApplied(mhz);
    } catch (const std::exception &error) {
        reportFailure(error);
    }
}

void Hp8620::startSweep(int bandIndex, FrequencyBand band, FrequencySweep request)
{
    if (!device || timer->isActive()) {
        return;
    }
    try {
        selectBand(bandIndex);
        sweepBand = band;
        sweep = request;
        lastVoltage.clear();
        sendFrequency(sweepBand, sweep.start);
        elapsed.start();
        lastProgress = 0;
        emit frequencyApplied(sweep.start);
        emit sweepProgress(0);
        timer->start();
    } catch (const std::exception &error) {
        reportFailure(error);
    }
}

void Hp8620::stopSweep()
{
    if (timer->isActive()) {
        timer->stop();
        emit sweepCompleted(true);
    }
}

void Hp8620::advanceSweep()
{
    try {
        if (!elapsed.isValid()) {
            sendFrequency(sweepBand, sweep.start);
            elapsed.start();
            lastProgress = 0;
            emit frequencyApplied(sweep.start);
            emit sweepProgress(0);
            return;
        }
        const auto duration = sweep.durationSeconds * 1000.0;
        const auto fraction = std::min(1.0, elapsed.elapsed() / duration);
        const auto mhz = fraction == 1.0 ? sweep.stop
                         : sweep.start + (sweep.stop - sweep.start) * fraction;
        sendFrequency(sweepBand, mhz);
        // Throttle updates to the GUI independently of GPIB write timing.
        const int progress = static_cast<int>(fraction * 1000.0);
        if (progress == 1000 || progress - lastProgress >= 10) {
            lastProgress = progress;
            emit frequencyApplied(mhz);
            emit sweepProgress(progress);
        }
        if (fraction < 1.0) {
            return;
        }
        if (sweep.repeat) {
            // Yield to the event loop at the endpoint so a pending Stop can take effect.
            elapsed.invalidate();
            return;
        }
        timer->stop();
        emit sweepCompleted(false);
    } catch (const std::exception &error) {
        reportFailure(error);
    }
}

void Hp8620::releaseDevice()
{
    timer->stop();
    selectedBand = -1;
    lastVoltage.clear();
    auto connection = std::move(device);
    if (connection) {
        connection->local();
    }
}

void Hp8620::reportFailure(const std::exception &error)
{
    auto message = QString::fromUtf8(error.what());
    try {
        releaseDevice();
    } catch (const std::exception &localError) {
        message += QLatin1Char('\n') + QString::fromUtf8(localError.what());
    }
    emit connectionChanged(false);
    emit failed(message);
}
