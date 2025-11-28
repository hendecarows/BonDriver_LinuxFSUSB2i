// SPDX-License-Identifier: MIT
/*
 * BonDriver for FSUSB2i (BonDriver_LinuxFSUSB2i.cpp)
 *
 * Copyright (c) 2021 nns779
 *               2025 hendecarows
 */

#include "BonDriver_LinuxFSUSB2i.hpp"

#include <cstddef>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

#include <iconv.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <dlfcn.h>

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>

#include "util.hpp"

namespace BonDriver_LinuxFSUSB2i {

BonDriver::BonDriver(Config& config) :
	current_system_(::PTX_ISDB_T_SYSTEM),
	current_space_(0),
	current_channel_(0),
	usb_devfile_(""),
	vids_pids_({0x0511, 0x0046, 0x048d, 0xe275, 0x048d, 0x9175}),
	timeout_wait_tuning_(0),
	timeout_wait_stream_(0),
	timeout_purge_stream_(0),
	wait_purge_stream_(0),
	required_purge_size_(0),
	usb_endpoint_({-1, 0, nullptr, nullptr, 0}),
	device_state_(nullptr),
	ts_thread_(nullptr)
{
	CharCodeConv cv;

	auto sct = config.Get("BonDriver_LinuxFSUSB2i");

	if (!cv.Utf8ToUtf16(sct.Get("Name", "LinuxFSUSB2i"), name_))
		throw std::runtime_error("BonDriver::BonDriver: CharCodeConv::Utf8ToUtf16() failed");

	auto val = 0;
	std::string str;
	std::vector<std::string> res;

	// DebugLog
	// 0 : 出力しない, それ以外: 出力する
	// plog level : none = 0, fatal = 1, error = 2, warning = 3, info = 4, debug = 5, verbose = 6
	val = sct.Get("DebugLog", 2);
	val = (val == 0) ? 2 : 5;
	if (!plog::get()) {
		plog::init<plog::TxtFormatter>(static_cast<plog::Severity>(val), plog::streamStdErr);
	}
	PLOGD << "plog init = " << plog::get()->getInstance();
	PLOGD << "Config DebugLog = " << val;

	// USB デバイスの指定
	// (1)udevルールファイルで作成したデバイスファイルを指定
	// Device=/dev/fsmini0
	usb_devfile_ = sct.Get("Device", "");
	if (!usb_devfile_.empty()) {
		PLOGD << "Config Device = " << usb_devfile_;
	} else {
		// (2)VID,PIDを指定
		// 指定VID,PIDデバイスを検索して使用します
		// VIDPID=0x048d,0xe275
		str = sct.Get("VIDPID", "");
		if (!str.empty()) {
			util::Separate(str, res);
			try {
				auto vid = std::stoi(res.at(0), nullptr, 16);
				auto pid = std::stoi(res.at(1), nullptr, 16);
				if (vid < 0 || vid > 0xffff || pid < 0 || pid > 0xffff) {
					throw std::invalid_argument(str);
				}
				vids_pids_.clear();
				vids_pids_.emplace_back(static_cast<uint16_t>(vid));
				vids_pids_.emplace_back(static_cast<uint16_t>(pid));
			} catch (const std::exception& ex) {
				PLOGD << "failed to parse VIDPID = " << str;
			}
		}

		for (auto p = vids_pids_.cbegin(); p != vids_pids_.cend(); p += 2) {
			PLOGD << "Config VID,PID = 0x" << std::hex << std::setw(4) << std::setfill('0') << *p
			<< ",0x"  << std::hex << std::setw(4) << std::setfill('0') << *(p + 1);
		}
	}

	// チャンネル周波数の同調を待つ時間(ms)
	val = sct.Get("TimeoutWaitTuning", 1500);
	timeout_wait_tuning_ = (val <= 0) ? 1500 : val;
	PLOGD << "Config TimeoutWaitTuning = " << std::dec << timeout_wait_tuning_ << "ms";

	// ストリームの復調を待つ時間(ms)
	val = sct.Get("TimeoutWaitStream", 1500);
	timeout_wait_stream_ = (val <= 0) ? 1500 : val;
	PLOGD << "Config TimeoutWaitStream = " << timeout_wait_stream_ << "ms";

	// 初期ストリームを破棄する最大時間(ms)
	val = sct.Get("TimeoutPurgeStream", 1500);
	timeout_purge_stream_ = (val < 0) ? 0 : val;
	PLOGD << "Config TimeoutPurgeStream = " << timeout_purge_stream_ << "ms";

	// 初期ストリーム破棄処理の待機時間(ms)
	val = sct.Get("WaitPurgeStream", 100);
	wait_purge_stream_ = (val <= 0) ? 100 : val;
	PLOGD << "Config WaitPurgeStream = " << wait_purge_stream_ << "ms";

	// 初期ストリーム必要破棄サイズ(byte)
	val = sct.Get("RequiredPurgeSize", 57340);
	required_purge_size_ = (val < 0) ? 0 : val;
	PLOGD << "Config RequiredPurgeSize = " << required_purge_size_ << "byte";

	// チャンネル設定
	std::vector<std::string> spaces;
	util::Separate(config.Get("Space").Get("Space"), spaces);
	for (auto v : spaces) {
		v = "Space." + v;
		auto subspace_sct = config.Get(v);
		auto subspace_ch_sct = config.Get(v + ".Channel");

		auto sys_str = subspace_sct.Get("System");
		::ptx_system_type sys;

		if (!sys_str.compare("ISDB-T")) {
			sys = ::PTX_ISDB_T_SYSTEM;
		} else if (!sys_str.compare("ISDB-S")) {
			sys = ::PTX_ISDB_S_SYSTEM;
		} else {
			throw std::runtime_error("BonDriver::BonDriver: unknown system");
		}

		auto& space = space_.emplace_back(cv, subspace_sct.Get("Name"), sys);
		space.AddChannel(cv, subspace_ch_sct);
	}
}

BonDriver::~BonDriver()
{
	CloseTuner();
}

const BOOL BonDriver::OpenTuner(void)
{
	std::lock_guard<std::mutex> lock(mtx_);

	if (usb_endpoint_.fd >= 0) {
		PLOGD << "already open usb device fd = " << usb_endpoint_.fd;
		return TRUE;
	}

	if (!usb_devfile_.empty()) {
		usb_endpoint_.fd = usbdevfile_alloc_devfile(usb_devfile_.data());
	} else {
		usb_endpoint_.fd = usbdevfile_alloc_vid_pid(vids_pids_.data(), vids_pids_.size());
	}
	if (usb_endpoint_.fd < 0) {
		PLOGD << "failed to usbdevfile_alloc ret = " << usb_endpoint_.fd;
		return FALSE;
	}

	auto ret = it9175_create(&device_state_, &usb_endpoint_);
	if (ret != 0) {
		PLOGD << "failed to it9175_create ret = " << ret;
		goto close_device;
	}

	// ストリーム受信スレッドの生成
	ret = tsthread_create(&ts_thread_, &usb_endpoint_);
	if (ret != 0) {
		PLOGD << "failed to tsthread_create ret = " << ret;
		goto close_device;
	}

	return TRUE;

close_device:
	if (ts_thread_) {
		PLOGD << "tsthread_stop and tsthread_destroy";
		tsthread_stop(ts_thread_);
		tsthread_destroy(ts_thread_);
		ts_thread_ = nullptr;
	}

	if (device_state_) {
		PLOGD << "it9175_destroy";
		it9175_destroy(device_state_);
		device_state_ = nullptr;
	}

	if (usb_endpoint_.fd >= 0) {
		PLOGD << "close usb device fd";
		close(usb_endpoint_.fd);
		usb_endpoint_.fd = -1;
	}

	return FALSE;
}

void BonDriver::CloseTuner(void)
{
	std::lock_guard<std::mutex> lock(mtx_);

	if (usb_endpoint_.fd < 0) {
		PLOGD << "no open usb device fd = " << usb_endpoint_.fd;
		return;
	}

	if (ts_thread_) {
		PLOGD << "tsthread_stop and tsthread_destroy";
		tsthread_stop(ts_thread_);
		tsthread_destroy(ts_thread_);
		ts_thread_ = nullptr;
	}

	if (device_state_) {
		PLOGD << "it9175_destroy";
		it9175_destroy(device_state_);
		device_state_ = nullptr;
	}

	if (usb_endpoint_.fd >= 0) {
		PLOGD << "close usb device fd";
		close(usb_endpoint_.fd);
		usb_endpoint_.fd = -1;
	}

	current_space_.store(0, std::memory_order_release);
	current_channel_.store(0, std::memory_order_release);

	return;
}

const BOOL BonDriver::SetChannel(const BYTE bCh)
{
	return SetChannel(0, bCh);
}

const float BonDriver::GetSignalLevel(void)
{
	if (!device_state_) {
		return 0.0f;
	}

	uint8_t data[44];
	auto ret = it9175_readStatistic(device_state_, data);
	if (ret != 0) {
		return 0.1f;
	}

	return  static_cast<float>(data[3]);
}

const DWORD BonDriver::WaitTsStream(const DWORD dwTimeOut)
{
	int remain_time = (dwTimeOut < 0x10000000) ? dwTimeOut : 0x10000000;
	if (!ts_thread_) {
		return WAIT_FAILED;
	}

	auto ret = tsthread_wait(ts_thread_, remain_time);
	if (ret < 0) {
		return WAIT_FAILED;
	} else if (ret > 0) {
		return WAIT_OBJECT_0;
	}

	return WAIT_TIMEOUT;
}

const DWORD BonDriver::GetReadyCount(void)
{
	if (!ts_thread_) {
		return WAIT_FAILED;
	}

	auto ret = tsthread_readable(ts_thread_);
	return (ret > 0) ? 1: 0;
}

const BOOL BonDriver::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = nullptr;
	auto ret = GetTsStream(&pSrc, pdwSize, pdwRemain);
	if (ret) {
		if (*pdwSize > 0) {
			std::memcpy(pDst, pSrc, *pdwSize);
		}
		return TRUE;
	}

	return FALSE;
}

const BOOL BonDriver::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if (!ts_thread_) {
		return FALSE;
	}

	*pdwSize = tsthread_read(ts_thread_, (void**)(ppDst));
	*pdwRemain = GetReadyCount();

	return TRUE;
}

void BonDriver::PurgeTsStream(void)
{
	if (!ts_thread_) {
		return;
	}

	tsthread_read(ts_thread_, nullptr);
}

void BonDriver::Release(void)
{
	DestroyInstance();
	return;
}

LPCTSTR BonDriver::GetTunerName(void)
{
	return name_.get();
}

const BOOL BonDriver::IsTunerOpening(void)
{
	std::lock_guard<std::mutex> lock(mtx_);

	return (usb_endpoint_.fd >= 0) ? TRUE : FALSE;
}

LPCTSTR BonDriver::EnumTuningSpace(const DWORD dwSpace)
{
	try {
		return space_.at(dwSpace).GetName();
	} catch (const std::out_of_range&) {
		return NULL;
	}
}

LPCTSTR BonDriver::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	try {
		return space_.at(dwSpace).GetChannel(dwChannel).GetName();
	} catch (const std::out_of_range&) {
		return NULL;
	}
}

const BOOL BonDriver::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	std::lock_guard<std::mutex> lock(mtx_);
	DWORD freq = 0;

	if (usb_endpoint_.fd < 0) {
		return FALSE;
	}

	try {
		auto& s = space_.at(dwSpace);
		if (s.GetSystem() != ::PTX_ISDB_T_SYSTEM) {
			PLOGD << "system error = " << s.GetSystem();
			return FALSE;
		}

		freq = s.GetChannel(dwChannel).GetFreqT();
		PLOGD << "GetChannel space = " << dwSpace
			<< " channel = " << dwChannel
			<< " freq = " << freq;
		if (freq < 61000 || freq > 874000) {
			PLOGD << "freq error = " << freq;
			return FALSE;
		}
	} catch (const std::out_of_range&) {
		PLOGD << "out of range space or channel";
		return FALSE;
	}

	// チャンネル周波数変更
	auto ret = it9175_setFreq(device_state_, freq);
	PLOGD << "it9175_setFreq ret = " << ret;
	if (ret != 0) {
		PLOGD << "failed to it9175_setFreq ret = " << ret;
		return FALSE;
	}

	// 周波数同調確認
	ret = it9175_waitTuning(device_state_, timeout_wait_tuning_);
	auto has_channel = (ret & 0x01) ? true : false;
	PLOGD << "it9175_waitTuning ret = " << ret
		<< " found = " << (ret & 0x01)
		<< " empty = " << (ret & 0x02)
		<< " elapsed = " << (ret >> 2) << "ms";
	if (!has_channel) {
		PLOGD << "failed to it9175_waitTuning ret = " << ret;
		return FALSE;
	}

	// ストリーム復調確認
	ret = it9175_waitStream(device_state_, timeout_wait_stream_);
	auto has_stream = (ret & 0x01) ? true : false;
	PLOGD << "it9175_waitStream ret = " << ret
		<< " stream = " << (ret & 0x01)
		<< " overflow = " << (ret & 0x02)
		<< " elapsed = " << (ret >> 2) << "ms";
	if (!has_stream) {
		PLOGD << "failed to it9175_waitStream ret = " << ret;
		return FALSE;
	}

	// 安定してストリームが転送されるようになるまでの初期ストリームを捨てる処理
	// 破棄したストリームの累計がrequired_purge_size以上になるか、経過時間が
	// timeout_purge_streamを超えた場合に終了する
	if (timeout_purge_stream_ > 0) {
		auto now = std::chrono::steady_clock::now();
		auto timeout = now + std::chrono::milliseconds(timeout_purge_stream_);
		auto wait = std::chrono::milliseconds(wait_purge_stream_);
		auto has_error = false;
		auto purge_size = 0;
		while (now < timeout) {
			auto size = tsthread_read(ts_thread_, nullptr);
			PLOGD << "purge stream tsthread_read ret = " << size;
			if (size < 0) {
				PLOGD << "failed to tsthread_read ret = " << size;
				has_error = true;
				break;
			} else if (size > 0) {
				purge_size += size;
				if (purge_size >= required_purge_size_) {
					break;
				}
			}
			std::this_thread::sleep_for(wait);
			now = std::chrono::steady_clock::now();
		}

		PLOGD << "total purge size = " << purge_size;
		if (has_error || purge_size < required_purge_size_) {
			PLOGD << "tsthread_stop and tsthread_destory";
			tsthread_stop(ts_thread_);
			tsthread_destroy(ts_thread_);
			ts_thread_ = nullptr;
			return FALSE;
		}
	}

	current_space_.store(dwSpace, std::memory_order_release);
	current_channel_.store(dwChannel, std::memory_order_release);

	return TRUE;
}

const DWORD BonDriver::GetCurSpace(void)
{
	return current_space_.load(std::memory_order_acquire);
}

const DWORD BonDriver::GetCurChannel(void)
{
	return current_channel_.load(std::memory_order_acquire);
}

BonDriver::Space::Channel::Channel(CharCodeConv& cv, const std::string& name, int number, int slot)
	: number_(number),
	slot_(slot)
{
	if (!cv.Utf8ToUtf16(name, name_))
		throw std::runtime_error("BonDriver::Space::Channel::Channel: CharCodeConv::Utf8ToUtf16() failed");
}

const ::WCHAR * BonDriver::Space::Channel::GetName() const
{
	return name_.get();
}

void BonDriver::Space::Channel::ToFreq(::ptx_freq& freq) const
{
	freq.freq_no = number_;
	freq.slot = slot_;
	return;
}

uint32_t BonDriver::Space::Channel::GetFreqT() const
{
	uint32_t freq = 0;
	if ((number_ >= 3 && number_ <= 12) ||
		(number_ >= 22 && number_ <= 62)) {
		// CATV C13-C22ch, C23-C63ch
		freq = 93143 + number_ * 6000 + slot_;

		if (number_ == 12)
			freq += 2000;
	} else if (number_ >= 63 && number_ <= 112) {
		/* UHF 13-62ch */
		freq = 95143 + number_ * 6000 + slot_;
	}

	return freq;
}

BonDriver::Space::Space(CharCodeConv& cv, const std::string& name, ::ptx_system_type system)
	: system_(system)
{
	if (!cv.Utf8ToUtf16(name, name_))
		throw std::runtime_error("BonDriver::Space::Space: CharCodeConv::Utf8ToUtf16() failed");
}

const ::WCHAR * BonDriver::Space::GetName() const
{
	return name_.get();
}

::ptx_system_type BonDriver::Space::GetSystem() const
{
	return system_;
}

void BonDriver::Space::AddChannel(CharCodeConv& cv, Config::Section& sct)
{
	for (std::uint16_t i = 0; i < 300; i++) {
		char k[8];
		std::snprintf(k, 8, "Ch%u", i);

		auto key = std::string(k);
		try {
			std::vector<std::string> data;

			util::Separate(sct.Get(key), data);
			if (data.size() != 3)
				throw std::runtime_error("BonDriver::Space::AddChannel: invalid channel");

			channel_.emplace_back(cv, data[0], std::stoi(data[1], nullptr, 0), std::stoi(data[2], nullptr, 0));
		} catch (const std::out_of_range&) {
			break;
		}
	}

	return;
}

const BonDriver::Space::Channel& BonDriver::Space::GetChannel(std::size_t pos) const
{
	return channel_.at(pos);
}

std::mutex BonDriver::instance_mtx_;
BonDriver *BonDriver::instance_ = nullptr;

BonDriver * BonDriver::GetInstance()
{
	std::lock_guard<std::mutex> lock(instance_mtx_);

	if (!instance_) {
		try {
			Dl_info dli;
			if (!::dladdr(reinterpret_cast<void *>(GetInstance), &dli))
				return nullptr;

			std::size_t path_len = std::strlen(dli.dli_fname);
			if (path_len < 3 || std::strcmp(&dli.dli_fname[path_len - 3], ".so"))
				return nullptr;

			auto path = std::make_unique<char[]>(path_len + 5);
			std::strncpy(path.get(), dli.dli_fname, path_len - 2);
			std::strncpy(path.get() + path_len - 2, "ini", 4);

			Config config;
			if (!config.Load(path.get())) {
				std::strncpy(path.get() + path_len - 2, "so.ini", 7);
				if (!config.Load(path.get()))
					return nullptr;
			}

			instance_ = new BonDriver(config);
		} catch (...) {
			return nullptr;
		}
	}

	return instance_;
}

void BonDriver::DestroyInstance()
{
	std::lock_guard<std::mutex> lock(instance_mtx_);

	if (instance_) {
		delete instance_;
		instance_ = nullptr;
	}

	return;
}

extern "C" IBonDriver * CreateBonDriver()
{
	return BonDriver::GetInstance();
}

} // namespace BonDriver_LinuxFSUSB2i
