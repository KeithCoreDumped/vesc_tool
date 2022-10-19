/*
    Copyright 2019 Benjamin Vedder	benjamin@vedder.se

    This file is part of VESC Tool.

    VESC Tool is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    VESC Tool is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */


#include "pageloganalysis.h"
#include "ui_pageloganalysis.h"
#include "utility.h"
#include <QFileDialog>
#include <cmath>
#include <QStandardPaths>

PageLogAnalysis::PageLogAnalysis(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PageLogAnalysis)
{
    ui->setupUi(this);
    mVesc = nullptr;

    resetInds();

    QString theme = Utility::getThemePath();
    ui->centerButton->setIcon(QPixmap(theme + "icons/icons8-target-96.png"));
    ui->playButton->setIcon(QPixmap(theme + "icons/Circled Play-96.png"));
    ui->logListRefreshButton->setIcon(QPixmap(theme + "icons/Refresh-96.png"));
    ui->logListOpenButton->setIcon(QPixmap(theme + "icons/Open Folder-96.png"));
    ui->openCurrentButton->setIcon(QPixmap(theme + "icons/Open Folder-96.png"));
    ui->openCsvButton->setIcon(QPixmap(theme + "icons/Open Folder-96.png"));
    ui->savePlotPdfButton->setIcon(QPixmap(theme + "icons/Line Chart-96.png"));
    ui->savePlotPngButton->setIcon(QPixmap(theme + "icons/Line Chart-96.png"));
    ui->saveMapPdfButton->setIcon(QPixmap(theme + "icons/Waypoint Map-96.png"));
    ui->saveMapPngButton->setIcon(QPixmap(theme + "icons/Waypoint Map-96.png"));
    ui->vescLogListRefreshButton->setIcon(QPixmap(theme + "icons/Refresh-96.png"));
    ui->vescLogListOpenButton->setIcon(QPixmap(theme + "icons/Open Folder-96.png"));
    ui->vescUpButton->setIcon(QPixmap(theme + "icons/Upload-96.png"));

    updateTileServers();

    ui->spanSlider->setMinimum(0);
    ui->spanSlider->setMaximum(10000);
    ui->spanSlider->setValue(10000);

    ui->mapSplitter->setStretchFactor(0, 2);
    ui->mapSplitter->setStretchFactor(1, 1);

    ui->statSplitter->setStretchFactor(0, 6);
    ui->statSplitter->setStretchFactor(1, 1);

    Utility::setPlotColors(ui->plot);
    ui->plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->plot->axisRect()->setRangeZoom(Qt::Orientations());
    ui->plot->axisRect()->setRangeDrag(Qt::Orientations());

    ui->dataTable->setColumnWidth(0, 140);
    ui->dataTable->setColumnWidth(1, 120);
    ui->statTable->setColumnWidth(0, 140);
    ui->logTable->setColumnWidth(0, 250);
    ui->vescLogTable->setColumnWidth(0, 250);

    m3dView = new Vesc3DView(this);
    m3dView->setMinimumWidth(200);
    m3dView->setRollPitchYaw(20, 20, 0);
    m3dView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    mUseYawBox = new QCheckBox("Use Yaw (will drift)");
    mUseYawBox->setChecked(true);
    ui->tab_3->layout()->addWidget(mUseYawBox);
    ui->tab_3->layout()->addWidget(m3dView);

    mPlayTimer = new QTimer(this);
    mPlayPosNow = 0.0;
    mPlayTimer->start(100);

    connect(mPlayTimer, &QTimer::timeout, [this]() {
        if (ui->playButton->isChecked() && !mLogTruncated.isEmpty()) {
            mPlayPosNow += double(mPlayTimer->interval()) / 1000.0;

            if (mInd_t_day >= 0) {
                double time = (mLogTruncated.last()[mInd_t_day]->value - mLogTruncated.first()[mInd_t_day]->value);
                if (time < 0.0) { // Handle midnight
                    time += 60.0 * 60.0 * 24.0;
                }

                if (mLogTruncated.size() > 0 &&
                        mPlayPosNow <= time) {
                    updateDataAndPlot(mPlayPosNow);
                } else {
                    ui->playButton->setChecked(false);
                }
            }
        }
    });

    QFont legendFont = font();
    legendFont.setPointSize(9);

    ui->plot->legend->setVisible(true);
    ui->plot->legend->setFont(legendFont);
    ui->plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignRight|Qt::AlignBottom);
    ui->plot->xAxis->setLabel("Seconds (s)");

    mVerticalLine = new QCPCurve(ui->plot->xAxis, ui->plot->yAxis);
    mVerticalLine->removeFromLegend();
    mVerticalLine->setPen(QPen(Utility::getAppQColor("normalText")));
    mVerticalLineMsLast = -1;

    auto updateMouse = [this](QMouseEvent *event) {
        if (event->modifiers() == Qt::ShiftModifier) {
            ui->plot->axisRect()->setRangeZoom(Qt::Vertical);
            ui->plot->axisRect()->setRangeDrag(Qt::Vertical);
        } else {
            ui->plot->axisRect()->setRangeZoom(Qt::Orientations());
            ui->plot->axisRect()->setRangeDrag(Qt::Orientations());
        }

        if (event->buttons() & Qt::LeftButton) {
            double vx = ui->plot->xAxis->pixelToCoord(event->x());
            updateDataAndPlot(vx);
        }
    };

    connect(ui->map, &MapWidget::infoPointClicked, [this](LocPoint info) {
        if (mInd_t_day >= 0 && !mLogTruncated.isEmpty()) {
            updateDataAndPlot(info.getInfo().toDouble() - mLogTruncated.first()[mInd_t_day]->value);
        }
    });

    connect(ui->plot, &QCustomPlot::mousePress, [updateMouse](QMouseEvent *event) {
        updateMouse(event);
    });

    connect(ui->plot, &QCustomPlot::mouseMove, [updateMouse](QMouseEvent *event) {
        updateMouse(event);
    });

    connect(ui->plot, &QCustomPlot::mouseWheel, [this](QWheelEvent *event) {
        if (event->modifiers() == Qt::ShiftModifier) {
            ui->plot->axisRect()->setRangeZoom(Qt::Vertical);
            ui->plot->axisRect()->setRangeDrag(Qt::Vertical);
        } else {
            ui->plot->axisRect()->setRangeZoom(Qt::Orientations());
            ui->plot->axisRect()->setRangeDrag(Qt::Orientations());

            double upper = ui->plot->xAxis->range().upper;
            double progress = ui->plot->xAxis->pixelToCoord(event->x()) / upper;
            double diff = event->angleDelta().y();
            double d1 = diff * progress;
            double d2 = diff * (1.0 - progress);

            ui->spanSlider->alt_setValue(ui->spanSlider->alt_value() + int(d1));
            ui->spanSlider->setValue(ui->spanSlider->value() - int(d2));
        }
    });

    connect(ui->spanSlider, &SuperSlider::alt_valueChanged, [this](int value) {
        if (value >= ui->spanSlider->value()) {
            ui->spanSlider->setValue(value);
        }
        truncateDataAndPlot(ui->autoZoomBox->isChecked());
    });

    connect(ui->spanSlider, &SuperSlider::valueChanged, [this](int value) {
        if (value <= ui->spanSlider->alt_value()) {
            ui->spanSlider->alt_setValue(value);
        }
        truncateDataAndPlot(ui->autoZoomBox->isChecked());
    });

    connect(ui->dataTable, &QTableWidget::itemSelectionChanged, [this]() {
        updateGraphs();
    });

    connect(ui->filterOutlierBox, &QGroupBox::toggled, [this]() {
        truncateDataAndPlot(ui->autoZoomBox->isChecked());
    });

    connect(ui->filterhAccBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double newVal) {
        (void)newVal;
        if (ui->filterOutlierBox->isChecked()) {
            truncateDataAndPlot(ui->autoZoomBox->isChecked());
        }
    });

    connect(ui->tabWidget, &QTabWidget::currentChanged, [this](int index) {
        (void)index;
        logListRefresh();
    });

    on_gridBox_toggled(ui->gridBox->isChecked());
}

PageLogAnalysis::~PageLogAnalysis()
{
    delete ui;
}

VescInterface *PageLogAnalysis::vesc() const
{
    return mVesc;
}

void PageLogAnalysis::setVesc(VescInterface *vesc)
{
    mVesc = vesc;

    if (mVesc) {
        connect(mVesc->commands(), &Commands::fileProgress, [this]
                (int32_t prog, int32_t tot, double percentage, double bytesPerSec) {
            (void)prog;
            (void)tot;
            ui->vescDisplay->setValue(percentage);
            ui->vescDisplay->setText(tr("Speed: %1 KB/s").arg(bytesPerSec / 1024, 0, 'f', 2));
        });
    }
}

void PageLogAnalysis::loadVescLog(QVector<LOG_DATA> log)
{
    resetInds();

    for (auto e: mLog) {
        for (auto e2: e) {
            delete e2;
        }
    }

    mLog.clear();
    mLogTruncated.clear();

    double i_llh[3];
    for (auto d: log) {
        if (d.posTime >= 0 && (!ui->filterOutlierBox->isChecked() ||
                               d.hAcc < ui->filterhAccBox->value())) {
            i_llh[0] = d.lat;
            i_llh[1] = d.lon;
            i_llh[2] = d.alt;
            ui->map->setEnuRef(i_llh[0], i_llh[1], i_llh[2]);
            break;
        }
    }

    LOG_DATA prevSampleGnss;
    bool prevSampleGnssSet = false;
    double metersGnss = 0.0;

    for (auto d: log) {
        if (d.posTime >= 0 &&
                (!ui->filterOutlierBox->isChecked() ||
                 d.hAcc < ui->filterhAccBox->value())) {
            if (prevSampleGnssSet) {
                double i_llh[3];
                double llh[3];
                double xyz[3];
                ui->map->getEnuRef(i_llh);

                llh[0] = d.lat;
                llh[1] = d.lon;
                llh[2] = d.alt;
                Utility::llhToEnu(i_llh, llh, xyz);

                LocPoint p, p2;
                p.setXY(xyz[0], xyz[1]);
                p.setRadius(10);

                llh[0] = prevSampleGnss.lat;
                llh[1] = prevSampleGnss.lon;
                llh[2] = prevSampleGnss.alt;
                Utility::llhToEnu(i_llh, llh, xyz);

                p2.setXY(xyz[0], xyz[1]);
                p2.setRadius(10);

                metersGnss += p.getDistanceTo(p2);
            }

            prevSampleGnssSet = true;
            prevSampleGnss = d;
        }

        // Todo: There is a lot of duplication here, which leads to high memory usage for large logs. A log with
        // 30000 samples (10 MB file size) takes around 500 MB RAM to load.
        QVector<LOG_ENTRY*> e;
        e.append(new LOG_ENTRY("kmh_vesc", "Speed VESC", "km/h", d.setupValues.speed * 3.6));
        e.append(new LOG_ENTRY("kmh_gnss", "Speed GNSS", "km/h", d.gVel * 3.6));
        e.append(new LOG_ENTRY("t_day", "Time", "s", double(d.valTime) / 1000.0, 0, "", false, true, false));
        e.append(new LOG_ENTRY("t_day_pos", "Time GNSS", "", double(d.posTime) / 1000.0, 0, "", false, true, false));
        e.append(new LOG_ENTRY("t_trip", "Time of trip", "", double(d.valTime) / 1000.0, 0, "", true, true, false));
        e.append(new LOG_ENTRY("trip_vesc", "Trip VESC", "m", d.setupValues.tachometer, 3, "", true));
        e.append(new LOG_ENTRY("trip_vesc_abs", "Trip VESC ABS", "m", d.setupValues.tachometer, 3, "", true));
        e.append(new LOG_ENTRY("trip_gnss", "Trip GNSS", "m", metersGnss, 3, "", false));
        e.append(new LOG_ENTRY("setup_curr_motor", "Current Motors", "A", d.setupValues.current_motor));
        e.append(new LOG_ENTRY("setup_curr_battery", "Current Battery", "A", d.setupValues.current_in));
        e.append(new LOG_ENTRY("setup_power", "Power", "W", d.setupValues.current_in * d.values.v_in, 0));
        e.append(new LOG_ENTRY("erpm", "ERPM", "1/1000", d.values.rpm / 1000, 0));
        e.append(new LOG_ENTRY("duty", "Duty", "%", d.values.duty_now * 100, 1));
        e.append(new LOG_ENTRY("fault", "Fault Code", "", d.values.fault_code,
                           0, Commands::faultToStr(mc_fault_code(d.values.fault_code)).mid(11)));
        e.append(new LOG_ENTRY("v_in", "Input Voltage", "V", d.values.v_in));
        e.append(new LOG_ENTRY("soc", "Battery Level", "%", d.setupValues.battery_level * 100.0, 1));
        e.append(new LOG_ENTRY("t_mosfet", "Temp MOSFET", "°C", d.values.temp_mos, 1));
        e.append(new LOG_ENTRY("t_motor", "Temp Motor", "°C", d.values.temp_motor, 1));
        e.append(new LOG_ENTRY("cnt_ah", "Ah Used", "Ah", d.setupValues.amp_hours, 3));
        e.append(new LOG_ENTRY("cnt_ah_chg", "Ah Charged", "Ah", d.setupValues.amp_hours_charged, 3));
        e.append(new LOG_ENTRY("cnt_wh", "Wh Used", "Wh", d.setupValues.watt_hours, 3));
        e.append(new LOG_ENTRY("cnt_wh_chg", "Wh Charged", "Wh", d.setupValues.watt_hours_charged, 3));
        e.append(new LOG_ENTRY("id", "id", "A", d.values.id));
        e.append(new LOG_ENTRY("iq", "iq", "A", d.values.iq));
        e.append(new LOG_ENTRY("vd", "vd", "V", d.values.vd));
        e.append(new LOG_ENTRY("vq", "vq", "V", d.values.vq));
        e.append(new LOG_ENTRY("t_mosfet_1", "Temp MOSFET 1", "°C", d.values.temp_mos_1, 1));
        e.append(new LOG_ENTRY("t_mosfet_2", "Temp MOSFET 2", "°C", d.values.temp_mos_2, 1));
        e.append(new LOG_ENTRY("t_mosfet_3", "Temp MOSFET 3", "°C", d.values.temp_mos_3, 1));
        e.append(new LOG_ENTRY("position", "Motor Pos", "°", d.values.position, 1));
        e.append(new LOG_ENTRY("roll", "Roll", "°", d.imuValues.roll, 1));
        e.append(new LOG_ENTRY("pitch", "Pitch", "°", d.imuValues.pitch, 1));
        e.append(new LOG_ENTRY("yaw", "Yaw", "°", d.imuValues.yaw, 1));
        e.append(new LOG_ENTRY("acc_x", "Accel X", "G", d.imuValues.accX));
        e.append(new LOG_ENTRY("acc_y", "Accel Y", "G", d.imuValues.accY));
        e.append(new LOG_ENTRY("acc_z", "Accel Z", "G", d.imuValues.accZ));
        e.append(new LOG_ENTRY("gyro_x", "Gyro X", "°/s", d.imuValues.gyroX, 1));
        e.append(new LOG_ENTRY("gyro_y", "Gyro Y", "°/s", d.imuValues.gyroY, 1));
        e.append(new LOG_ENTRY("gyro_z", "Gyro Z", "°/s", d.imuValues.gyroZ, 1));
        e.append(new LOG_ENTRY("v1_curr_motor", "V1 Current", "A", d.values.current_motor));
        e.append(new LOG_ENTRY("v1_curr_battery", "V1 Current Battery", "A", d.values.current_in));
        e.append(new LOG_ENTRY("v1_cnt_ah", "V1 Ah Used", "Ah", d.values.amp_hours, 3));
        e.append(new LOG_ENTRY("v1_cnt_ah_chg", "V1 Ah Charged", "Ah", d.values.amp_hours_charged, 3));
        e.append(new LOG_ENTRY("v1_cnt_wh", "V1 Wh Used", "Wh", d.values.watt_hours, 3));
        e.append(new LOG_ENTRY("v1_cnt_wh_chg", "V1 Wh Charged", "Wh", d.values.watt_hours_charged, 3));
        e.append(new LOG_ENTRY("gnss_lat", "Latitude", "°", d.lat, 6));
        e.append(new LOG_ENTRY("gnss_lon", "Longitude", "°", d.lon, 6));
        e.append(new LOG_ENTRY("gnss_alt", "Altitude", "m", d.alt));
        e.append(new LOG_ENTRY("gnss_v_vel", "V. Speed GNSS", "km/h", d.vVel * 3.6));
        e.append(new LOG_ENTRY("gnss_h_acc", "H. Accuracy GNSS", "m", d.hAcc));
        e.append(new LOG_ENTRY("gnss_v_acc", "V. Accuracy GNSS", "m", d.vAcc));
        e.append(new LOG_ENTRY("num_vesc", "VESC num", "", d.setupValues.num_vescs, 0));

        mLog.append(e);
    }

    updateInds();

    ui->dataTable->setRowCount(0);

    if (mLog.size() == 0) {
        return;
    }

    auto addDataItem = [this](QString name, bool hasScale = true,
            double scaleStep = 0.1, double scaleMax = 99.99) {
        ui->dataTable->setRowCount(ui->dataTable->rowCount() + 1);
        auto item1 = new QTableWidgetItem(name);
        ui->dataTable->setItem(ui->dataTable->rowCount() - 1, 0, item1);
        ui->dataTable->setItem(ui->dataTable->rowCount() - 1, 1, new QTableWidgetItem(""));
        if (hasScale) {
            QDoubleSpinBox *sb = new QDoubleSpinBox;
            sb->setSingleStep(scaleStep);
            sb->setValue(1.0);
            sb->setMaximum(scaleMax);
            // Prevent mouse wheel focus to avoid changing the selection
            sb->setFocusPolicy(Qt::StrongFocus);
            ui->dataTable->setCellWidget(ui->dataTable->rowCount() - 1, 2, sb);
            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    [this](double value) {
                (void)value;
                updateGraphs();
            });
        } else {
            ui->dataTable->setItem(ui->dataTable->rowCount() - 1, 2, new QTableWidgetItem("Not Plottable"));
        }
    };

    for (auto e: mLog.first()) {
        addDataItem(e->name, e->hasScale, e->scaleStep, e->scaleMax);
    }

    truncateDataAndPlot();
}

void PageLogAnalysis::on_openCsvButton_clicked()
{
    if (mVesc) {
        QString fileName = QFileDialog::getOpenFileName(this,
                                                        tr("Load CSV File"), "",
                                                        tr("CSV files (*.csv)"));

        if (!fileName.isEmpty()) {            
            QSettings set;
            set.setValue("pageloganalysis/lastdir",
                         QFileInfo(fileName).absolutePath());

            if (mVesc->loadRtLogFile(fileName)) {
                on_openCurrentButton_clicked();
            }

            logListRefresh();
        }
    }
}

void PageLogAnalysis::on_openCurrentButton_clicked()
{
    if (mVesc) {
        loadVescLog(mVesc->getRtLogData());
    }
}

void PageLogAnalysis::on_gridBox_toggled(bool checked)
{
    ui->map->setDrawGrid(checked);
}

void PageLogAnalysis::on_tilesHiResButton_toggled(bool checked)
{
    (void)checked;
    updateTileServers();
    ui->map->update();
}

void PageLogAnalysis::on_tilesOsmButton_toggled(bool checked)
{
    (void)checked;
    updateTileServers();
    ui->map->update();
}

void PageLogAnalysis::truncateDataAndPlot(bool zoomGraph)
{
    double start = double(ui->spanSlider->alt_value()) / 10000.0;
    double end = double(ui->spanSlider->value()) / 10000.0;

    ui->map->setInfoTraceNow(0);
    ui->map->clearAllInfoTraces();

    int ind = 0;
    double i_llh[3];
    int posTimeLast = -1;

    ui->map->getEnuRef(i_llh);
    mLogTruncated.clear();

    for (const auto &d: mLog) {
        ind++;
        double prop = double(ind) / double(mLog.size());
        if (prop < start || prop > end) {
            continue;
        }

        mLogTruncated.append(d);
        bool skip = false;

        if (mInd_t_day_pos >= 0 && mInd_gnss_h_acc >= 0) {
            int postime = int(d[mInd_t_day_pos]->value * 1000.0);
            double h_acc = d[mInd_gnss_h_acc]->value;

            skip = true;
            if (postime >= 0 &&
                    (!ui->filterOutlierBox->isChecked() ||
                     h_acc < ui->filterhAccBox->value()) &&
                    posTimeLast != postime) {
                skip = false;
                posTimeLast = postime;
            }
        }

        if (mInd_gnss_lat < 0 || mInd_gnss_lon < 0) {
            skip = true;
        }

        if (!skip) {
            double llh[3];
            double xyz[3];

            llh[0] = d[mInd_gnss_lat]->value;
            llh[1] = d[mInd_gnss_lon]->value;
            if (mInd_gnss_alt >= 0) {
                llh[2] = d[mInd_gnss_alt]->value;
            } else {
                llh[2] = 0.0;
            }
            Utility::llhToEnu(i_llh, llh, xyz);

            LocPoint p;
            p.setXY(xyz[0], xyz[1]);
            p.setRadius(5);

            if (mInd_t_day >= 0) {
                p.setInfo(QString("%1").arg(d[mInd_t_day]->value));
            }

            ui->map->addInfoPoint(p, false);
        }
    }

    if (zoomGraph) {
        ui->map->zoomInOnInfoTrace(-1, 0.1);
    }

    ui->map->update();
    updateGraphs();
    updateStats();
}

void PageLogAnalysis::updateGraphs()
{
    auto rows = ui->dataTable->selectionModel()->selectedRows();

    QVector<double> xAxis;
    QVector<QVector<double> > yAxes;
    QVector<QString> names;

    double startTime = -1.0;
    double verticalTime = -1.0;
    LocPoint p, p2;

    int timeMs = 0;
    for (const auto &d: mLogTruncated) {
        if (mInd_t_day >= 0) {
            if (startTime < 0) {
                startTime = d[mInd_t_day]->value;
            }

            timeMs = int((d[mInd_t_day]->value - startTime) * 1000.0);
            if (timeMs < 0) { // Handle midnight
                timeMs += 60 * 60 * 24 * 1000;
            }
        } else {
            timeMs += 1000.0;
        }

        xAxis.append(double(timeMs) / 1000.0);
        int rowInd = 0;

        for (int r = 0;r < rows.size();r++) {
            int row = rows.at(r).row();
            double rowScale = 1.0;
            if(QDoubleSpinBox *sb = qobject_cast<QDoubleSpinBox*>
                    (ui->dataTable->cellWidget(row, 2))) {
                rowScale = sb->value();
            }

            auto entry = d[row];

            if (entry->hasScale) {
                if (yAxes.size() <= rowInd) yAxes.append(QVector<double>());
                yAxes[rowInd].append(entry->value * rowScale);
                names.append(QString("%1 (%2 * %3)").arg(entry->name).
                             arg(entry->unit).arg(rowScale));
                rowInd++;
            }
        }
    }

    ui->plot->clearGraphs();

    for (int i = 0;i < yAxes.size();i++) {
        QPen pen = QPen(Utility::getAppQColor("plot_graph1"));

        if (i == 1) {
            pen = QPen(Qt::magenta);
        } else if (i == 2) {
            pen = QPen(Utility::getAppQColor("plot_graph2"));
        } else if (i == 3) {
            pen = QPen(Utility::getAppQColor("plot_graph3"));
        } else if (i == 4) {
            pen = QPen(Qt::cyan);
        } else if (i == 5) {
            pen = QPen(Utility::getAppQColor("plot_graph4"));
        }

        ui->plot->addGraph();
        ui->plot->graph(i)->setPen(pen);
        ui->plot->graph(i)->setName(names.at(i));
        ui->plot->graph(i)->setData(xAxis, yAxes.at(i));
    }

    mVerticalLine->setVisible(false);

    if (yAxes.size() > 0) {
        ui->plot->rescaleAxes(true);
    } else if (xAxis.size() >= 2) {
        ui->plot->xAxis->setRangeLower(xAxis.first());
        ui->plot->xAxis->setRangeUpper(xAxis.last());
    }

    if (verticalTime >= 0) {
        QVector<double> x(2) , y(2);
        x[0] = verticalTime; y[0] = ui->plot->yAxis->range().lower;
        x[1] = verticalTime; y[1] = ui->plot->yAxis->range().upper;
        mVerticalLine->setData(x, y);
        mVerticalLine->setVisible(true);
    }

    ui->plot->replotWhenVisible();
}

void PageLogAnalysis::updateStats()
{
    if (mLogTruncated.size() < 2) {
            return;
    }

    auto startSample = mLogTruncated.first();
    auto endSample = mLogTruncated.last();

    int samples = mLogTruncated.size();
    int timeTotMs = 0;

    if (samples < 2) {
        return;
    }

    if (mInd_t_day >= 0) {
        timeTotMs = (endSample[mInd_t_day]->value - startSample[mInd_t_day]->value) * 1000.0;
        if (timeTotMs < 0) { // Handle midnight
            timeTotMs += 60 * 60 * 24 * 1000;
        }
    }

    double meters = 0.0;
    double metersAbs = 0.0;
    double metersGnss = 0.0;
    double wh = 0.0;
    double whCharge = 0.0;
    double ah = 0.0;
    double ahCharge = 0.0;

    if (mInd_trip_vesc >= 0) {
        meters = endSample[mInd_trip_vesc]->value - startSample[mInd_trip_vesc]->value;
    }

    if (mInd_trip_vesc_abs >= 0) {
        metersAbs = endSample[mInd_trip_vesc_abs]->value - startSample[mInd_trip_vesc_abs]->value;
    }

    if (mInd_trip_gnss >= 0) {
        metersGnss = endSample[mInd_trip_gnss]->value - startSample[mInd_trip_gnss]->value;
    }

    if (mInd_cnt_wh >= 0) {
        wh = endSample[mInd_cnt_wh]->value - startSample[mInd_cnt_wh]->value;
    }

    if (mInd_cnt_wh_chg >= 0) {
        whCharge = endSample[mInd_cnt_wh_chg]->value - startSample[mInd_cnt_wh_chg]->value;
    }

    if (mInd_cnt_ah >= 0) {
        ah = endSample[mInd_cnt_ah]->value - startSample[mInd_cnt_ah]->value;
    }

    if (mInd_cnt_ah_chg >= 0) {
        ahCharge = endSample[mInd_cnt_ah_chg]->value - startSample[mInd_cnt_ah_chg]->value;
    }

    ui->statTable->setRowCount(0);
    auto addStatItem = [this](QString name) {
        ui->statTable->setRowCount(ui->statTable->rowCount() + 1);
        ui->statTable->setItem(ui->statTable->rowCount() - 1, 0, new QTableWidgetItem(name));
        ui->statTable->setItem(ui->statTable->rowCount() - 1, 1, new QTableWidgetItem(""));
    };

    addStatItem("Samples");
    addStatItem("Total Time");
    addStatItem("Distance");
    addStatItem("Distance ABS");
    addStatItem("Distance GNSS");
    addStatItem("Wh");
    addStatItem("Wh Charged");
    addStatItem("Ah");
    addStatItem("Ah Charged");
    addStatItem("Avg Speed");
    addStatItem("Avg Speed GNSS");
    addStatItem("Efficiency");
    addStatItem("Efficiency GNSS");
    addStatItem("Avg Sample Rate");

    QTime t(0, 0, 0, 0);
    t = t.addMSecs(timeTotMs);

    ui->statTable->item(0, 1)->setText(QString::number(samples));
    ui->statTable->item(1, 1)->setText(t.toString("hh:mm:ss.zzz"));
    ui->statTable->item(2, 1)->setText(QString::number(meters, 'f', 2) + " m");
    ui->statTable->item(3, 1)->setText(QString::number(metersAbs, 'f', 2) + " m");
    ui->statTable->item(4, 1)->setText(QString::number(metersGnss, 'f', 2) + " m");
    ui->statTable->item(5, 1)->setText(QString::number(wh, 'f', 2) + " Wh");
    ui->statTable->item(6, 1)->setText(QString::number(whCharge, 'f', 2) + " Wh");
    ui->statTable->item(7, 1)->setText(QString::number(ah, 'f', 2) + " Ah");
    ui->statTable->item(8, 1)->setText(QString::number(ahCharge, 'f', 2) + " Ah");
    ui->statTable->item(9, 1)->setText(QString::number(3.6 * metersAbs / (double(timeTotMs) / 1000.0), 'f', 2) + " km/h");
    ui->statTable->item(10, 1)->setText(QString::number(3.6 * metersGnss / (double(timeTotMs) / 1000.0), 'f', 2) + " km/h");
    ui->statTable->item(11, 1)->setText(QString::number((wh - whCharge) / (metersAbs / 1000.0), 'f', 2) + " wh/km");
    ui->statTable->item(12, 1)->setText(QString::number((wh - whCharge) / (metersGnss / 1000.0), 'f', 2) + " wh/km");
    ui->statTable->item(13, 1)->setText(QString::number(double(samples) / (double(timeTotMs) / 1000.0), 'f', 2) + " Hz");
}

void PageLogAnalysis::updateDataAndPlot(double time)
{
    if (mLogTruncated.isEmpty()) {
        return;
    }

    mPlayPosNow = time;

    double upper = ui->plot->xAxis->range().upper;
    double lower = ui->plot->xAxis->range().lower;
    if (time > upper) {
        time = upper;
    } else if (time < lower) {
        time = lower;
    }
    QVector<double> x(2) , y(2);
    x[0] = time; y[0] = ui->plot->yAxis->range().lower;
    x[1] = time; y[1] = ui->plot->yAxis->range().upper;
    mVerticalLine->setData(x, y);
    mVerticalLine->setVisible(true);
    ui->plot->replotWhenVisible();

    auto sample = getLogSample(int(time * 1000));
    auto first = mLogTruncated.first();

    if (mInd_t_day >= 0) {
        mVerticalLineMsLast = int(sample[mInd_t_day]->value * 1000.0);
    }

    int ind = 0;
    for (int i = 0;i < sample.size();i++) {
        const auto &e = sample.at(i);

        auto value = e->value;
        if (e->isRelativeToFirst) {
            value -= first[i]->value;

            if (e->isTimeStamp && value < 0) {
                value += 60 * 60 * 24;
            }
        }

        if (e->valueString.isEmpty()) {
            if (e->isTimeStamp) {
                QTime t(0, 0, 0, 0);
                t = t.addMSecs(value * 1000);
                ui->dataTable->item(ind, 1)->setText(t.toString("hh:mm:ss.zzz"));
            } else {
                ui->dataTable->item(ind, 1)->setText(
                            QString::number(value, 'f', e->precision) + " " + e->unit);
            }
        } else {
            ui->dataTable->item(ind, 1)->setText(e->valueString);
        }

        ind++;
    }

    bool skip = false;
    if (mInd_t_day_pos >= 0 && mInd_gnss_h_acc >= 0) {
        int postime = int(sample[mInd_t_day_pos]->value * 1000.0);
        double h_acc = sample[mInd_gnss_h_acc]->value;

        skip = true;
        if (postime >= 0 &&
                (!ui->filterOutlierBox->isChecked() ||
                 h_acc < ui->filterhAccBox->value())) {
            skip = false;
        }
    }

    if (mInd_gnss_lat < 0 || mInd_gnss_lon < 0) {
        skip = true;
    }

    if (!skip) {
        double i_llh[3];
        double llh[3];
        double xyz[3];

        ui->map->getEnuRef(i_llh);
        llh[0] = sample[mInd_gnss_lat]->value;
        llh[1] = sample[mInd_gnss_lon]->value;
        if (mInd_gnss_alt >= 0) {
            llh[2] = sample[mInd_gnss_alt]->value;
        } else {
            llh[2] = 0.0;
        }
        Utility::llhToEnu(i_llh, llh, xyz);

        LocPoint p;
        p.setXY(xyz[0], xyz[1]);
        p.setRadius(10);

        ui->map->setInfoTraceNow(1);
        ui->map->clearInfoTrace();
        ui->map->addInfoPoint(p);

        if (ui->followBox->isChecked()) {
            ui->map->moveView(xyz[0], xyz[1]);
        }
    }

    if (mInd_roll >= 0 && mInd_pitch >= 0 && mInd_yaw >= 0) {
        m3dView->setRollPitchYaw(sample[mInd_roll]->value * 180.0 / M_PI, sample[mInd_pitch]->value * 180.0 / M_PI,
                                 mUseYawBox->isChecked() ? sample[mInd_yaw]->value * 180.0 / M_PI : 0.0);
    }
}

QVector<LOG_ENTRY*> PageLogAnalysis::getLogSample(int timeMs)
{
    QVector<LOG_ENTRY*> d;

    if (!mLogTruncated.isEmpty()) {
        d = mLogTruncated.first();

        if (mInd_t_day >= 0) {
            int startTime = int(d[mInd_t_day]->value * 1000.0);

            for (auto dn: mLogTruncated) {
                int timeMsNow = (dn[mInd_t_day]->value * 1000) - startTime;
                if (timeMsNow < 0) { // Handle midnight
                    timeMsNow += 60 * 60 * 24 * 1000;
                }

                if (timeMsNow >= timeMs) {
                    d = dn;
                    break;
                }
            }
        }
    }

    return d;
}

void PageLogAnalysis::updateTileServers()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    if (ui->tilesOsmButton->isChecked()) {
        ui->map->osmClient()->setTileServerUrl("http://tile.openstreetmap.org");
        ui->map->osmClient()->setCacheDir(base + "/osm_tiles/osm");
        ui->map->osmClient()->clearCacheMemory();
    } else if (ui->tilesHiResButton->isChecked()) {
        ui->map->osmClient()->setTileServerUrl("http://c.osm.rrze.fau.de/osmhd");
        ui->map->osmClient()->setCacheDir(base + "/osm_tiles/hd");
        ui->map->osmClient()->clearCacheMemory();
    }
}

void PageLogAnalysis::logListRefresh()
{
    if (ui->tabWidget->currentIndex() == 3) {
        ui->logTable->setRowCount(0);
        QSettings set;
        if (set.contains("pageloganalysis/lastdir")) {
            QString dirPath = set.value("pageloganalysis/lastdir").toString();
            QDir dir(dirPath);
            if (dir.exists()) {
                for (QFileInfo f: dir.entryInfoList(QStringList() << "*.csv" << "*.Csv" << "*.CSV",
                                              QDir::Files, QDir::Name)) {
                    QTableWidgetItem *itName = new QTableWidgetItem(f.fileName());
                    itName->setData(Qt::UserRole, f.absoluteFilePath());
                    ui->logTable->setRowCount(ui->logTable->rowCount() + 1);
                    ui->logTable->setItem(ui->logTable->rowCount() - 1, 0, itName);
                    ui->logTable->setItem(ui->logTable->rowCount() - 1, 1,
                                          new QTableWidgetItem(QString("%1 MB").
                                                               arg(double(f.size())
                                                                   / 1024.0 / 1024.0,
                                                                   0, 'f', 2)));
                }
            }
        }
    }
}

void PageLogAnalysis::on_saveMapPdfButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save PDF"), "",
                                                    tr("PDF Files (*.pdf)"));

    if (!fileName.isEmpty()) {
        if (!fileName.toLower().endsWith(".pdf")) {
            fileName.append(".pdf");
        }

        ui->map->printPdf(fileName,
                          ui->saveWidthBox->value(),
                          ui->saveHeightBox->value());
    }
}

void PageLogAnalysis::on_saveMapPngButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save Image"), "",
                                                    tr("PNG Files (*.png)"));

    if (!fileName.isEmpty()) {
        if (!fileName.toLower().endsWith(".png")) {
            fileName.append(".png");
        }

        ui->map->printPng(fileName,
                          ui->saveWidthBox->value(),
                          ui->saveHeightBox->value());
    }
}

void PageLogAnalysis::on_savePlotPdfButton_clicked()
{
    Utility::plotSavePdf(ui->plot, ui->saveWidthBox->value(), ui->saveHeightBox->value());
}

void PageLogAnalysis::on_savePlotPngButton_clicked()
{
    Utility::plotSavePng(ui->plot, ui->saveWidthBox->value(), ui->saveHeightBox->value());
}

void PageLogAnalysis::on_centerButton_clicked()
{
    ui->map->zoomInOnInfoTrace(-1, 0.1);
}

void PageLogAnalysis::on_logListOpenButton_clicked()
{
    auto items = ui->logTable->selectedItems();

    if (items.size() > 0) {
        QString fileName = items.
                first()->data(Qt::UserRole).toString();
        if (mVesc->loadRtLogFile(fileName)) {
            on_openCurrentButton_clicked();
        }
    } else {
        mVesc->emitMessageDialog("Open Log", "No Log Selected", false);
    }
}

void PageLogAnalysis::on_logListRefreshButton_clicked()
{
    logListRefresh();
}

void PageLogAnalysis::on_logTable_cellDoubleClicked(int row, int column)
{
    (void)row; (void)column;
    on_logListOpenButton_clicked();
}

void PageLogAnalysis::on_vescLogListRefreshButton_clicked()
{
    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog("Refresh", "Not conected", false, false);
        mVescLastPath = "";
        return;
    }

    ui->vescLogTable->setRowCount(0);

    ui->vescLogTab->setEnabled(false);
    auto res = mVesc->commands()->fileBlockList(mVescLastPath);
    ui->vescLogTab->setEnabled(true);

    for (auto f: res) {
        FILE_LIST_ENTRY fe;
        if (f.canConvert<FILE_LIST_ENTRY>()) {
            fe = f.value<FILE_LIST_ENTRY>();
        }

        if (!fe.isDir && !fe.name.toLower().endsWith(".csv")) {
            continue;
        }

        QTableWidgetItem *itName = new QTableWidgetItem(fe.name);
        itName->setData(Qt::UserRole, QVariant::fromValue(fe));
        ui->vescLogTable->setRowCount(ui->vescLogTable->rowCount() + 1);
        ui->vescLogTable->setItem(ui->vescLogTable->rowCount() - 1, 0, itName);

        if (fe.isDir) {
            ui->vescLogTable->setItem(ui->vescLogTable->rowCount() - 1, 1,
                                      new QTableWidgetItem("Dir"));
        } else {
            ui->vescLogTable->setItem(ui->vescLogTable->rowCount() - 1, 1,
                                      new QTableWidgetItem(QString("%1 MB").
                                                           arg(double(fe.size)
                                                               / 1024.0 / 1024.0,
                                                               0, 'f', 2)));
        }
    }
}

void PageLogAnalysis::on_vescLogListOpenButton_clicked()
{
    if (!ui->vescLogListOpenButton->isEnabled()) {
        return;
    }

    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog("Open", "Not conected", false, false);
        mVescLastPath = "";
        return;
    }

    auto items = ui->vescLogTable->selectedItems();

    if (items.size() > 0) {
        FILE_LIST_ENTRY fe;
        if (items.first()->data(Qt::UserRole).canConvert<FILE_LIST_ENTRY>()) {
            fe = items.first()->data(Qt::UserRole).value<FILE_LIST_ENTRY>();
        }

        if (fe.isDir) {
            mVescLastPath += "/" + fe.name;
            mVescLastPath.replace("//", "/");
            on_vescLogListRefreshButton_clicked();
        } else {
            ui->vescLogListOpenButton->setEnabled(false);
            auto data = mVesc->commands()->fileBlockRead(mVescLastPath + "/" + fe.name);
            ui->vescLogListOpenButton->setEnabled(true);
            if (!data.isEmpty()) {
                if (mVesc->loadRtLogFile(data)) {
                    on_openCurrentButton_clicked();
                }
            }
        }
    }
}

void PageLogAnalysis::on_vescUpButton_clicked()
{
    if (!mVesc->isPortConnected()) {
        mVesc->emitMessageDialog("Up", "Not conected", false, false);
        mVescLastPath = "";
        return;
    }

    if (mVescLastPath.lastIndexOf("/") >= 0) {
        mVescLastPath = mVescLastPath.mid(0, mVescLastPath.lastIndexOf("/"));
        on_vescLogListRefreshButton_clicked();
    }
}

void PageLogAnalysis::on_vescLogCancelButton_clicked()
{
    mVesc->commands()->fileBlockCancel();
}

void PageLogAnalysis::on_vescLogTable_cellDoubleClicked(int row, int column)
{
    (void)row; (void)column;
    on_vescLogListOpenButton_clicked();
}
