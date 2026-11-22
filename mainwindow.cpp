#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int sliderSteps = 10000; // 10 V in steps of 1 mV.
constexpr double maximumMHz = 1000000.0;

QLocale numericLocale()
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale;
}

QString compactFrequency(double mhz)
{
    const auto locale = numericLocale();
    if (mhz >= 1000.0) {
        return locale.toString(mhz / 1000.0, 'g', 8) + QStringLiteral(" GHz");
    }
    if (mhz > 0.0 && mhz < 1.0) {
        return locale.toString(mhz * 1000.0, 'g', 8) + QStringLiteral(" kHz");
    }
    return locale.toString(mhz, 'g', 8) + QStringLiteral(" MHz");
}

QLabel *label(const QString &text, const char *name)
{
    auto *widget = new QLabel(text);
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}

QFrame *panel()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("panel"));
    return frame;
}

QDoubleSpinBox *frequencyInput()
{
    auto *editor = new QDoubleSpinBox;
    editor->setRange(0.0, maximumMHz);
    editor->setDecimals(3);
    editor->setSingleStep(1.0);
    editor->setLocale(numericLocale());
    editor->setSuffix(QStringLiteral(" MHz"));
    editor->setKeyboardTracking(false);
    editor->setButtonSymbols(QAbstractSpinBox::NoButtons);
    editor->setMinimumWidth(116);
    return editor;
}

QLabel *field(QGridLayout *layout, QWidget *editor, int column)
{
    auto *title = label({}, "muted");
    title->setBuddy(editor);
    layout->addWidget(title, 0, column);
    layout->addWidget(editor, 1, column);
    return title;
}

QIcon globeIcon(bool dark)
{
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(dark ? "#d4e3f5" : "#27425a"), 1.35));
    painter.drawEllipse(QRectF(3, 3, 18, 18));
    painter.drawEllipse(QRectF(8, 3, 8, 18));
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(5, 7.5), QPointF(19, 7.5));
    painter.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    return QIcon(icon);
}

void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), instrument(new Hp8620)
{
    setWindowTitle(QStringLiteral("HP8620 Control"));
    resize(920, 488);
    installLanguage(settings.value(QStringLiteral("language"), QStringLiteral("de")).toString());
    setDarkTheme(settings.value(QStringLiteral("appearance/dark"), true).toBool());
    createMenus();

    auto *body = new QWidget;
    setCentralWidget(body);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(28, 20, 28, 16);
    layout->setSpacing(12);

    auto *headline = new QHBoxLayout;
    headline->addWidget(label(QStringLiteral("HP8620 Control"), "title"));
    headline->addStretch();
    connectionState = label(QString(), "connectionState");
    headline->addWidget(connectionState);
    connectButton = new QPushButton;
    connectButton->setObjectName(QStringLiteral("connectDevice"));
    connectButton->setMinimumWidth(132);
    headline->addWidget(connectButton);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    layout->addLayout(headline);

    auto *tuningLayout = new QHBoxLayout;
    tuningLayout->setSpacing(12);
    tuningLayout->addWidget(frequencyPanel(), 3);
    tuningLayout->addWidget(bandPanel(), 2);
    layout->addLayout(tuningLayout);
    layout->addWidget(sweepPanel());

    message = label(QString(), "muted");
    message->setMinimumHeight(20);
    message->setWordWrap(true);
    message->setTextFormat(Qt::PlainText);
    layout->addWidget(message);
    layout->addStretch();

    loadSettings();
    setSweepStatus(QT_TR_NOOP("Bereit"));
    retranslateUi();
    instrument->moveToThread(&instrumentThread);
    connect(&instrumentThread, &QThread::finished, instrument, &QObject::deleteLater);
    connect(instrument, &Hp8620::connectionChanged, this, [this](bool connected) {
        if (closing && connected) {
            return;
        }
        connection = connected ? Connection::Connected : Connection::Disconnected;
        tuning = false;
        pendingFrequency.reset();
        sweepState = SweepState::Idle;
        if (connected) {
            setMessage([band = currentBand + 1] { return tr("Band %1 ist ausgewählt.").arg(band); });
        } else {
            progress->reset();
            setSweepStatus(QT_TR_NOOP("Bereit"));
            setMessage([] { return tr("GPIB-Verbindung getrennt."); });
        }
        updateControls();
        if (closing && !connected) {
            close();
        }
    });
    connect(instrument, &Hp8620::failed, this, [this](const QString &error) {
        connection = Connection::Failed;
        setMessage([error] { return error; });
        updateControls();
    });
    connect(instrument, &Hp8620::frequencyApplied, this, [this](double mhz) {
        if (!tuning) {
            displayFrequency(mhz);
            return;
        }
        tuning = false;
        const auto queued = std::exchange(pendingFrequency, std::nullopt);
        if (queued && connection == Connection::Connected) {
            queueFrequency(*queued);
            return;
        }
        setMessage([address = target.address, mhz] {
            return tr("An GPIB-Adresse %1 übertragen: %2 MHz.")
                .arg(address).arg(numericLocale().toString(mhz, 'f', 3));
        });
        updateControls();
    });
    connect(instrument, &Hp8620::sweepProgress, this, [this](int permille) {
        progress->setValue(permille);
        if (sweepState == SweepState::Running) {
            setSweepStatus(nullptr);
        }
    });
    connect(instrument, &Hp8620::sweepCompleted, this, [this](bool stopped) {
        sweepState = SweepState::Idle;
        setSweepStatus(stopped ? QT_TR_NOOP("Gestoppt") : QT_TR_NOOP("Abgeschlossen"));
        updateControls();
    });

    instrumentThread.start();
}

MainWindow::~MainWindow()
{
    if (instrumentThread.isRunning()) {
        QMetaObject::invokeMethod(instrument, &Hp8620::disconnectDevice, Qt::BlockingQueuedConnection);
        instrumentThread.quit();
        instrumentThread.wait();
    }
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (connection == Connection::Connected || connection == Connection::Connecting
        || connection == Connection::Disconnecting) {
        event->ignore();
        closing = true;
        if (connection != Connection::Disconnecting) {
            connection = Connection::Disconnecting;
            updateControls();
            QMetaObject::invokeMethod(instrument, &Hp8620::disconnectDevice, Qt::QueuedConnection);
        }
        return;
    }
    event->accept();
}

QFrame *MainWindow::frequencyPanel()
{
    auto *frame = panel();
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(9);
    frequencyTitle = label({}, "sectionTitle");
    layout->addWidget(frequencyTitle);

    auto *readout = new QFrame;
    readout->setObjectName(QStringLiteral("readout"));
    auto *display = new QHBoxLayout(readout);
    display->setContentsMargins(12, 3, 12, 4);
    display->setSpacing(8);
    frequencyValue = label(QString(), "frequencyValue");
    frequencyValue->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    display->addWidget(frequencyValue, 0, Qt::AlignBaseline);
    display->addWidget(label(QStringLiteral("MHz"), "frequencyUnit"), 0, Qt::AlignBaseline);
    display->addStretch();
    layout->addWidget(readout);

    slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(QStringLiteral("frequencySlider"));
    slider->setRange(0, sliderSteps);
    slider->setSingleStep(1);
    slider->setPageStep(100);
    slider->setMinimumHeight(24);
    layout->addWidget(slider);

    auto *ticks = new QHBoxLayout;
    lowFrequency = label(QString(), "muted");
    midFrequency = label(QString(), "muted");
    highFrequency = label(QString(), "muted");
    ticks->addWidget(lowFrequency, 1, Qt::AlignLeft);
    ticks->addWidget(midFrequency, 1, Qt::AlignHCenter);
    ticks->addWidget(highFrequency, 1, Qt::AlignRight);
    layout->addLayout(ticks);

    auto *edit = new QHBoxLayout;
    frequency = frequencyInput();
    frequency->setObjectName(QStringLiteral("frequencyInput"));
    apply = new QPushButton;
    apply->setObjectName(QStringLiteral("applyFrequency"));
    apply->setProperty("primary", true);
    edit->addWidget(frequency, 1);
    edit->addWidget(apply);
    layout->addStretch();
    layout->addLayout(edit);

    connect(slider, &QSlider::valueChanged, this, [this](int step) {
        const auto band = bands[currentBand];
        displayFrequency(band.lower + (band.upper - band.lower) * step / sliderSteps);
        if (!slider->isSliderDown()) {
            applyFrequency();
        }
    });
    connect(slider, &QSlider::sliderReleased, this, &MainWindow::applyFrequency);
    connect(frequency, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::displayFrequency);
    connect(apply, &QPushButton::clicked, this, &MainWindow::applyFrequency);
    auto *enter = new QShortcut(QKeySequence(Qt::Key_Return), frequency);
    enter->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enter, &QShortcut::activated, this, &MainWindow::applyFrequency);
    return frame;
}

QFrame *MainWindow::bandPanel()
{
    auto *frame = panel();
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(9);
    bandTitle = label({}, "sectionTitle");
    layout->addWidget(bandTitle);

    auto *group = new QButtonGroup(frame);
    auto *grid = new QGridLayout;
    grid->setVerticalSpacing(6);
    grid->setHorizontalSpacing(12);
    grid->setColumnStretch(1, 1);
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        auto *button = new QRadioButton;
        button->setObjectName(QStringLiteral("band%1").arg(i + 1));
        bandButtons[i] = button;
        bandRanges[i] = label(QString(), "muted");
        bandRanges[i]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        group->addButton(button, i);
        grid->addWidget(button, i, 0);
        grid->addWidget(bandRanges[i], i, 1);
    }
    layout->addLayout(grid);
    layout->addStretch();
    editBands = new QPushButton;
    editBands->setObjectName(QStringLiteral("editBands"));
    layout->addWidget(editBands, 0, Qt::AlignLeft);
    connect(group, &QButtonGroup::idClicked, this, &MainWindow::selectBand);
    connect(editBands, &QPushButton::clicked, this, &MainWindow::editBandLimits);
    return frame;
}

QFrame *MainWindow::sweepPanel()
{
    auto *frame = panel();
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 11, 16, 13);
    layout->setSpacing(9);

    auto *header = new QHBoxLayout;
    sweepTitle = label({}, "sectionTitle");
    header->addWidget(sweepTitle);
    header->addStretch();
    sweepStatus = label({}, "sweepState");
    header->addWidget(sweepStatus);
    layout->addLayout(header);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    sweepStart = frequencyInput();
    sweepStart->setObjectName(QStringLiteral("sweepStart"));
    sweepStop = frequencyInput();
    sweepStop->setObjectName(QStringLiteral("sweepStop"));
    sweepDuration = new QSpinBox;
    sweepDuration->setObjectName(QStringLiteral("sweepDuration"));
    sweepDuration->setRange(1, 1000);
    sweepDuration->setSuffix(QStringLiteral(" s"));
    sweepDuration->setKeyboardTracking(false);
    sweepDuration->setButtonSymbols(QAbstractSpinBox::NoButtons);
    repeat = new QCheckBox;
    repeat->setObjectName(QStringLiteral("repeatSweep"));
    sweepAction = new QPushButton;
    sweepAction->setObjectName(QStringLiteral("sweepAction"));
    sweepAction->setProperty("primary", true);
    sweepAction->setMinimumWidth(132);
    sweepStartLabel = field(grid, sweepStart, 0);
    sweepStopLabel = field(grid, sweepStop, 1);
    sweepDurationLabel = field(grid, sweepDuration, 2);
    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(1, 2);
    grid->setColumnStretch(2, 1);
    grid->addWidget(repeat, 1, 3);
    grid->addWidget(sweepAction, 1, 4);
    layout->addLayout(grid);

    progress = new QProgressBar;
    progress->setObjectName(QStringLiteral("sweepProgress"));
    progress->setRange(0, 1000);
    progress->setTextVisible(false);
    layout->addWidget(progress);
    connect(sweepAction, &QPushButton::clicked, this, &MainWindow::toggleSweep);
    return frame;
}

void MainWindow::createMenus()
{
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    settingsMenu = bar->addMenu(QString());
    settingsMenu->setObjectName(QStringLiteral("settingsMenu"));
    connectionSettings = settingsMenu->addAction(QString(), this, &MainWindow::editConnection);
    connectionSettings->setObjectName(QStringLiteral("connectionSettings"));

    appearanceMenu = settingsMenu->addMenu(QString());
    auto *themes = new QActionGroup(appearanceMenu);
    lightThemeAction = appearanceMenu->addAction(QString());
    lightThemeAction->setObjectName(QStringLiteral("lightTheme"));
    darkThemeAction = appearanceMenu->addAction(QString());
    darkThemeAction->setObjectName(QStringLiteral("darkTheme"));
    for (auto *action : {lightThemeAction, darkThemeAction}) {
        const bool dark = action == darkThemeAction;
        action->setCheckable(true);
        action->setChecked(dark == darkTheme);
        themes->addAction(action);
        connect(action, &QAction::triggered, this, [this, dark] { setDarkTheme(dark); });
    }
    settingsMenu->addSeparator();
    saveSettingsAction = settingsMenu->addAction(QString(), this, &MainWindow::saveSettings, QKeySequence::Save);
    saveSettingsAction->setObjectName(QStringLiteral("saveSettings"));

    auto *right = new QMenuBar(bar);
    right->setNativeMenuBar(false);
    languageMenu = right->addMenu(QString());
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    languageMenu->setIcon(globeIcon(darkTheme));
    auto *languages = new QActionGroup(languageMenu);
    const std::array choices{
        std::pair{QStringLiteral("de"), QStringLiteral("Deutsch")},
        std::pair{QStringLiteral("en"), QStringLiteral("English")},
        std::pair{QStringLiteral("fr"), QStringLiteral("Français")}
    };
    for (const auto &[code, name] : choices) {
        auto *action = languageMenu->addAction(name);
        action->setData(code);
        action->setCheckable(true);
        action->setChecked(code == language);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }

    aboutMenu = right->addMenu(QString());
    aboutMenu->setObjectName(QStringLiteral("aboutMenu"));
    aboutApplicationAction = aboutMenu->addAction(QString(), this, &MainWindow::showAbout);
    aboutApplicationAction->setObjectName(QStringLiteral("aboutApplication"));
    aboutQtAction = aboutMenu->addAction(QString(), this, [this] {
        QMessageBox::aboutQt(this, tr("Über Qt"));
    });
    aboutQtAction->setObjectName(QStringLiteral("aboutQt"));
    aboutMenu->addSeparator();
    updateAction = aboutMenu->addAction(QString(), this, &MainWindow::checkForUpdates);
    updateAction->setObjectName(QStringLiteral("checkForUpdates"));
    bar->setCornerWidget(right, Qt::TopRightCorner);
}

void MainWindow::showAbout()
{
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Über HP8620 Control"));
    dialog.setTextFormat(Qt::RichText);
    dialog.setText(QStringLiteral("<b>%1</b><br>%2")
                       .arg(QApplication::applicationDisplayName().toHtmlEscaped(),
                            tr("Version %1").arg(QCoreApplication::applicationVersion())));
    dialog.setStandardButtons(QMessageBox::Ok);

    QMovie logo(QStringLiteral(":/branding/logo_g.gif"));
    if (logo.isValid()) {
        connect(&logo, &QMovie::frameChanged, &dialog, [&] {
            dialog.setIconPixmap(logo.currentPixmap());
        });
        logo.start();
    } else {
        dialog.setIconPixmap(windowIcon().pixmap(64));
    }

    dialog.exec();
}

void MainWindow::installLanguage(const QString &code)
{
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
    language = QStringLiteral("de");
    if ((code == QStringLiteral("en") || code == QStringLiteral("fr"))
        && appTranslator.load(QStringLiteral(":/i18n/hp8620_") + code)) {
        language = code;
    }

    const QLocale selectedLocale(language);
    QLocale::setDefault(selectedLocale);
    setLocale(selectedLocale);
    if (qtTranslator.load(QStringLiteral(":/qt-i18n/qtbase_") + language)) {
        qApp->installTranslator(&qtTranslator);
    }
    if (language != QStringLiteral("de")) {
        qApp->installTranslator(&appTranslator);
    }
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == language) {
        return;
    }

    for (auto *editor : {frequency, sweepStart, sweepStop}) {
        editor->interpretText();
    }
    sweepDuration->interpretText();
    installLanguage(code);
    for (auto *action : languageMenu->actions()) {
        action->setChecked(action->data().toString() == language);
    }
    settings.setValue(QStringLiteral("language"), language);
    retranslateUi();
}

void MainWindow::retranslateUi()
{
    settingsMenu->setTitle(tr("&Einstellungen"));
    connectionSettings->setText(tr("Verbindung …"));
    appearanceMenu->setTitle(tr("Darstellung"));
    lightThemeAction->setText(tr("Hell"));
    darkThemeAction->setText(tr("Dunkel"));
    saveSettingsAction->setText(tr("Einstellungen speichern"));
    languageMenu->menuAction()->setToolTip(tr("Sprache wählen"));
    aboutMenu->setTitle(tr("&Über"));
    aboutApplicationAction->setText(tr("Über HP8620 Control"));
    aboutQtAction->setText(tr("Über Qt"));
    updateAction->setText(tr("Nach Updates suchen"));

    // Reposition the corner menu when its translated titles change its width.
    auto *bar = menuBar();
    auto *right = bar->cornerWidget(Qt::TopRightCorner);
    bar->setCornerWidget(nullptr, Qt::TopRightCorner);
    bar->setCornerWidget(right, Qt::TopRightCorner);

    frequencyTitle->setText(tr("Sollfrequenz"));
    slider->setAccessibleName(tr("Sollfrequenz einstellen"));
    frequency->setAccessibleName(tr("Sollfrequenz in Megahertz"));
    apply->setText(tr("Übernehmen"));
    bandTitle->setText(tr("Frequenzbänder"));
    for (int i = 0; i < static_cast<int>(bandButtons.size()); ++i) {
        bandButtons[i]->setText(tr("Band %1").arg(i + 1));
    }
    editBands->setText(tr("Bandgrenzen bearbeiten"));
    sweepTitle->setText(tr("Sweep"));
    sweepStartLabel->setText(tr("Startfrequenz"));
    sweepStopLabel->setText(tr("Stoppfrequenz"));
    sweepDurationLabel->setText(tr("Dauer"));
    repeat->setText(tr("Wiederholen"));
    progress->setAccessibleName(tr("Sweep-Fortschritt"));

    const QLocale numberFormat = numericLocale();
    for (auto *editor : {frequency, sweepStart, sweepStop}) {
        editor->setLocale(numberFormat);
    }
    sweepDuration->setLocale(numberFormat);
    updateBandDisplay();
    updateControls();
    setSweepStatus(sweepStatusText);
    message->setText(currentMessage ? currentMessage() : QString());
}

void MainWindow::setMessage(std::function<QString()> text)
{
    currentMessage = std::move(text);
    message->setText(currentMessage ? currentMessage() : QString());
}

void MainWindow::setSweepStatus(const char *text)
{
    sweepStatusText = text;
    if (text) {
        sweepStatus->setText(tr(text));
    } else {
        const auto duration = sweepDuration->value();
        const auto seconds = duration * progress->value() / 1000.0;
        sweepStatus->setText(tr("%1 / %2 s").arg(numericLocale().toString(seconds, 'f', 1)).arg(duration));
    }
}

void MainWindow::editConnection()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("GPIB-Verbindung"));
    dialog.setMinimumWidth(360);
    auto *form = new QFormLayout(&dialog);
    form->setContentsMargins(18, 16, 18, 16);
    form->setSpacing(10);

    auto *adapter = new QComboBox;
    adapter->setObjectName(QStringLiteral("adapter"));
    adapter->addItems({QStringLiteral("NI-488.2"), QStringLiteral("Elektor USB-GPIB")});
    adapter->setCurrentIndex(target.elektor ? 1 : 0);
    form->addRow(tr("Adapter"), adapter);

    auto *board = new QSpinBox;
    board->setObjectName(QStringLiteral("gpibBoard"));
    board->setRange(0, 99);
    board->setValue(target.board);
    auto *serial = new QComboBox;
    serial->setObjectName(QStringLiteral("serialPort"));
    serial->setEditable(true);
    serial->setInsertPolicy(QComboBox::NoInsert);
    auto *interfaceStack = new QStackedWidget;
    interfaceStack->addWidget(board);
    interfaceStack->addWidget(serial);
    auto *interfaceLabel = new QLabel;
    form->addRow(interfaceLabel, interfaceStack);

    const auto updateInterface = [=](int index) {
        const bool elektor = index == 1;
        interfaceStack->setCurrentIndex(index);
        interfaceLabel->setText(elektor ? tr("COM-Port") : tr("GPIB-Bus"));
        interfaceLabel->setBuddy(interfaceStack->currentWidget());
        if (elektor) {
            const auto preferred = serial->currentText();
            serial->clear();
            for (const auto &port : QSerialPortInfo::availablePorts()) {
                serial->addItem(port.portName());
            }
            if (!preferred.isEmpty()) {
                serial->setCurrentText(preferred);
            }
        }
    };
    serial->setCurrentText(target.serialPort);
    updateInterface(adapter->currentIndex());
    connect(adapter, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateInterface);

    auto *address = new QSpinBox;
    address->setObjectName(QStringLiteral("gpibAddress"));
    address->setRange(0, 30);
    address->setValue(target.address);
    form->addRow(tr("Primäradresse"), address);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    buttons->addButton(tr("Übernehmen"), QDialogButtonBox::AcceptRole);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        target = {board->value(), address->value(), adapter->currentIndex() == 1, serial->currentText().trimmed()};
        const auto interface = target.elektor ? target.serialPort : QStringLiteral("GPIB %1").arg(target.board);
        setMessage([interface, address = target.address] {
            return interface.isEmpty() ? tr("COM-Port des Elektor-GPIB-Adapters auswählen.")
                                       : tr("%1 · Adresse %2").arg(interface).arg(address);
        });
    }
}

void MainWindow::setDarkTheme(bool dark)
{
    darkTheme = dark;
    QPalette palette;
    const auto setColor = [&](QPalette::ColorRole role, const char *darkColor, const char *lightColor) {
        palette.setColor(role, QColor(dark ? darkColor : lightColor));
    };
    setColor(QPalette::Window, "#10191d", "#eef4f0");
    setColor(QPalette::WindowText, "#e9f1f1", "#172c28");
    setColor(QPalette::Base, "#111e23", "#ffffff");
    setColor(QPalette::AlternateBase, "#1b2b31", "#e7f0ea");
    setColor(QPalette::Text, "#e9f1f1", "#172c28");
    setColor(QPalette::Button, "#21343a", "#e1ece5");
    setColor(QPalette::ButtonText, "#e9f1f1", "#172c28");
    setColor(QPalette::Highlight, "#b8ea98", "#c0e7a8");
    setColor(QPalette::HighlightedText, "#142517", "#142517");
    setColor(QPalette::PlaceholderText, "#9cb0b4", "#4b625c");
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#82969a" : "#586b65"));
    }
    qApp->setPalette(palette);

    QFile theme(QStringLiteral(":/theme.qss"));
    QString style;
    if (theme.open(QIODevice::ReadOnly)) {
        style = QString::fromUtf8(theme.readAll());
    }
    if (!dark) {
        QFile light(QStringLiteral(":/theme-light.qss"));
        if (light.open(QIODevice::ReadOnly)) {
            style += QString::fromUtf8(light.readAll());
        }
    }
    qApp->setStyleSheet(style);
    if (languageMenu) {
        languageMenu->setIcon(globeIcon(dark));
    }
}

void MainWindow::loadSettings()
{
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        const auto key = QStringLiteral("bands/%1/").arg(i + 1);
        const FrequencyBand stored{
            settings.value(key + QStringLiteral("lower"), bands[i].lower).toDouble(),
            settings.value(key + QStringLiteral("upper"), bands[i].upper).toDouble()};
        if (stored.valid() && stored.upper <= maximumMHz) {
            bands[i] = stored;
        }
    }
    currentBand = std::clamp(settings.value(QStringLiteral("bands/selected"), 3).toInt(),
                             0, static_cast<int>(bands.size()) - 1);
    sweepStart->setValue(settings.value(QStringLiteral("sweep/startMHz"), 100.0).toDouble());
    sweepStop->setValue(settings.value(QStringLiteral("sweep/stopMHz"), 1000.0).toDouble());
    sweepDuration->setValue(settings.value(QStringLiteral("sweep/durationSeconds"), 20).toInt());
    repeat->setChecked(settings.value(QStringLiteral("sweep/repeat"), false).toBool());
    target.elektor = settings.value(QStringLiteral("gpib/elektor"), false).toBool();
    target.board = std::clamp(settings.value(QStringLiteral("gpib/board"), 0).toInt(), 0, 99);
    target.address = std::clamp(settings.value(QStringLiteral("gpib/address"), 6).toInt(), 0, 30);
    target.serialPort = settings.value(QStringLiteral("gpib/serial")).toString();

    updateBandDisplay();
    displayFrequency(settings.value(QStringLiteral("frequencyMHz"), bands[currentBand].lower).toDouble());
}

void MainWindow::saveSettings()
{
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        const auto key = QStringLiteral("bands/%1/").arg(i + 1);
        settings.setValue(key + QStringLiteral("lower"), bands[i].lower);
        settings.setValue(key + QStringLiteral("upper"), bands[i].upper);
    }
    settings.setValue(QStringLiteral("bands/selected"), currentBand);
    settings.setValue(QStringLiteral("frequencyMHz"), frequency->value());
    settings.setValue(QStringLiteral("sweep/startMHz"), sweepStart->value());
    settings.setValue(QStringLiteral("sweep/stopMHz"), sweepStop->value());
    settings.setValue(QStringLiteral("sweep/durationSeconds"), sweepDuration->value());
    settings.setValue(QStringLiteral("sweep/repeat"), repeat->isChecked());
    settings.setValue(QStringLiteral("gpib/elektor"), target.elektor);
    settings.setValue(QStringLiteral("gpib/board"), target.board);
    settings.setValue(QStringLiteral("gpib/address"), target.address);
    settings.setValue(QStringLiteral("gpib/serial"), target.serialPort);
    settings.setValue(QStringLiteral("appearance/dark"), darkTheme);
    settings.sync();
    setMessage([saved = settings.status() == QSettings::NoError] {
        return saved ? tr("Einstellungen gespeichert.") : tr("Die Einstellungen konnten nicht gespeichert werden.");
    });
}

void MainWindow::editBandLimits()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Bandgrenzen bearbeiten"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    grid->addWidget(label(tr("Untergrenze"), "muted"), 0, 1);
    grid->addWidget(label(tr("Obergrenze"), "muted"), 0, 2);
    std::array<QDoubleSpinBox *, 4> lower{};
    std::array<QDoubleSpinBox *, 4> upper{};
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        lower[i] = frequencyInput();
        lower[i]->setValue(bands[i].lower);
        lower[i]->setAccessibleName(tr("Band %1, Untergrenze").arg(i + 1));
        upper[i] = frequencyInput();
        upper[i]->setValue(bands[i].upper);
        upper[i]->setAccessibleName(tr("Band %1, Obergrenze").arg(i + 1));
        grid->addWidget(new QLabel(tr("Band %1").arg(i + 1)), i + 1, 0);
        grid->addWidget(lower[i], i + 1, 1);
        grid->addWidget(upper[i], i + 1, 2);
    }
    layout->addLayout(grid);
    auto *error = new QLabel;
    error->setObjectName(QStringLiteral("bandValidationError"));
    error->setWordWrap(true);
    error->hide();
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox;
    buttons->addButton(tr("Abbrechen"), QDialogButtonBox::RejectRole);
    auto *accept = buttons->addButton(tr("Übernehmen"), QDialogButtonBox::AcceptRole);
    accept->setProperty("primary", true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto edited = bands;
        for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
            edited[i] = {lower[i]->value(), upper[i]->value()};
            if (!edited[i].valid()) {
                error->setText(tr("In Band %1 muss die Obergrenze größer sein.").arg(i + 1));
                error->show();
                upper[i]->setFocus();
                return;
            }
        }
        const auto previous = bands[currentBand];
        const auto next = edited[currentBand];
        bands = edited;
        updateBandDisplay();
        if (previous.lower != next.lower || previous.upper != next.upper) {
            applyFrequency();
        }
        if (!tuning) {
            setMessage([] { return tr("Bandgrenzen übernommen."); });
        }
        dialog.accept();
    });
    dialog.exec();
}

void MainWindow::selectBand(int band)
{
    if (band != currentBand) {
        currentBand = band;
        updateBandDisplay();
        applyFrequency();
    }
}

void MainWindow::updateBandDisplay()
{
    for (int i = 0; i < static_cast<int>(bands.size()); ++i) {
        bandRanges[i]->setText(QStringLiteral("%1 – %2")
                               .arg(compactFrequency(bands[i].lower), compactFrequency(bands[i].upper)));
    }
    bandButtons[currentBand]->setChecked(true);
    const auto band = bands[currentBand];
    lowFrequency->setText(compactFrequency(band.lower));
    midFrequency->setText(compactFrequency(band.lower + (band.upper - band.lower) / 2.0));
    highFrequency->setText(compactFrequency(band.upper));
    const QSignalBlocker block(frequency);
    frequency->setRange(band.lower, band.upper);
    displayFrequency(frequency->value());
}

void MainWindow::displayFrequency(double mhz)
{
    const auto band = bands[currentBand];
    const auto target = std::clamp(mhz, band.lower, band.upper);
    const QSignalBlocker blockSlider(slider);
    const QSignalBlocker blockFrequency(frequency);
    slider->setValue(qRound(sliderSteps * (target - band.lower) / (band.upper - band.lower)));
    frequency->setValue(target);
    frequencyValue->setText(numericLocale().toString(target, 'f', 3));
}

void MainWindow::updateControls()
{
    const bool connected = connection == Connection::Connected;
    const bool transitioning = connection == Connection::Connecting || connection == Connection::Disconnecting;
    const bool sweeping = sweepState != SweepState::Idle;
    const bool canTune = !sweeping && !transitioning;
    const bool adjustable = canTune && !tuning;
    switch (connection) {
    case Connection::Disconnected: connectionState->setText(tr("Getrennt")); break;
    case Connection::Connecting: connectionState->setText(tr("Verbindet …")); break;
    case Connection::Connected: connectionState->setText(tr("Verbunden")); break;
    case Connection::Disconnecting: connectionState->setText(tr("Trennt …")); break;
    case Connection::Failed: connectionState->setText(tr("Verbindungsfehler")); break;
    }
    connectionState->setProperty("connected", connected);
    connectionState->setProperty("error", connection == Connection::Failed);
    refreshStyle(connectionState);
    connectButton->setText(connected ? tr("Trennen") : tr("Verbinden"));
    connectButton->setProperty("primary", !connected);
    refreshStyle(connectButton);
    connectButton->setEnabled(!transitioning);
    connectionSettings->setEnabled(!connected && !transitioning);
    slider->setEnabled(canTune);
    frequency->setEnabled(canTune);
    apply->setEnabled(connected && canTune);
    editBands->setEnabled(adjustable);
    for (auto *button : bandButtons) {
        button->setEnabled(adjustable);
    }
    sweepStart->setEnabled(adjustable);
    sweepStop->setEnabled(adjustable);
    sweepDuration->setEnabled(adjustable);
    repeat->setEnabled(adjustable);
    sweepAction->setText(sweepState == SweepState::Stopping ? tr("Stoppt …")
                         : sweeping ? tr("Sweep stoppen") : tr("Sweep starten"));
    sweepAction->setProperty("primary", !sweeping);
    sweepAction->setProperty("stopping", sweeping);
    refreshStyle(sweepAction);
    sweepAction->setEnabled(connected && !tuning && sweepState != SweepState::Stopping);
}

void MainWindow::toggleConnection()
{
    if (connection == Connection::Connected) {
        connection = Connection::Disconnecting;
        updateControls();
        QMetaObject::invokeMethod(instrument, &Hp8620::disconnectDevice, Qt::QueuedConnection);
        return;
    }

    if (target.elektor && target.serialPort.isEmpty()) {
        setMessage([] { return tr("COM-Port unter Einstellungen → Verbindung auswählen."); });
        return;
    }
    connection = Connection::Connecting;
    setMessage();
    updateControls();
    QMetaObject::invokeMethod(instrument, [worker = instrument, target = target, band = currentBand] {
        worker->connectDevice(target, band);
    }, Qt::QueuedConnection);
}

void MainWindow::applyFrequency()
{
    frequency->interpretText();
    if (connection != Connection::Connected || sweepState != SweepState::Idle) {
        return;
    }
    if (tuning) {
        pendingFrequency = frequency->value();
        return;
    }
    queueFrequency(frequency->value());
}

void MainWindow::queueFrequency(double mhz)
{
    const auto band = bands[currentBand];
    tuning = true;
    updateControls();
    QMetaObject::invokeMethod(instrument, [worker = instrument, index = currentBand, band, mhz] {
        worker->applyFrequency(index, band, mhz);
    }, Qt::QueuedConnection);
}

void MainWindow::toggleSweep()
{
    if (sweepState == SweepState::Running) {
        sweepState = SweepState::Stopping;
        updateControls();
        QMetaObject::invokeMethod(instrument, &Hp8620::stopSweep, Qt::QueuedConnection);
        return;
    }

    const auto band = bands[currentBand];
    sweepStart->interpretText();
    sweepStop->interpretText();
    sweepDuration->interpretText();
    const FrequencySweep sweep{sweepStart->value(), sweepStop->value(), sweepDuration->value(), repeat->isChecked()};
    if (!band.contains(sweep.start) || !band.contains(sweep.stop)) {
        setMessage([index = currentBand + 1, band] {
            return tr("Start und Stopp müssen in Band %1 liegen (%2 bis %3).")
                .arg(index).arg(compactFrequency(band.lower), compactFrequency(band.upper));
        });
        (band.contains(sweep.start) ? sweepStop : sweepStart)->setFocus();
        return;
    }
    if (sweep.start == sweep.stop) {
        setMessage([] { return tr("Für einen Sweep müssen sich Start- und Stoppfrequenz unterscheiden."); });
        sweepStop->setFocus();
        return;
    }
    sweepState = SweepState::Running;
    progress->setValue(0);
    setSweepStatus(QT_TR_NOOP("Startet …"));
    setMessage();
    updateControls();
    QMetaObject::invokeMethod(instrument, [worker = instrument, index = currentBand, band, sweep] {
        worker->startSweep(index, band, sweep);
    }, Qt::QueuedConnection);
}
