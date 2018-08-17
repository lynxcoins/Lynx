#include <QLabel>
#include <QTimer>
#include "builtin_miner.h"
#include "builtinminerstatus.h"

BuilinMinerStatus::BuilinMinerStatus(QWidget* parent) : QLabel(parent)
{
    auto updateTimer = new QTimer(this);
    connect(updateTimer, SIGNAL(timeout()), this, SLOT(updateStatus()));
    updateTimer->start(250);

    updateStatus();
}

BuilinMinerStatus::~BuilinMinerStatus()
{
}

void BuilinMinerStatus::updateStatus()
{
    QString status;
    if (BuiltinMiner::isRunning())
        status = tr("running");
    else
        status = tr("stopped");

    QString newText = tr("The built-in miner is %1").arg(status);
    setText(newText);
}

void BuilinMinerStatus::mouseReleaseEvent(QMouseEvent *event)
{
    if (BuiltinMiner::isRunning())
        BuiltinMiner::stop();
    else
        BuiltinMiner::start();
    updateStatus();
}
