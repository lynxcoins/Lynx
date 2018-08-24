#ifndef BITCOIN_QT_BUILTINMINERSTATUS_H
#define BITCOIN_QT_BUILTINMINERSTATUS_H

#include <QLabel>
#include <QPixmap>

class BuiltinMinerStatus : public QLabel
{
    Q_OBJECT

public:
    explicit BuiltinMinerStatus(QWidget* parent = nullptr);
    ~BuiltinMinerStatus() override;

    void setRunningIcon(const QPixmap& pixmap);
    void setStoppedIcon(const QPixmap& pixmap);

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;

private Q_SLOTS:
    void updateStatus();

private:
    QPixmap runningIcon;
    QPixmap stoppedIcon;
};

#endif // BITCOIN_QT_BUILTINMINERSTATUS_H