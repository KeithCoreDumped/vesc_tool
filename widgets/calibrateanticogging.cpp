#include "calibrateanticogging.h"
#include "ui_calibrateanticogging.h"
#include "utility.h"
#include <QDebug>
#include <ranges>

CalibrateAnticogging::CalibrateAnticogging(QWidget* parent) :
	QWidget(parent),
	ui(new Ui::CalibrateAnticogging), fft(3600), ifft(3600) {
	ui->setupUi(this);

	auto csvMenu = new QMenu(this);
	csvMenu->addActions({ ui->actionImportCSV, ui->actionExportCSV });
	ui->csvToolButton->setMenu(csvMenu);

	acDegreeAxis.resize(3600);
	acDataForward.resize(3600);
	acDataReverse.resize(3600);
	acFreqAxis.resize(3600 / 2 + 1);
	std::ranges::iota(acFreqAxis, 0);
	for (int i = 0; i < 3600; i++) {
		acDegreeAxis[i] = i / 10.0;
	}

	Utility::setPlotColors(ui->plot);
	ui->plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

	ui->plot->addGraph();
	ui->plot->addGraph();
	ui->plot->graph(0)->setPen(QPen(Utility::getAppQColor("plot_graph1")));
	ui->plot->graph(0)->setName("Forward");
	ui->plot->graph(0)->setVisible(true);

	ui->plot->graph(1)->setPen(QPen(Utility::getAppQColor("plot_graph2")));
	ui->plot->graph(1)->setName("Reverse");
	ui->plot->graph(1)->setVisible(true);

	QFont legendFont = font();
	legendFont.setPointSize(9);
	ui->plot->legend->setVisible(true);
	ui->plot->legend->setFont(legendFont);
	ui->plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignRight | Qt::AlignBottom);
	ui->plot->xAxis->setLabel("Degrees");
	ui->plot->yAxis->setLabel("Q Current");
	ui->plot->rescaleAxes();
	ui->plot->replotWhenVisible();

	ui->progressBar->setMaximum(3700 * 2);
	acSampleCounter = 0;
	ui->progressBar->setValue(acSampleCounter);
}

VescInterface* CalibrateAnticogging::vesc() const {
	return mVesc;
}

void CalibrateAnticogging::setVesc(VescInterface* vesc) {
	mVesc = vesc;

	if (mVesc) {
		connect(mVesc->commands(), &Commands::focAnticoggingCalibrationDataReceived,
			this, &CalibrateAnticogging::focAnticoggingCalibrationDataReceived);
	}
}

void CalibrateAnticogging::focAnticoggingCalibrationDataReceived(bool finish, bool success, bool forward, int pos_index, double iq) {
	if (pos_index < 0 || pos_index > 3600 || !success) {
		mVesc->emitStatusMessage("Bad Anticogging Data Received", false);
	}
	else if (success) {
		//qWarning() << "pos=" << pos_index << "iq=" << iq;
		if (pos_index != 3600) {
			// discard pos = 3600
			if (forward) {
				acDataForward[pos_index] = iq;
			}
			else {
				acDataReverse[pos_index] = iq;
			}
			ui->progressBar->setValue(++acSampleCounter);
		}
		updateGraph();
	}
}

void CalibrateAnticogging::on_startButton_clicked() {
	uint16_t attempt = ui->maxAttemptBox->value();
	uint16_t smplppt = ui->smplPerPtBox->value();
	double err_abs_threshold = ui->posAbsToleranceBox->value();
	double err_threshold = ui->posToleranceBox->value();
	mVesc->commands()->focAnticoggingCalibrationStart(attempt, smplppt, err_abs_threshold, err_threshold);
	std::ranges::fill(acDataForward, 0.0);
	std::ranges::fill(acDataReverse, 0.0);
	ui->plot->rescaleAxes();
	ui->plot->replotWhenVisible();
	ui->progressBar->setMaximum(3700 * 2);
	acSampleCounter = 0;
	acSampleStart = std::chrono::steady_clock::now();
	ui->progressBar->setValue(acSampleCounter);
}

void CalibrateAnticogging::on_cancelButton_clicked() {
	mVesc->commands()->setCurrent(0);
	emit focAnticoggingCancelDownloadCalData();
}

void CalibrateAnticogging::on_readCalDataButton_clicked() {
	ui->progressBar->setMaximum(3600 * 4 * 2);
	ui->progressBar->setValue(0);
	enum AC_WAIT_RESULT {
		AC_WAIT_OK,
		AC_WAIT_TIMEOUT,
		AC_WAIT_ERROR
	};
	auto waitForData = [this] {
		QEventLoop loop;
		QTimer timeoutTimer;
		timeoutTimer.setSingleShot(true);
		timeoutTimer.start(3000);
		AC_WAIT_RESULT res = AC_WAIT_OK;
		VByteArray payload;
		auto conn = connect(mVesc->commands(), &Commands::focAnticoggingCalDataReadBackReceived,
			[&](bool valid, VByteArray data) {
				if (!valid) {
					res = AC_WAIT_ERROR;
				}
				else {
					payload = std::move(data);
					res = AC_WAIT_OK;
				}
				loop.quit();
			});
		connect(&timeoutTimer, &QTimer::timeout, [&] {
			res = AC_WAIT_TIMEOUT;
			loop.quit();
			});
		loop.exec();
		disconnect(conn);
		return std::pair{ res , payload };
		};
	mVesc->commands()->focAnticoggingReadBackCalData(AC_BLOCK_START, 0, 0);
	{
		auto [res, payload] = waitForData();
		switch (res) {
		case AC_WAIT_OK:
			break;
		case AC_WAIT_TIMEOUT:
			QMessageBox::critical(this, "Error", "Data read timeout.");
			return;
		case AC_WAIT_ERROR:
			QMessageBox::information(this, "Information", "No valid data in connected VESC.");
			return;
		default:
			break;
		}
	}
	VByteArray data;
	ui->progressBar->setMaximum(3600 * 4 * 2);
	ui->progressBar->setValue(data.length());
	while (data.length() < 3600 * 4 * 2) {
		mVesc->commands()->focAnticoggingReadBackCalData(
			AC_BLOCK_ONGOING, data.length(),
			std::min(500, 3600 * 4 * 2 - data.length())
		);
		auto [res, payload] = waitForData();
		switch (res) {
		case AC_WAIT_OK:
			std::ranges::copy(payload, std::back_inserter(data));
			ui->progressBar->setValue(data.length());
			break;
		case AC_WAIT_TIMEOUT:
			QMessageBox::critical(this, "Error", "Data read timeout.");
			return;
		case AC_WAIT_ERROR:
			QMessageBox::information(this, "Information", "No valid data in connected VESC.");
			return;
		default:
			break;
		}
	}
	// read complete

	QVector<double> tmp_common_mode(3600);
	QVector<double> tmp_diff_mode(3600);
	std::generate_n(std::begin(tmp_common_mode), 3600, [&] {
		return data.vbPopFrontDouble32Auto();
		});
	std::generate_n(std::begin(tmp_diff_mode), 3600, [&] {
		return data.vbPopFrontDouble32Auto();
		});
	fromDecomposed(tmp_common_mode, tmp_diff_mode);
	updateGraph();
	on_rescaleButton_clicked();
	QMessageBox::information(this, "Information", "Read complete.");
}

void CalibrateAnticogging::on_downloadCalDataButton_clicked() {
	auto waitForAck = [this] {
		QEventLoop loop;
		QTimer timeoutTimer;
		timeoutTimer.setSingleShot(true);
		timeoutTimer.start(3000);
		bool res = false;
		auto conn = connect(mVesc->commands(), &Commands::focAnticoggingCalDataAckReceived,
			[&res, &loop](bool res_) {
				res = res_;
				loop.quit();
			});
		connect(&timeoutTimer, SIGNAL(timeout()), &loop, SLOT(quit()));
		loop.exec();
		disconnect(conn);
		return res;
		};
	VByteArray vb;
	// start
	mVesc->commands()->focAnticoggingDownloadCalData(AC_BLOCK_START, 0, {});
	if (!waitForAck()) {
		QMessageBox::critical(this, "Error", "Upload failed or timeout.");
		return;
	}
	// ongoing
	vb.clear();
	QVector<double> cm_download, dm_download;
	if (ui->cutOffCheckBox->isChecked()) {
		QVector<double> cm_fft_abs, dm_fft_abs, fwd_filtered, rev_filtered;
		int cm_freq = ui->cmFreqBox->value();
		int dm_freq = ui->dmFreqBox->value();
		getFiltered(cm_freq, dm_freq,
			cm_fft_abs, dm_fft_abs,
			cm_download, dm_download,
			fwd_filtered, rev_filtered
		);
	}
	else {
		getDecomposed(cm_download, dm_download);
	}
	for (auto x : cm_download) {
		vb.vbAppendDouble32Auto(x);
	}
	for (auto x : dm_download) {
		vb.vbAppendDouble32Auto(x);
	}
	// todo: error handling
	Q_ASSERT(vb.length() == 3600 * 4 * 2);

	ui->progressBar->setMaximum(vb.length());
	// divide
	const uint32_t packet_max_len = 500; // less than 512
	uint32_t offset = 0;
	for (auto x : vb | std::views::chunk(packet_max_len)) {
		mVesc->commands()->focAnticoggingDownloadCalData(AC_BLOCK_ONGOING, offset, x);
		if (!waitForAck()) {
			QMessageBox::critical(this, "Error", "Upload failed or timeout.");
			return;
		}
		offset += x.size();
		ui->progressBar->setValue(offset);
	}

	ui->progressBar->setValue(vb.length());
	// end
	vb.clear();
	mVesc->commands()->focAnticoggingDownloadCalData(AC_BLOCK_END, 0, {});
	if (!waitForAck()) {
		QMessageBox::critical(this, "Error", "Upload failed or timeout.");
	}
	else {
		QMessageBox::information(this, "Information", "Upload complete.");
	}
}

void CalibrateAnticogging::on_zoomHButton_toggled(bool checked) {
	(void)checked;
	updateZoom();
}

void CalibrateAnticogging::on_zoomVButton_toggled(bool checked) {
	(void)checked;
	updateZoom();
}

void CalibrateAnticogging::on_rescaleButton_clicked() {
	ui->plot->rescaleAxes();
	ui->plot->replotWhenVisible();
}

void CalibrateAnticogging::on_graphSelectBox_currentIndexChanged(int index) {
	updateGraphSelection();
}

void CalibrateAnticogging::on_cmFreqBox_valueChanged(int) {
	if (ui->cutOffCheckBox->isChecked()) {
		updateGraph();
	}
}

void CalibrateAnticogging::on_dmFreqBox_valueChanged(int) {
	if (ui->cutOffCheckBox->isChecked()) {
		updateGraph();
	}
}

void CalibrateAnticogging::on_cutOffCheckBox_clicked() {
	updateGraph();
}

void CalibrateAnticogging::updateZoom() {
	Qt::Orientations plotOrientations = Qt::Orientations(
		((ui->zoomHButton->isChecked() ? Qt::Horizontal : 0) |
			(ui->zoomVButton->isChecked() ? Qt::Vertical : 0)));

	ui->plot->axisRect()->setRangeZoom(plotOrientations);
}

void CalibrateAnticogging::updateGraph() {
	int selection = ui->graphSelectBox->currentIndex();
	bool filtered = ui->cutOffCheckBox->isChecked();
	QVector<double> cm_fft_abs, dm_fft_abs, cm_filtered, dm_filtered, fwd_filtered, rev_filtered;
	if (filtered) {
		int cm_freq = ui->cmFreqBox->value();
		int dm_freq = ui->dmFreqBox->value();
		getFiltered(cm_freq, dm_freq,
			cm_fft_abs, dm_fft_abs,
			cm_filtered, dm_filtered,
			fwd_filtered, rev_filtered
		);
	}
	if (selection == 0) {
		// show sampled raw data
		ui->plot->graph(0)->setData(acDegreeAxis, filtered ? fwd_filtered : acDataForward, true);
		ui->plot->graph(1)->setData(acDegreeAxis, filtered ? rev_filtered : acDataReverse, true);
	}
	else if (selection == 1) {
		// show decomposed raw data
		QVector<double> tmp_common_mode(3600), tmp_diff_mode(3600);
		getDecomposed(tmp_common_mode, tmp_diff_mode);
		ui->plot->graph(0)->setData(acDegreeAxis, filtered ? cm_filtered : tmp_common_mode, true);
		ui->plot->graph(1)->setData(acDegreeAxis, filtered ? dm_filtered : tmp_diff_mode, true);
	}
	else {
		// show decomposed fft data
		if (filtered) {
			ui->plot->graph(0)->setData(acFreqAxis, cm_fft_abs, true);
			ui->plot->graph(1)->setData(acFreqAxis, dm_fft_abs, true);
		}
		else {
			// perform fft
			QVector<double> tmp_common_mode(3600);
			QVector<double> tmp_diff_mode(3600);
			getDecomposed(tmp_common_mode, tmp_diff_mode);
			auto tmp_cm_fft = fft.transform(tmp_common_mode);
			auto tmp_dm_fft = fft.transform(tmp_diff_mode);
			auto tmp_cm_fft_abs = FFT::getAbs(tmp_cm_fft);
			auto tmp_dm_fft_abs = FFT::getAbs(tmp_dm_fft);
			ui->plot->graph(0)->setData(acFreqAxis, tmp_cm_fft_abs, true);
			ui->plot->graph(1)->setData(acFreqAxis, tmp_dm_fft_abs, true);
		}
	}
	if (ui->autoscaleButton->isChecked()) {
		ui->plot->rescaleAxes();
	}
	ui->plot->replotWhenVisible();
}

void CalibrateAnticogging::updateGraphSelection() {
	int selection = ui->graphSelectBox->currentIndex();
	if (selection == 0) {
		// show sampled raw data
		ui->plot->graph(0)->setPen(QPen(Utility::getAppQColor("plot_graph1")));
		ui->plot->graph(0)->setName("Forward");
		ui->plot->graph(0)->setVisible(true);

		ui->plot->graph(1)->setPen(QPen(Utility::getAppQColor("plot_graph2")));
		ui->plot->graph(1)->setName("Reverse");
		ui->plot->graph(1)->setVisible(true);

		ui->plot->legend->setVisible(true);
		ui->plot->xAxis->setLabel("Degrees");
		ui->plot->yAxis->setLabel("Q Current");
	}
	else if (selection == 1) {
		// show decomposed raw data
		ui->plot->graph(0)->setPen(QPen(Utility::getAppQColor("plot_graph3")));
		ui->plot->graph(0)->setName("Common Mode");
		ui->plot->graph(0)->setVisible(true);

		ui->plot->graph(1)->setPen(QPen(Utility::getAppQColor("plot_graph4")));
		ui->plot->graph(1)->setName("Differential Mode");
		ui->plot->graph(1)->setVisible(true);

		ui->plot->legend->setVisible(true);
		ui->plot->xAxis->setLabel("Degrees");
		ui->plot->yAxis->setLabel("Q Current");
	}
	else {
		// show decomposed fft data
		ui->plot->graph(0)->setPen(QPen(Utility::getAppQColor("plot_graph3")));
		ui->plot->graph(0)->setName("Common Mode");
		ui->plot->graph(0)->setVisible(true);
		//ui->plot->graph(0)->setData(acFreqAxis, tmp_cm_fft_abs, true);

		ui->plot->graph(1)->setPen(QPen(Utility::getAppQColor("plot_graph4")));
		ui->plot->graph(1)->setName("Differential Mode");
		ui->plot->graph(1)->setVisible(true);
		//ui->plot->graph(1)->setData(acFreqAxis, tmp_dm_fft_abs, true);

		ui->plot->legend->setVisible(true);
		ui->plot->xAxis->setLabel("Freq");
		ui->plot->yAxis->setLabel("Amplitude");
	}
	updateGraph();
	ui->plot->rescaleAxes();
	ui->plot->replotWhenVisible();
}
void CalibrateAnticogging::getDecomposed(QVector<double>& out_common_mode, QVector<double>& out_diff_mode) {
	out_common_mode.resize(3600);
	out_diff_mode.resize(3600);
	// generate common/differential mode iq data
	std::ranges::transform(
		acDataForward, acDataReverse,
		std::begin(out_common_mode), [](auto fwd, auto rev) { return (fwd + rev) / 2.0; }
	);
	std::ranges::transform(
		acDataForward, acDataReverse,
		std::begin(out_diff_mode), [](auto fwd, auto rev) { return (fwd - rev) / 2.0; }
	);
}
void CalibrateAnticogging::fromDecomposed(const QVector<double>& in_common_mode, const QVector<double>& in_diff_mode) {
	// generate sampled (forward/reverse) iq data
	std::ranges::transform(
		in_common_mode, in_diff_mode,
		std::begin(acDataForward), std::plus{}
	);
	std::ranges::transform(
		in_common_mode, in_diff_mode,
		std::begin(acDataReverse), std::minus{}
	);
}
void CalibrateAnticogging::getFFT(QVector<double>& out_cm_fft_abs, QVector<double>& out_dm_fft_abs) {
	// perform fft
	QVector<double> tmp_common_mode(3600);
	QVector<double> tmp_diff_mode(3600);
	getDecomposed(tmp_common_mode, tmp_diff_mode);
	auto tmp_cm_fft = fft.transform(tmp_common_mode);
	auto tmp_dm_fft = fft.transform(tmp_diff_mode);
	out_cm_fft_abs = FFT::getAbs(tmp_cm_fft);
	out_dm_fft_abs = FFT::getAbs(tmp_dm_fft);
}
void CalibrateAnticogging::getFiltered(
	int cm_cutoff_freq, int dm_cutoff_freq,
	QVector<double>& out_cm_fft_abs, QVector<double>& out_dm_fft_abs,
	QVector<double>& out_cm_filtered, QVector<double>& out_dm_filtered,
	QVector<double>& out_fwd_filtered, QVector<double>& out_rev_filtered
) {
	// perform fft
	QVector<double> tmp_common_mode(3600);
	QVector<double> tmp_diff_mode(3600);
	getDecomposed(tmp_common_mode, tmp_diff_mode);
	auto tmp_cm_fft = fft.transform(tmp_common_mode);
	auto tmp_dm_fft = fft.transform(tmp_diff_mode);
	// apply filter
	FFT::applyLowPassFilter(tmp_cm_fft, cm_cutoff_freq);
	FFT::applyLowPassFilter(tmp_dm_fft, dm_cutoff_freq);
	// perform ifft
	out_cm_filtered = ifft.transform(tmp_cm_fft);
	out_dm_filtered = ifft.transform(tmp_dm_fft);
	out_cm_fft_abs = FFT::getAbs(tmp_cm_fft);
	out_dm_fft_abs = FFT::getAbs(tmp_dm_fft);
	// generate sampled (forward/reverse) iq data
	out_fwd_filtered.resize(3600);
	out_rev_filtered.resize(3600);
	std::ranges::transform(
		out_cm_filtered, out_dm_filtered,
		std::begin(out_fwd_filtered), std::plus{}
	);
	std::ranges::transform(
		out_cm_filtered, out_dm_filtered,
		std::begin(out_rev_filtered), std::minus{}
	);
}
//
//QVector<double> CalibrateAnticogging::FourierTransform(QVector<double> f_t) {
//	int N = f_t.size();
//
//	// ������������������
//	fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);
//	fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);
//
//	// ��QVector���ݸ��Ƶ�FFTW��������
//	for (int i = 0; i < N; ++i) {
//		in[i][0] = f_t[i];
//		in[i][1] = 0.0;
//	}
//
//	// ����FFT�ƻ���ִ��
//	p = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
//	fftw_execute(p);
//
//	// ���������ݸ��Ƶ�QVector
//	QVector<double> output(2 * N);  // �洢ʵ�����鲿
//	for (int i = 0; i < N; ++i) {
//		output[2 * i] = out[i][0];
//		output[2 * i + 1] = out[i][1];
//	}
//
//	// ����
//	fftw_destroy_plan(p);
//	fftw_free(in);
//	fftw_free(out);
//
//	return output;
//}
//
//QVector<double> CalibrateAnticogging::InvertFourierTransform(QVector<double> f_i) {
//	fftw_complex* out_cpx;
//
//	fftw_plan ifft;
//	QVector<double> out(f_i.length());
//	QVector<double> err(f_i.length());
//	out_cpx = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * f_i.length());
//	ifft = fftw_plan_dft_c2r_1d(f_i.length(), out_cpx, out.data(), FFTW_ESTIMATE);   //Setup fftw plan for ifft
//	fftw_execute(ifft);
//	return out;
//}

void CalibrateAnticogging::on_actionImportCSV_triggered() {
	QString fileName = QFileDialog::getOpenFileName(this,
		tr("Open CSV"), "",
		tr("CSV Files (*.csv)"));

	if (!fileName.isEmpty()) {
		if (!fileName.toLower().endsWith(".csv")) {
			fileName.append(".csv");
		}

		QFile file(fileName);
		if (!file.open(QIODevice::ReadOnly)) {
			QMessageBox::critical(this, "Open CSV File",
				"Could not open\n" + fileName + "\nfor reading");
			return;
		}

		QTextStream stream(&file);
		stream.setCodec("UTF-8");
		if (stream.readLine() != "pos, iq_forward, iq_reverse") {
			goto fail;
		}
		{
			QVector<double> newDegreeAxis(3600), newDataForward(3600), newDataReverse(3600);
			for (int i = 0; i < 3600; i++) {
				double pos, iq_forward, iq_reverse;
				char crlf = 0, comma = 0;
				stream >> pos >> comma;
				stream >> iq_forward >> comma;
				stream >> iq_reverse >> crlf;
				newDegreeAxis[i] = pos;
				newDataForward[i] = iq_forward;
				newDataReverse[i] = iq_reverse;
				if (comma != ',' || crlf != '\n' || stream.status() != QTextStream::Ok) {
					goto fail;
				}
			}
			acDegreeAxis = newDegreeAxis;
			acDataForward = newDataForward;
			acDataReverse = newDataReverse;
		}
		file.close();
		updateGraph();
		on_rescaleButton_clicked();
		return;
	fail:
		QMessageBox::critical(this, "Error", "Failed to parse selected CSV file.");
		file.close();
	}
}

void CalibrateAnticogging::on_actionExportCSV_triggered() {
	QString fileName = QFileDialog::getSaveFileName(this,
		tr("Save CSV"), "",
		tr("CSV Files (*.csv)"));

	if (!fileName.isEmpty()) {
		if (!fileName.toLower().endsWith(".csv")) {
			fileName.append(".csv");
		}

		QFile file(fileName);
		if (!file.open(QIODevice::WriteOnly)) {
			QMessageBox::critical(this, "Save CSV File",
				"Could not open\n" + fileName + "\nfor writing");
			return;
		}

		QTextStream stream(&file);
		stream.setCodec("UTF-8");

		stream << "pos, iq_forward, iq_reverse\n";

		for (int i = 0; i < acDegreeAxis.size(); i++) {
			stream << QString::number(acDegreeAxis.at(i), 'f', 1) << ", ";
			stream << QString::number(acDataForward.at(i), 'f', 6) << ", ";
			stream << QString::number(acDataReverse.at(i), 'f', 6) << "\n";
		}

		file.close();
	}
}

CalibrateAnticogging::~CalibrateAnticogging() {
	delete ui;
}


