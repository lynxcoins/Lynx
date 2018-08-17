#ifndef BITCOIN_QT_BUILTINMINERSTATUS_H
#define BITCOIN_QT_BUILTINMINERSTATUS_H

#include <QLabel>

class BuilinMinerStatus : public QLabel
{
    Q_OBJECT

public:
    explicit BuilinMinerStatus(QWidget* parent = nullptr);
    ~BuilinMinerStatus() override;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;

private Q_SLOTS:
    void updateStatus();
};

#endif // BITCOIN_QT_BUILTINMINERSTATUS_H