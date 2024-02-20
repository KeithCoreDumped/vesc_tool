#ifndef CALIBRATEANTICOGGING_H
#define CALIBRATEANTICOGGING_H

#include <array>
#include <QWidget>
#include <fftw3.h>

#include "fftw3wrapper.h"
#include "vescinterface.h"

namespace Ui {
	class CalibrateAnticogging;
}

class CalibrateAnticogging : public QWidget {
	Q_OBJECT

public:
	explicit CalibrateAnticogging(QWidget* parent = nullptr);
	~CalibrateAnticogging();
	VescInterface* vesc() const;
	void setVesc(VescInterface* vesc);

private slots:
	void focAnticoggingCalibrationDataReceived(bool finish, bool success, bool forward, int pos_index, double iq);
	void on_startButton_clicked();
	void on_cancelButton_clicked();
	void on_readCalDataButton_clicked();
	void on_downloadCalDataButton_clicked();
	void on_zoomHButton_toggled(bool checked);
	void on_zoomVButton_toggled(bool checked);
	void on_rescaleButton_clicked();
	void on_actionImportCSV_triggered();
	void on_actionExportCSV_triggered();
	void on_graphSelectBox_currentIndexChanged(int index);
	void on_cmFreqBox_valueChanged(int);
	void on_dmFreqBox_valueChanged(int);
	void on_cutOffCheckBox_clicked();

signals:
	void focAnticoggingCancelDownloadCalData();

private:
	Ui::CalibrateAnticogging* ui;
	VescInterface* mVesc;
	QVector<double> acDegreeAxis;
	QVector<double> acFreqAxis;
	QVector<double> acDataForward;
	QVector<double> acDataReverse;
	int acSampleCounter;
	std::chrono::time_point<std::chrono::steady_clock> acSampleStart;
	FFT fft;
	IFFT ifft;
	void updateZoom();
	void updateGraph();
	void updateGraphSelection();
	void getDecomposed(QVector<double>& out_common_mode, QVector<double>& out_diff_mode);
	void fromDecomposed(const QVector<double>& in_common_mode, const QVector<double>& in_diff_mode);
	void getFFT(QVector<double>& out_cm_fft_abs, QVector<double>& out_dm_fft_abs);
	void getFiltered(int cm_cutoff_freq, int dm_cutoff_freq,
		QVector<double>& out_cm_fft_abs, QVector<double>& out_dm_fft_abs,
		QVector<double>& out_cm_filtered, QVector<double>& out_dm_filtered,
		QVector<double>& out_fwd_filtered, QVector<double>& out_rev_filtered);
};

#endif // CALIBRATEANTICOGGING_H
