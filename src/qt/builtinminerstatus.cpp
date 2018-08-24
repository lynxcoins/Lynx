#include <QLabel>
#include <QTimer>
#include "builtin_miner.h"
#include "builtinminerstatus.h"

BuiltinMinerStatus::BuiltinMinerStatus(QWidget* parent) : QLabel(parent)
{
    auto updateTimer = new QTimer(this);
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
    updateTimer->start(250);

    updateStatus();
}

BuiltinMinerStatus::~BuiltinMinerStatus()
{
}

void BuiltinMinerStatus::setRunningIcon(const QPixmap& pixmap)
{
    runningIcon = pixmap;
}

void BuiltinMinerStatus::setStoppedIcon(const QPixmap& pixmap)
{
    stoppedIcon = pixmap;
}

void BuiltinMinerStatus::updateStatus()
{
    QString status;
    QString action;
    if (BuiltinMiner::isRunning())
    {
        setPixmap(runningIcon);
        status = tr("running");
        action = tr("stop");
    }
    else
    {
        status = tr("stopped");
        action = tr("start");
        setPixmap(stoppedIcon);
    }

    QString newToolTip = tr("The built-in miner is %1. Click to %2 the miner.")
        .arg(status, action);
    setToolTip(newToolTip);
}

void BuiltinMinerStatus::mouseReleaseEvent(QMouseEvent *event)
{
    if (BuiltinMiner::isRunning())
        BuiltinMiner::stop();
    else
        BuiltinMiner::start();
    updateStatus();
}
