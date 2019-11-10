#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <stdlib.h>
#include <regex>
#include <thread>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <signal.h>
#include <algorithm>

#include <QMainWindow>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QTreeView>
#include <QListWidgetItem>
#include <QCloseEvent>
#include <QMessageBox>
#include <QStandardPaths>
#include <QInputDialog>
#include <QLineEdit>
#include <QTimer>

#include <qhotkey.h>
#include <json.hpp>
#include <soundplayback.h>
#include <settings.h>
#include <sethotkeydialog.h>
#include <clickablesliderstyle.h>
#include <soundlistwidgetitem.h>

class SoundPlayback;
class SettingsDialog;

using namespace std;
using json = nlohmann::json;

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    SoundPlayback *soundPlayback;
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void closeEvent(QCloseEvent *event);

private:
    Ui::MainWindow *ui;
    SettingsDialog *settingsDialog;

    void clearSoundFiles();
    void saveSoundFiles();
    void loadSoundFiles();
    QListWidget *getActiveView();
    SoundListWidgetItem *getSelectedItem();
    QListWidget *createTab(QString title);
    void addSoundToView(QFile &file, QListWidget *widget);
    void syncVolume(bool remote);
    void registerHotkey(SoundListWidgetItem* it, QString keys);
    void unregisterHotkey(SoundListWidgetItem* it);

private slots:
    void on_addTabButton_clicked();
    void on_soundsListWidget_itemDoubleClicked(QListWidgetItem *listWidgetItem);
    void on_refreshAppsButton_clicked();
    void on_stopButton_clicked();
    void on_addSoundButton_clicked();
    void on_removeSoundButton_clicked();
    void on_clearSoundsButton_clicked();
    void on_playSoundButton_clicked();
    void on_setHotkeyButton_clicked();
    void on_tabWidget_tabCloseRequested(int index);
    void on_tabWidget_tabBarDoubleClicked(int index);
    void on_addFolderButton_clicked();
    void on_localVolumeSlider_valueChanged(int value);
    void on_remoteVolumeSlider_valueChanged(int value);
    void on_settingsButton_clicked();
};
#endif // MAINWINDOW_H
