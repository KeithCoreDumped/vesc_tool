#pragma once
#include <complex>
#include <fftw3.h>
#include <QVector>
#include <ranges>
#include <span>

class FFT {
public:
	FFT(int n) : N(n) {
		in = (double*)fftw_malloc(sizeof(double) * N);
		out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N / 2 + 1));
		p = fftw_plan_dft_r2c_1d(N, in, out, FFTW_MEASURE); // or FFTW_ESTIMATE?
	}

	~FFT() {
		fftw_destroy_plan(p);
		fftw_free(in);
		fftw_free(out);
	}

	QVector<std::complex<double>> transform(const QVector<double>& input) {
		// copy data
		std::ranges::copy(input, in);
		//for (int i = 0; i < N; ++i) {
		//	in[i] = input[i];
		//}

		// perform FFT
		fftw_execute(p);

		// copy FFT output to QVector
		QVector<std::complex<double>> output(N / 2 + 1);
		for (int i = 0; i < N / 2 + 1; ++i) {
			output[i] = { out[i][0], out[i][1] };
		}

		return output;
	}
	static QVector<double> getAbs(std::span<std::complex<double>> input) {
		QVector<double> ret(input.size());
		auto j = std::begin(ret);
		for (auto& i : input) {
			*j++ = std::abs(i);
		}
		return ret;
	}
	static void applyLowPassFilter(QVector<std::complex<double>>& input, int cutoff_freq) {
		using namespace std::complex_literals;
		std::ranges::fill(input | std::views::drop(cutoff_freq), 0i);
	}
private:
	int N;
	double* in;
	fftw_complex* out;
	fftw_plan p;
};

class IFFT {
public:
	IFFT(int n) : N(n) {
		in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N / 2 + 1));
		out = (double*)fftw_malloc(sizeof(double) * N);
		p = fftw_plan_dft_c2r_1d(N, in, out, FFTW_MEASURE);
	}

	~IFFT() {
		fftw_destroy_plan(p);
		fftw_free(in);
		fftw_free(out);
	}

	QVector<double> transform(const QVector<std::complex<double>>& input) {
		// copy data to IFFT input
		for (int i = 0; i < (N / 2 + 1); ++i) {
			in[i][0] = input[i].real();
			in[i][1] = input[i].imag();
		}

		// perform IFFT
		fftw_execute(p);

		// copy IFFT output to QVector
		QVector<double> output(N);
		for (int i = 0; i < N; ++i) {
			output[i] = out[i] / N;
		}

		return output;
	}

private:
	int N;
	fftw_complex* in;
	double* out;
	fftw_plan p;
};