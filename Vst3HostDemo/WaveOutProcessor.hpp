#pragma once

#include <memory>
#include <utility>
#include <vector>

#include <boost/assert.hpp>
#include <boost/atomic.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/thread.hpp>

#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#pragma comment(lib, "winmm.lib")

namespace hwm {

//! WAVEHDRのラッパクラス
//! WAVEHDRを管理し、バイト単位で指定した長さのバッファを割り当てる。
struct WaveHeader {
	enum { UNUSED, USING, DONE };

	WaveHeader(size_t byte_length)
	{
		header_.lpData = new char[byte_length];
		header_.dwBufferLength = byte_length;
		header_.dwBytesRecorded = 0;
		header_.dwUser = UNUSED;
		header_.dwLoops = 0;
		header_.dwFlags = 0;
		header_.lpNext = nullptr;
		header_.reserved = 0;
	}

	~WaveHeader()
	{
		delete [] header_.lpData;
	}

	WaveHeader(WaveHeader &&rhs)
		:	header_(rhs.header_)
	{
		WAVEHDR empty = {};
		rhs.header_ = empty;
	}

	WaveHeader & operator=(WaveHeader &&rhs)
	{
		WaveHeader(std::move(rhs)).swap(rhs);
		return *this;
	}

	void swap(WaveHeader &rhs)
	{
		WAVEHDR tmp = header_;
		header_ = rhs.header_;
		rhs.header_ = tmp;
	}

	WAVEHDR * get()			{ return &header_; }

private:
	WAVEHDR	header_;
};

//! Waveオーディオデバイスをオープンし、
//! デバイスへの書き出しを行うクラス
struct WaveOutProcessor
{
	WaveOutProcessor()
		:	hwo_		(NULL)
		,	terminated_	(false)
		,	block_size_	(0)
		,	channel_	(0)
		,	multiplicity_(0)
	{
		InitializeCriticalSection(&cs_);
	}

	~WaveOutProcessor() {
		BOOST_ASSERT(!hwo_);
		DeleteCriticalSection(&cs_);
	}
	
	CRITICAL_SECTION cs_;
	HWAVEOUT hwo_;
	size_t block_size_;
	size_t multiplicity_;
	size_t channel_;
	std::vector<std::unique_ptr<WaveHeader>>	headers_;
	boost::thread					process_thread_;
	boost::atomic<bool>				terminated_;


	typedef
		std::function<void(short *data, size_t channel, size_t sample)>
	callback_function_t;

    //! デバイスのバッファが空いている場合に追加でデータを要求する
    //! コールバック関数
    //! デバイスのデータ形式は簡単のため、16bit符号付き整数固定
	callback_function_t				callback_;
	boost::mutex					initial_lock_mutex_;

    //! デバイスを開く
    //! 開くデバイスの指定は、現在WAVE_MAPPER固定。
    //! 簡単のため例外安全性などはあまり考慮されていない点に注意。
	bool OpenDevice(size_t sampling_rate, size_t channel, size_t block_size, size_t multiplicity, callback_function_t callback)
	{
		BOOST_ASSERT(0 < block_size);
		BOOST_ASSERT(0 < multiplicity);
		//BOOST_ASSERT(0 < channel && channel <= 2);
		BOOST_ASSERT(callback);
		BOOST_ASSERT(!process_thread_.joinable());

		block_size_ = block_size;
		channel_ = channel;
		callback_ = callback;
		multiplicity_ = multiplicity;

        //! デバイスがオープン完了するまでcallbackが呼ばれないようにするためのロック
		boost::unique_lock<boost::mutex> initial_lock(initial_lock_mutex_);

		terminated_ = false;
		process_thread_ = boost::thread([this] { ProcessThread(); });

		WAVEFORMATEXTENSIBLE wf;
		wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wf.Format.nChannels = channel;
		wf.Format.wBitsPerSample = 16;
		wf.Format.nBlockAlign = wf.Format.nChannels * (wf.Format.wBitsPerSample / 8);
		wf.Format.nSamplesPerSec = sampling_rate;
		wf.Format.nAvgBytesPerSec = wf.Format.nBlockAlign * wf.Format.nSamplesPerSec;
		wf.Format.cbSize = sizeof(wf) - sizeof(WAVEFORMATEX);
		wf.dwChannelMask = KSAUDIO_SPEAKER_5POINT1;
		wf.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		wf.Samples.wValidBitsPerSample = 16;

		headers_.resize(multiplicity_);
		for(auto &header: headers_) {
			header.reset(new WaveHeader(block_size * channel * sizeof(short)));
		}

        //! WAVEHDR使用済み通知を受け取る方式として
        //! CALLBACK_FUNCTIONを指定。
        //! 正常にデバイスがオープンできると、
        //! waveOutWriteを呼び出した後でWaveOutProcessor::waveOutProcに通知が来るようになる。
		MMRESULT const result = 
			waveOutOpen(
				&hwo_, 
				0, 
				(LPCWAVEFORMATEX)&wf, 
				reinterpret_cast<DWORD_PTR>(&WaveOutProcessor::waveOutProc), 
				reinterpret_cast<DWORD_PTR>(this),
				CALLBACK_FUNCTION
				);

		if(result != MMSYSERR_NOERROR) {
			terminated_ = true;
			initial_lock.unlock();
			process_thread_.join();
			terminated_ = false;
            hwo_ = NULL;

			return false;
		}

		return true;
	}

    //! デバイスを閉じる
	void CloseDevice() {
		terminated_.store(true);
		process_thread_.join();
		waveOutReset(hwo_);
		waveOutClose(hwo_);

        //! waveOutResetを呼び出したあとで
        //! WAVEHDRの使用済み通知が来ることがある。
        //! つまり、waveOutResetを呼び出した直後に
        //! すぐにWAVEHDRを解放してはいけない(デバイスによって使用中かもしれないため)
        //! そのため、確実にすべてのWAVEHDRの解放を確認する。
		for( ; ; ) {
			int left = 0;
			for(auto &header : headers_ | boost::adaptors::indirected) {
				if(header.get()->dwUser == WaveHeader::DONE) {
					waveOutUnprepareHeader(hwo_, header.get(), sizeof(WAVEHDR));
					header.get()->dwUser = WaveHeader::UNUSED;
				} else if(header.get()->dwUser == WaveHeader::USING) {
					left++;
				}
			}

			if(!left) { break; }
			Sleep(10);
		}
		hwo_ = NULL;
	}

    //! デバイスオープン時に指定したコールバック関数を呼び出して、
    //! デバイスに出力するオーディオデータを準備する。
	void PrepareData(WAVEHDR *header)
	{
		callback_(reinterpret_cast<short *>(header->lpData), channel_, block_size_);
	}

    //! 再生用データの準備と
    //! WAVEHDRの入れ替えを延々と行うワーカースレッド
	void ProcessThread()
	{
		{
			boost::unique_lock<boost::mutex> lock(initial_lock_mutex_);
		}
		for( ; ; ) {
			if(terminated_.load()) { break; }

            //! 使用済みWAVEHDRの確認
			for(auto &header: headers_ | boost::adaptors::indirected) {
				DWORD_PTR status = NULL;

				EnterCriticalSection(&cs_);
				status = header.get()->dwUser;
				LeaveCriticalSection(&cs_);

                //! 使用済みWAVEHDRはUnprepareして、未使用フラグを立てる。
				if(status == WaveHeader::DONE) {
					waveOutUnprepareHeader(hwo_, header.get(), sizeof(WAVEHDR));
					header.get()->dwUser = WaveHeader::UNUSED;
				}
			}

            //! 未使用WAVEHDRを確認
			for(auto &header: headers_ | boost::adaptors::indirected) {
				DWORD_PTR status = NULL;

				EnterCriticalSection(&cs_);
				status = header.get()->dwUser;
				LeaveCriticalSection(&cs_);

				if(status == WaveHeader::UNUSED) {
                    //! 再生用データを準備
					PrepareData(header.get());
					header.get()->dwUser = WaveHeader::USING;
                    //! waveOutPrepareHeaderを呼び出す前に、dwFlagsは必ず0にする。
					header.get()->dwFlags = 0;

                    //! WAVEHDRをPrepare
					waveOutPrepareHeader(hwo_, header.get(), sizeof(WAVEHDR));
                    //! デバイスへ書き出し(されるように登録)
					waveOutWrite(hwo_, header.get(), sizeof(WAVEHDR));
				}
			}

			Sleep(1);
		}
	}

    //! デバイスからの通知を受け取る関数
	static
	void CALLBACK waveOutProc(HWAVEOUT hwo, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR /*p2*/)
	{
		reinterpret_cast<WaveOutProcessor *>(instance)->WaveCallback(hwo, msg, reinterpret_cast<WAVEHDR*>(p1));
	}

    //! マルチメディア系のコールバック関数は
    //! 使える関数に限りがあるなど要求が厳しい
    //! 特に、この関数内でwaveOutUnprepareHeaderなどを呼び出してWAVEHDRの
    //! リセット処理を行ったりはできない。さもなくばシステムがデッドロックする。
	void WaveCallback(HWAVEOUT /*hwo*/, UINT msg, WAVEHDR *header)
	{
		switch(msg) {
			case WOM_OPEN:
				break;

			case WOM_CLOSE: 
				break;

			case WOM_DONE:
				EnterCriticalSection(&cs_);
                //! 使用済みフラグを立てるのみ
				header->dwUser = WaveHeader::DONE;
				LeaveCriticalSection(&cs_);
				break;
		}
	}
};

}	//::hwm
