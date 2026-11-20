#pragma once

#include "hp8620.h"

#include <QMainWindow>
#include <QSettings>
#include <QThread>
#include <QTranslator>

#include <functional>

#include <optional>

class QAction;
class QCheckBox;
class QCloseEvent;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Connection { Disconnected, Connecting, Connected, Disconnecting, Failed };
    enum class SweepState { Idle, Running, Stopping };

    QFrame *frequencyPanel();
    QFrame *bandPanel();
    QFrame *sweepPanel();
    void createMenus();
    void showAbout();
    void checkForUpdates();
    void checkPrivateRelease();
    void finishUpdateCheck(const QByteArray &data, const QString &error = {});
    void installLanguage(const QString &code);
    void changeLanguage(const QString &code);
    void retranslateUi();
    void setMessage(std::function<QString()> text = {});
    void setSweepStatus(const char *text);
    void editConnection();
    void setDarkTheme(bool dark);
    void loadSettings();
    void saveSettings();
    void editBandLimits();
    void selectBand(int band);
    void displayFrequency(double mhz);
    void updateBandDisplay();
    void updateControls();
    void toggleConnection();
    void applyFrequency();
    void queueFrequency(double mhz);
    void toggleSweep();

    QSettings settings;
    QTranslator appTranslator;
    QTranslator qtTranslator;
    QString language;
    std::function<QString()> currentMessage;
    const char *sweepStatusText = nullptr;
    GpibTarget target;
    bool darkTheme = true;
    QMenu *settingsMenu;
    QMenu *appearanceMenu;
    QMenu *languageMenu = nullptr;
    QMenu *aboutMenu;
    QAction *connectionSettings;
    QAction *lightThemeAction;
    QAction *darkThemeAction;
    QAction *saveSettingsAction;
    QAction *aboutApplicationAction;
    QAction *aboutQtAction;
    QAction *updateAction;
    std::array<FrequencyBand, 4> bands = defaultBands;
    int currentBand = 3;
    Connection connection = Connection::Disconnected;
    SweepState sweepState = SweepState::Idle;
    bool tuning = false;
    std::optional<double> pendingFrequency;
    bool closing = false;
    QThread instrumentThread;
    Hp8620 *const instrument;

    QLabel *frequencyTitle;
    QLabel *bandTitle;
    QLabel *sweepTitle;
    QLabel *sweepStartLabel;
    QLabel *sweepStopLabel;
    QLabel *sweepDurationLabel;
    QLabel *connectionState;
    QLabel *frequencyValue;
    QLabel *lowFrequency;
    QLabel *midFrequency;
    QLabel *highFrequency;
    QLabel *message;
    QSlider *slider;
    QDoubleSpinBox *frequency;
    QPushButton *apply;
    QPushButton *editBands;
    std::array<QRadioButton *, 4> bandButtons{};
    std::array<QLabel *, 4> bandRanges{};

    QDoubleSpinBox *sweepStart;
    QDoubleSpinBox *sweepStop;
    QSpinBox *sweepDuration;
    QCheckBox *repeat;
    QPushButton *sweepAction;
    QLabel *sweepStatus;
    QProgressBar *progress;

    QPushButton *connectButton;
};
