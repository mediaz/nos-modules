#pragma once
#include <MediaZ/PluginAPI.h>
#include <chrono>
class mzWebRTCStatsLogger
{
public:
	mzWebRTCStatsLogger(std::string name, int refreshRate = 100) : RefreshRate(refreshRate) 
	{
		Name_FPS = name + " FPS: ";
		Name_MAX_FPS = name + " MAX FPS: ";
		Name_MIN_FPS = name + " MIN FPS: ";
		startTime = std::chrono::high_resolution_clock::now();
	};

	void LogStats() {
		if (++FrameCount > RefreshRate)
		{
			// Clear stats for each 100 frame
			FrameCount = 0;
			MaxFPS = -9999;
			MinFPS = 9999;
		}
		auto now = std::chrono::high_resolution_clock::now();
		auto FPS = 1.0 / (std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count()) * 1000.0;
		MaxFPS = (FPS > MaxFPS && FPS < 500) ? (FPS) : (MaxFPS);
		MinFPS = (MinFPS > FPS && FPS > 5) ? (FPS) : (MinFPS);
		mzEngine.WatchLog(Name_FPS.c_str(), std::to_string(FPS).c_str());
		mzEngine.WatchLog(Name_MAX_FPS.c_str() , std::to_string(MaxFPS).c_str());
		mzEngine.WatchLog(Name_MIN_FPS.c_str(), std::to_string(MinFPS).c_str());
		startTime = now;
	}

private:
	std::string Name_FPS, Name_MAX_FPS, Name_MIN_FPS;
	int FrameCount = 0;
	int RefreshRate = 100;
	std::chrono::steady_clock::time_point startTime;
	float MinFPS = 9999, MaxFPS = -9999, FPS = 0;

};

struct RingProxy {
public:
	RingProxy(size_t size, std::string ringName = "") :Size(size), FreeCount(size) {
		RingID++;
		if (ringName == "") {
			name = "Ring_" + std::to_string(RingID);
		}
		name_EMPTY = name + " Empty Slots:";
		name_FILLED = name + " Filled Slots:";

	}

	void LogRing() {
		mzEngine.WatchLog(name_EMPTY.c_str(), std::to_string(FreeCount).c_str());
		mzEngine.WatchLog(name_FILLED.c_str(), std::to_string(Size - FreeCount).c_str());
	}

	int GetNextReadable() {
		std::unique_lock lock(RingMutex);
		if (FreeCount == Size && !IsFulledOnce) {
			return -1;
		}
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
		return NextReadableFrame;
	}

	void SetConditionVariable(std::condition_variable* cv) {
		p_cv = cv;
	}

	void SetRead() {
		std::unique_lock lock(RingMutex);
		assert(FreeCount < (Size));
		FreeCount++;
		NextReadableFrame = (++NextReadableFrame % Size);
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
	}

	int GetNextWritable() {
		std::unique_lock lock(RingMutex);
		if (FreeCount == 0) {
			return -1;
		}
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
		return NextWritableFrame;
	}
	void SetWrote() {
		std::unique_lock lock(RingMutex);
		assert(FreeCount > 0);
		FreeCount--;
		NextWritableFrame = (++NextWritableFrame % Size);
		if (!IsFulledOnce && FreeCount == 0) {
			IsFulledOnce = true;
		}
		if(p_cv)
			p_cv->notify_one();
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
	}

	bool IsReadable() {
		std::unique_lock lock(RingMutex);
		if (!IsFulledOnce)
			return false;
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
		return (FreeCount < Size);
	}

	bool IsWriteable() {
		std::unique_lock lock(RingMutex);
		assert(!(FreeCount == 0 && NextReadableFrame != NextWritableFrame));
		return FreeCount > 0;
	}

	bool IsReady() {
		return IsFulledOnce;
	}

private:
	inline static int RingID;
	std::string name, name_EMPTY, name_FILLED;
	std::condition_variable* p_cv = nullptr;
	std::atomic_bool IsFulledOnce = false;
	std::mutex RingMutex;
	size_t Size;
	std::atomic_size_t FreeCount;
	std::atomic_size_t NextReadableFrame = 0;
	std::atomic_size_t NextWritableFrame = 0;
};