#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

struct GpibTarget
{
    int board = 0;
    int address = 6;
    bool elektor = false;
    QString serialPort;
};

class GpibConnection
{
public:
    virtual ~GpibConnection() = default;
    virtual void remote() = 0;
    virtual void local() = 0;
    virtual void send(const QByteArray &command) = 0;
};

std::unique_ptr<GpibConnection> openGpib(const GpibTarget &target);
