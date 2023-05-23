/*
 * Copyright MediaZ AS. All Rights Reserved.
 */

#pragma once

#include <ajabase/common/types.h>
#include <ajabase/system/memory.h>
#include <ajabase/system/process.h>
#include <ajabase/common/timecodeburn.h>

#include <ajantv2/includes/ntv2card.h>
#include <ajantv2/includes/ntv2enums.h>
#include <ajantv2/includes/ntv2rp188.h>
#include <ajantv2/includes/ntv2utils.h>
#include <ajantv2/includes/ntv2devicescanner.h>
#include <ajantv2/includes/ntv2signalrouter.h>

#include "ntv2devicefeatures.h"
#include "ntv2devicefeatures.hh"
#include "ntv2enums.h"
#include "ntv2publicinterface.h"
#include "ntv2signalrouter.h"
#include "ntv2utils.h"
#include "ntv2vpid.h"
#include "ntv2enums.h"
#include "ntv2utils.h"

#define AJA_ASSERT(x) { if(!(x)) { printf("%s:%d\n", __FILE__, __LINE__); abort();} }

struct AJADevice : CNTV2Card
{
    enum Mode: uint32_t
    {
        TSI,
        SQD,
        AUTO,
        SL,
    };

    static bool IsQuad(Mode mode) 
    {
        switch(mode)
        {
            case SL: return false;
            default: return true;
        }
    }
    
    inline static std::vector<std::shared_ptr<AJADevice>> Devices;

    NTV2FrameRate FPSFamily = NTV2_FRAMERATE_INVALID;

    NTV2DeviceID ID;

    std::set<NTV2Channel> Channels;

    std::atomic_bool HasInput = false;
    std::atomic_bool HasOutput = false;

    static void Dealloc(void* ptr, size_t size);
    static void* Alloc(size_t size, bool);
    static std::map<std::string, uint64_t>  EnumerateDevices();
    inline static const auto AvailableDevices = EnumerateDevices();
    static uint64_t FindDeviceSerial(const char* ident);
    static bool DeviceAvailable(const char* ident, bool input);
    static bool GetAvailableDevice(bool input, AJADevice** = 0);
    static void Init();
    static void Deinit();
    static std::shared_ptr<AJADevice> GetDevice(std::string const& name);
    static std::shared_ptr<AJADevice> GetDevice(uint32_t index);

    static NTV2ReferenceSource ChannelToRefSrc(NTV2Channel channel)
    {
        return NTV2InputSourceToReferenceSource(NTV2ChannelToInputSource(channel, NTV2_INPUTSOURCES_SDI));
    }
    
    CNTV2VPID GetVPID(NTV2Channel channel, CNTV2VPID* B = 0);

    NTV2VideoFormat GetInputVideoFormat2(NTV2Channel channel);
    NTV2VideoFormat GetInputVideoFormat(NTV2Channel channel);
    bool IsTSI(NTV2Channel channel);
    Mode GetMode(NTV2Channel channel);
    ~AJADevice();
    AJADevice(uint64_t serial);
    bool ChannelIsValid(NTV2Channel channel, bool isInput, NTV2VideoFormat fmt, Mode mode);
private:
    bool RouteSLInputSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, NTV2FrameBufferFormat fbFmt);
    bool RouteSLOutputSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, NTV2FrameBufferFormat fbFmt);

    bool RouteQuadInputSignal (NTV2Channel channel, NTV2VideoFormat videoFmt, Mode mode, NTV2FrameBufferFormat fbFmt);
    bool RouteQuadOutputSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, Mode mode, NTV2FrameBufferFormat fbFmt);

    bool ChannelCanInput (NTV2Channel channel);
    bool ChannelCanOutput(NTV2Channel channel);

    bool CanMakeQuadInputFromChannel (NTV2Channel channel);
    bool CanMakeQuadOutputFromChannel(NTV2Channel channel);

    bool RouteInputSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, Mode mode, NTV2FrameBufferFormat fbFmt)
    {
        return (mode != SL) ? RouteQuadInputSignal(channel, videoFmt, mode, fbFmt) : RouteSLInputSignal(channel, videoFmt, fbFmt);
    }

    bool RouteOutputSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, Mode mode, NTV2FrameBufferFormat fbFmt)
    {
        return (mode != SL) ? RouteQuadOutputSignal(channel, videoFmt, mode, fbFmt) : RouteSLOutputSignal(channel, videoFmt, fbFmt);
    }


    void CloseSLChannel(NTV2Channel channel, bool isInput);
    void CloseQLChannel(NTV2Channel channel, bool isInput);

public:
    bool RouteSignal(NTV2Channel channel, NTV2VideoFormat videoFmt, bool isInput, Mode mode, NTV2FrameBufferFormat fbFmt);

    
    bool SetRef(NTV2Channel channel);

    void CloseChannel(NTV2Channel channel, bool isInput, bool isQuad);

    void ClearState();

    uint32_t GetFBSize(NTV2Channel channel);
    uint32_t GetIntrinsicSize();

    bool GetExtent(NTV2Channel channel, Mode mode, uint32_t& width, uint32_t& height);
    bool GetExtent(NTV2VideoFormat fmt, Mode mode, uint32_t& width, uint32_t& height);

    void GetReferenceAndFrameRate(NTV2ReferenceSource& reference, NTV2FrameRate& framerate);
};