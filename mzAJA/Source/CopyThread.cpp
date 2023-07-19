
#include "CopyThread.h"
#include "AppEvents_generated.h"
#include "Ring.h"


namespace mz
{

mz::Name const& CTGetName(rc<CopyThread> const& c)
{
    return c->Name();
}

static std::set<u32> const& FindDivisors(const u32 N)
{
    static std::map<u32, std::set<u32>> Map;

    auto it = Map.find(N);
    if(it != Map.end()) 
        return it->second;

    u32 p2 = 0, p3 = 0, p5 = 0;
    std::set<u32> D;
    u32 n = N;
    while(0 == n % 2) n /= 2, p2++;
    while(0 == n % 3) n /= 3, p3++;
    while(0 == n % 5) n /= 5, p5++;
    
    for(u32 i = 0; i <= p2; ++i)
        for(u32 j = 0; j <= p3; ++j)
            for(u32 k = 0; k <= p5; ++k)
                D.insert(pow(2, i) * pow(3, j) * pow(5, k));

    static std::mutex Lock;
    Lock.lock();
    std::set<u32> const& re = (Map[N] = std::move(D));
    Lock.unlock();
    return re;
}

auto LUTFn(bool input, GammaCurve curve) -> f64 (*)(f64)
{
    switch (curve)
    {
    case GammaCurve::REC709:
    default:
        return input ? [](f64 c) -> f64 { return (c < 0.081) ? (c / 4.5) : pow((c + 0.099) / 1.099, 1.0 / 0.45); }
                     : [](f64 c) -> f64 { return (c < 0.018) ? (c * 4.5) : (pow(c, 0.45) * 1.099 - 0.099); };
    case GammaCurve::HLG:
        return input
               ? [](f64 c)
                     -> f64 { return (c < 0.5) ? (c * c / 3) : (exp(c / 0.17883277 - 5.61582460179) + 0.02372241); }
               : [](f64 c) -> f64 {
                     return (c < 1. / 12.) ? sqrt(c * 3) : (log(c - 0.02372241) * 0.17883277 + 1.00429346);
                 };
    case GammaCurve::ST2084:
        return input ? 
                [](f64 c) -> f64 { c = pow(c, 0.01268331); return pow(glm::max(c - 0.8359375f, 0.) / (18.8515625  - 18.6875 * c), 6.27739463); } : 
                [](f64 c) -> f64 { c = pow(c, 0.15930175); return pow((0.8359375 + 18.8515625 * c) / (1 + 18.6875 * c), 78.84375); };
    }
}

static std::vector<u16> GetGammaLUT(bool input, GammaCurve curve, u16 bits)
{
    std::vector<u16> re(1 << bits, 0.f);
    auto fn = LUTFn(input, curve);
    for (u32 i = 0; i < 1 << bits; ++i)
    {
        re[i] = u16(f64((1 << 16) - 1) * fn(f64(i) / f64((1 << bits) - 1)) + 0.5);
    }
    return re;
}

u32 CopyThread::GetRingSize()
{
    return CpuRing->Size;
}

mz::Name const& CopyThread::Name() const
{
    return PinName;
}

bool CopyThread::IsInput() const
{
    return PinKind == mz::fb::ShowAs::OUTPUT_PIN;
}

bool CopyThread::IsQuad() const
{
    return AJADevice::IsQuad(Mode);
}

bool CopyThread::Interlaced() const
{
    return !IsProgressivePicture(Format);
}

void CopyThread::StartThread()
{
    if (IsInput())
        Worker = std::make_unique<InputConversionThread>();
    else
        Worker = std::make_unique<OutputConversionThread>();
    Worker->Cpy = this;
    CpuRing->Exit = false;
    GpuRing->Exit = false;
    Run = true;
    Thread = std::thread([this] {
        Worker->Start();
        switch (this->PinKind)
        {
        default:
            UNREACHABLE;
        case mz::fb::ShowAs::INPUT_PIN:
            this->AJAOutputProc();
            break;
        case mz::fb::ShowAs::OUTPUT_PIN:
            this->AJAInputProc();
            break;
        }
        Worker->Stop();
    });

    flatbuffers::FlatBufferBuilder fbb;
    std::string threadName("AJA ");
    threadName += IsInput() ? "In" : "Out";
    threadName += ": " + Name().AsString();

    mzEngine.HandleEvent(
        CreateAppEvent(fbb, mz::app::CreateSetThreadNameDirect(fbb, (u64)Thread.native_handle(), threadName.c_str())));
}

mzVec2u CopyThread::Extent() const
{
    u32 width, height;
    Client->Device->GetExtent(Format, Mode, width, height);
    return mzVec2u(width, height);
}

void CopyThread::Stop()
{
    GpuRing->Stop();
    CpuRing->Stop();
    Run = false;
    if (Thread.joinable())
        Thread.join();
}

void CopyThread::Resize(u32 size)
{
    assert(size && size < 200);
    if(size == GetRingSize())
        return;
    Stop();
    CreateRings(size);
    StartThread();
}

void CopyThread::SetFrame(u32 FB)
{
    FB += 2 * Channel;
    IsInput() ? Client->Device->SetInputFrame(Channel, FB) : Client->Device->SetOutputFrame(Channel, FB);
    if (IsQuad())
    {
        for (u32 i = Channel + 1; i < Channel + 4; ++i)
        {
            IsInput() ? Client->Device->SetInputFrame(NTV2Channel(i), FB)
                      : Client->Device->SetOutputFrame(NTV2Channel(i), FB);
        }
    }
}

#define SSBO_SIZE 10

void CopyThread::UpdateCurve(enum GammaCurve curve)
{
    GammaCurve = curve;
    auto data = GetGammaLUT(IsInput(), GammaCurve, SSBO_SIZE);
    auto ptr = mzEngine.Map(&SSBO);
    memcpy(ptr, data.data(), data.size() * sizeof(data[0]));
}

std::array<f64, 2> CopyThread::GetCoeffs() const
{
    switch (Colorspace)
    {
    case Colorspace::REC601:
        return {.299, .114};
    case Colorspace::REC2020:
        return {.2627, .0593};
    case Colorspace::REC709:
    default:
        return {.2126, .0722};
    }
}

template<class T>
glm::mat<4,4,T> CopyThread::GetMatrix() const
{
    // https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#MODEL_CONVERSION
    const auto [R, B] = GetCoeffs();
    const T G = T(1) - R - B; // Colorspace

    /*
    * https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#QUANTIZATION_NARROW
        Dequantization:
            n = Bit Width {8, 10, 12}
            Although unnoticable, quantization scales differs between bit widths
            This is merely mathematical perfection the error terms is less than 0.001
    */

    const T QuantizationScalar = T(1 << (BitWidth() - 8)) / T((1 << BitWidth()) - 1);
    const T Y  = NarrowRange ? 219 * QuantizationScalar : 1;
    const T C  = NarrowRange ? 224 * QuantizationScalar : 1;
    const T YT = NarrowRange ? 16 * QuantizationScalar : 0;
    const T CT = 128 * QuantizationScalar;
    const T CB = .5 * C / (B - 1);
    const T CR = .5 * C / (R - 1);

    const auto V0 = glm::vec<3,T>(R, G, B);
    const auto V1 = V0 - glm::vec<3,T>(0, 0, 1);
    const auto V2 = V0 - glm::vec<3,T>(1, 0, 0);

    return glm::transpose(glm::mat<4,4,T>( 
            glm::vec<4,T>(Y  * V0, YT), 
            glm::vec<4,T>(CB * V1, CT), 
            glm::vec<4,T>(CR * V2, CT), 
            glm::vec<4,T>(0, 0, 0,  1)));
}

float GetFrameDurationFromFrameRate(auto frameRate)
{

    switch (frameRate)
    {
    case NTV2_FRAMERATE_6000  : return 1.f / 60.f;
    case NTV2_FRAMERATE_5994  : return 1001.f / 60000.f;
    case NTV2_FRAMERATE_3000  : return 1.f / 30.f;
	case NTV2_FRAMERATE_2997  :  return 1001.f / 30000.f;
	case NTV2_FRAMERATE_2500  :  return 1.f / 25.f;
	case NTV2_FRAMERATE_2400  :  return 1.f / 24.f;
	case NTV2_FRAMERATE_2398  :  return 1001.f / 24000.f;
	case NTV2_FRAMERATE_5000  :  return 1.f / 50.f;
	case NTV2_FRAMERATE_4800  :  return 1.f / 48.f;
	case NTV2_FRAMERATE_4795  :  return 1001.f / 48000.f;
	case NTV2_FRAMERATE_12000 :  return 1.f / 120.f;
	case NTV2_FRAMERATE_11988 : return 1001.f / 120000.f;
	case NTV2_FRAMERATE_1500  :  return 1.f / 15.f;
	case NTV2_FRAMERATE_1498  :  return 1001.f / 15000.f;
    default : return .02f;
    }
}

void CopyThread::Refresh()
{
    Client->Device->CloseChannel(Channel, Client->Input, IsQuad());
    Client->Device->RouteSignal(Channel, Format, Client->Input, Mode, Client->FBFmt());
    Format = IsInput() ? Client->Device->GetInputVideoFormat(Channel) : Format;
    Client->Device->SetRegisterWriteMode(Interlaced() ? NTV2_REGWRITE_SYNCTOFIELD : NTV2_REGWRITE_SYNCTOFRAME, Channel);

    CreateRings(GetRingSize());
}

void CopyThread::CreateRings(u32 size)
{
	const auto ext = Extent();
    
    if (CompressedTex.Memory.Handle)
        mzEngine.Destroy(&CompressedTex);
    
	GpuRing = MakeShared<GPURing>(ext, size);
	mzVec2u compressedExt((10 == BitWidth()) ? ((ext.x + (48 - ext.x % 48) % 48) / 3) << 1 : ext.x >> 1, ext.y >> u32(Interlaced()));
	CpuRing = MakeShared<CPURing>(compressedExt, size);
    CompressedTex.Info.Type = MZ_RESOURCE_TYPE_TEXTURE;
    CompressedTex.Info.Texture.Width = compressedExt.x;
    CompressedTex.Info.Texture.Height = compressedExt.y;
    CompressedTex.Info.Texture.Format = MZ_FORMAT_R8G8B8A8_UINT;
    //CompressedTex.unscaled = true;
    //CompressedTex.unmanaged = true;
    mzEngine.Create(&CompressedTex);
}

void CopyThread::InputUpdate(AJADevice::Mode &prevMode)
{
    if (Client->Device->GetInputVideoFormat(Channel) != Format)
    {
        Refresh();
    }

    if (Mode == AJADevice::AUTO)
    {
        auto curMode = Client->Device->GetMode(Channel);
        if (prevMode != curMode)
        {
            prevMode = curMode;
            Refresh();
        }
    }

#pragma push_macro("Q")
#pragma push_macro("R")
#define Q(N)                                                                                                           \
    case N: {                                                                                                          \
        reg = kRegRXSDI##N##FrameCountLow; /* Is only getting the low reg enough? */                                   \
        break;                                                                                                         \
    }
#define R(C)                                                                                                           \
    switch (C + 1)                                                                                                     \
    {                                                                                                                  \
        Q(1) Q(2) Q(3) Q(4) Q(5) Q(6) Q(7) Q(8)                                                                        \
    }
    NTV2RXSDIStatusRegister reg;
    R(Channel);
#pragma pop_macro("Q")
#pragma pop_macro("R")

    u32 val;
    Client->Device->ReadRegister(reg, val);
    FrameIDCounter.store(val);
}

void CopyThread::AJAInputProc()
{
    PathRestart();
    Orphan(false);
    {
        std::stringstream ss;
        ss << "AJAIn Thread: " << std::this_thread::get_id();
        mzEngine.LogI(ss.str().c_str());
    }

    auto prevMode = Client->Device->GetMode(Channel);

    u32 FB = 0;
    SetFrame(FB);
    Client->Device->WaitForInputVerticalInterrupt(Channel);

    Parameters params = {};
    DebugInfo.Time = std::chrono::nanoseconds(0);
    DebugInfo.Counter = 0;

    DropCount = 0;
    u32 framesSinceLastDrop = 0;

    while (Run && !GpuRing->Exit)
    {
        auto cpuRingReadSize = CpuRing->Read.Pool.size();
        auto gpuRingWriteSize = GpuRing->Write.Pool.size();
        auto cpuRingWriteSize = CpuRing->Write.Pool.size();
        auto gpuRingReadSize = GpuRing->Read.Pool.size();

        auto strCpuRingReadSize = std::to_string(cpuRingReadSize);
        auto strCpuRingWriteSize = std::to_string(cpuRingWriteSize);
        auto strGpuRingWriteSize = std::to_string(gpuRingWriteSize);
        auto strGpuRingReadSize = std::to_string(gpuRingReadSize);

        mzEngine.WatchLog("AJAIn CPU Ring Read Size", strCpuRingReadSize.c_str());
        mzEngine.WatchLog("AJAIn CPU Ring Write Size", strCpuRingWriteSize.c_str());
        mzEngine.WatchLog("AJAIn GPU Ring Read Size", strGpuRingReadSize.c_str());
        mzEngine.WatchLog("AJAIn GPU Ring Write Size", strGpuRingWriteSize.c_str());
        InputUpdate(prevMode);

        if (!(Client->Device->WaitForInputVerticalInterrupt(Channel)))
        {
            Orphan(true, "AJA Input has no signal");
            while (!Client->Device->WaitForInputVerticalInterrupt(Channel))
            {
                if (!Run || GpuRing->Exit || CpuRing->Exit)
                {
                    goto EXIT;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                InputUpdate(prevMode);
            }
            InputUpdate(prevMode);
            Orphan(false);
        }

        int ringSize = GetRingSize();
        int inFlight = InFlightFrames();
        
        CPURing::Resource* slot = nullptr;
        if (inFlight >= ringSize || (!(slot = CpuRing->TryPush())))
        {
            DropCount++;
            framesSinceLastDrop = 0;
            GpuRing->ResetFrameCount = true;
            continue;
        }

        const u32 Pitch = CompressedTex.Info.Texture.Width * 4;
        const u32 Segments = CompressedTex.Info.Texture.Height;
        ULWord *Buf = (ULWord *)mzEngine.Map(&slot->Res);
        const u32 Size = slot->Res.Info.Buffer.Size;
        const u32 ReadFB = 2 * Channel + FB;
        params.T0 = Clock::now();
        if (Interlaced())
        {
            NTV2FieldID field = NTV2_FIELD0;
            Client->Device->GetInputFieldID(Channel, field);
            params.FieldIdx = field + 1;
            u64 addr, length;
            Client->Device->GetDeviceFrameInfo(ReadFB, Channel, addr, length);
            Client->Device->DMAReadSegments(0, Buf, addr + Pitch * field, Pitch, Segments, Pitch, Pitch * 2);
        }
        else
        {
            Client->Device->DMAReadFrame(ReadFB, Buf, Size, Channel);
        }
        CpuRing->EndPush(slot);
        framesSinceLastDrop++;
        if (DropCount && framesSinceLastDrop == 50)
        {
            PathRestart();
        }
            
        NTV2RegisterReads Regs = { NTV2RegInfo(kRegRXSDI1FrameCountLow + Channel * (kRegRXSDI2FrameCountLow - kRegRXSDI1FrameCountLow)) };
        Client->Device->ReadRegisters(Regs);
        params.FrameNumber = Regs.front().registerValue;

        if (!Interlaced())
        {
            SetFrame(FB);
            FB ^= 1;
        }

        params.T1 = Clock::now();

        Worker->Enqueue(params);
    }
EXIT:

    CpuRing->Stop();
    GpuRing->Stop();

    if (Run)
    {
        SendDeleteRequest();
    }
}

mzVec2u CopyThread::GetSuitableDispatchSize() const
{
    constexpr auto BestFit = [](i64 val, i64 res) -> u32 {
        auto d = FindDivisors(res);
        auto it = d.upper_bound(val);
        if (it == d.begin())
            return *it;
        if (it == d.end())
            return res;
        const i64 hi = *it;
        const i64 lo = *--it;
        return u32(abs(val - lo) < abs(val - hi) ? lo : hi);
    };

    const u32 q = IsQuad();
    f32 x = glm::clamp<u32>(Client->DispatchSizeX.load(), 1, CompressedTex.Info.Texture.Width) * (1 + q) * (.25 * BitWidth() - 1);
    f32 y = glm::clamp<u32>(Client->DispatchSizeY.load(), 1, CompressedTex.Info.Texture.Height) * (1. + q) * (1 - .5 * Interlaced());

    return mzVec2u(BestFit(x + .5, CompressedTex.Info.Texture.Width >> (BitWidth() - 5)),
                     BestFit(y + .5, CompressedTex.Info.Texture.Height / 9));
}

void CopyThread::PathRestart()
{
    auto id = Client->GetPinId(mz::Name(Name()));
    auto cmdType = MZ_PATH_COMMAND_TYPE_RESTART;
    auto execType = MZ_PATH_COMMAND_EXECUTION_TYPE_WALKBACK;

    if(IsInput())
    {
        cmdType = MZ_PATH_COMMAND_TYPE_NOTIFY_DROP;
        execType = MZ_PATH_COMMAND_EXECUTION_TYPE_NOTIFY_ALL_CONNECTIONS;
        return;
    }

    auto args = Buffer::From(AjaRestartCommandParams{
        .UpdateFlags = u32(AjaRestartCommandParams::UpdateRingSize | AjaRestartCommandParams::UpdateSpareCount),
        .RingSize = GpuRing->Size,
        .SpareCount = SpareCount,
    });

    mzEngine.SendPathCommand(mzPathCommand{
        .PinId = id,
        .Command = (mzPathCommandType)cmdType,
        .Execution = (mzPathCommandExecutionType)execType,
        .Args = mzBuffer { args.data(), args.size() }
    });
}

u32 CopyThread::InFlightFrames()
{
    return CpuRing->InFlightFrames() + GpuRing->InFlightFrames() - (TransferInProgress ? 1 : 0);
}

void CopyThread::AJAOutputProc()
{
   
	flatbuffers::FlatBufferBuilder fbb;
    auto id = Client->GetPinId(PinName);
	auto hungerSignal = CreateAppEvent(fbb, mz::app::CreateScheduleRequest(fbb, mz::app::ScheduleRequestKind::PIN, &id, false));
    mzEngine.HandleEvent(hungerSignal);
    Orphan(false);
    {
        std::stringstream ss;
        ss << "AJAOut Thread: " << std::this_thread::get_id();
        mzEngine.LogI(ss.str().c_str());
    }

    u32 readyFrames = CpuRing->ReadyFrames();
    do
    {
        u32 latestReadyFrameCount = CpuRing->ReadyFrames();
        if (latestReadyFrameCount > readyFrames)
            readyFrames = latestReadyFrameCount;
    } while (Run && !CpuRing->Exit && !(CpuRing->IsFull()));

    u32 FB = 0;
    SetFrame(FB);

    DropCount = 0;
	u32 framesSinceLastDrop = 0;

    while (Run && !CpuRing->Exit)
    {
        auto cpuRingReadSize = CpuRing->Read.Pool.size();
        auto gpuRingWriteSize = GpuRing->Write.Pool.size();
        auto cpuRingWriteSize = CpuRing->Write.Pool.size();
        auto gpuRingReadSize = GpuRing->Read.Pool.size();

        auto strCpuRingReadSize = std::to_string(cpuRingReadSize);
        auto strCpuRingWriteSize = std::to_string(cpuRingWriteSize);
        auto strGpuRingWriteSize = std::to_string(gpuRingWriteSize);
        auto strGpuRingReadSize = std::to_string(gpuRingReadSize);

        mzEngine.WatchLog("AJAOut CPU Ring Read Size", strCpuRingReadSize.c_str());
        mzEngine.WatchLog("AJAOut CPU Ring Write Size", strCpuRingWriteSize.c_str());
        mzEngine.WatchLog("AJAOut GPU Ring Read Size", strGpuRingReadSize.c_str());
        mzEngine.WatchLog("AJAOut GPU Ring Write Size", strGpuRingWriteSize.c_str());
		if (!(Client->Device->WaitForOutputVerticalInterrupt(Channel)))
			break;

        ULWord lastVBLCount;
		Client->Device->GetOutputVerticalInterruptCount(lastVBLCount, Channel);

		if (auto res = CpuRing->BeginPop())
        {
			ULWord vblCount;
			Client->Device->GetOutputVerticalInterruptCount(vblCount, Channel);
			
            if (vblCount != lastVBLCount)
            {
				framesSinceLastDrop = 0;
				DropCount += vblCount - lastVBLCount;
            }
			else
			{
				if (++framesSinceLastDrop == 50)
				{
					PathRestart();
				}
			}

			u32 FieldIdx = 0;
            if (Interlaced())
            {
                NTV2FieldID field = NTV2_FIELD0;
                Client->Device->GetOutputFieldID(Channel, field);
                field = NTV2FieldID(field ^ 1);
                FieldIdx = field + 1;
            }
            const ULWord *Buf = (ULWord *)mzEngine.Map(&res->Res);

            const u32 OutFrame = 2 * Channel + FB;

			const u32 Pitch = CompressedTex.Info.Texture.Width * 4;
			const u32 Segments = CompressedTex.Info.Texture.Height;
			const u32 Size = CpuRing->Sample.Size;

            if (Interlaced())
            {
                u64 addr, length;
                Client->Device->GetDeviceFrameInfo(OutFrame, Channel, addr, length);
                Client->Device->DMAWriteSegments(0, Buf, addr + (FieldIdx - 1) * Pitch, Pitch, Segments, Pitch, Pitch * 2);
            }
            else
            {
                Client->Device->DMAWriteFrame(OutFrame, Buf, Pitch * Segments, Channel);
            }

            CpuRing->EndPop(res);
			mzEngine.HandleEvent(hungerSignal);

            if (!Interlaced())
            {
                SetFrame(FB);
                FB ^= 1;
            }
        }
        else
        {
            mzEngine.LogW((Name().AsString() + " dropped 1 frame").c_str(), "");
        }
    }
    GpuRing->Stop();
    CpuRing->Stop();

    mzEngine.HandleEvent(CreateAppEvent(fbb, 
        mz::app::CreateScheduleRequest(fbb, mz::app::ScheduleRequestKind::PIN, &id, true)));

    if (Run)
        SendDeleteRequest();
}

void CopyThread::SendDeleteRequest()
{
    flatbuffers::FlatBufferBuilder fbb;
    auto ids = Client->GeneratePinIDSet(mz::Name(Name()), Mode);
    mzEngine.HandleEvent(
        CreateAppEvent(fbb, mz::CreatePartialNodeUpdateDirect(fbb, &Client->Mapping.NodeId, ClearFlags::NONE, &ids)));
}

void CopyThread::InputConversionThread::Consume(CopyThread::Parameters const& params)
{
    auto* slot = Cpy->CpuRing->BeginPop();
    if (!slot)
        return;
    Cpy->TransferInProgress = true;
    auto* res = Cpy->GpuRing->BeginPush();
    if (!res)
    {
        Cpy->CpuRing->EndPop(slot);
        return;
    }

    std::vector<mzShaderBinding> inputs;

    glm::mat4 colorspace = glm::inverse(Cpy->GetMatrix<f64>());

    uint32_t iflags = params.FieldIdx | ((Cpy->Client->Shader == ShaderType::Comp10) << 2);

    inputs.emplace_back(ShaderBinding(MZN_Colorspace, colorspace));
	inputs.emplace_back(ShaderBinding(MZN_Source, Cpy->CompressedTex));
	inputs.emplace_back(ShaderBinding(MZN_Interlaced, iflags));
	inputs.emplace_back(ShaderBinding(MZN_ssbo, Cpy->SSBO));

    auto MsgKey = "Input " + Cpy->Name().AsString() + " DMA";

    mzCmd cmd;
    mzEngine.Begin(&cmd);

    mzEngine.Copy(cmd, &slot->Res, &Cpy->CompressedTex, Cpy->Client->Debug ? ("(GPUTransfer)" + MsgKey + ":" + std::to_string(Cpy->Client->Debug)).c_str() : 0);

    if (Cpy->Client->Shader != ShaderType::Frag8)
    {
		inputs.emplace_back(ShaderBinding(MZN_Output, res->Res));
        mzRunComputePassParams pass = {};
		pass.Key = MZN_AJA_YCbCr2RGB_Compute_Pass;
        pass.DispatchSize = Cpy->GetSuitableDispatchSize();
        pass.Bindings = inputs.data();
        pass.BindingCount = inputs.size();
        pass.Benchmark = Cpy->Client->Debug;
        mzEngine.RunComputePass(cmd, &pass);
    }
    else
    {
        mzRunPassParams pass = {};
		pass.Key = MZN_AJA_YCbCr2RGB_Pass;
        pass.Output = res->Res;
        pass.Bindings = inputs.data();
        pass.BindingCount = inputs.size();
        pass.Benchmark = Cpy->Client->Debug;
        mzEngine.RunPass(cmd, &pass);
    }

    mzEngine.End(cmd);

    auto& [time, counter] = Cpy->DebugInfo;
    if (Cpy->Client->Debug && ++counter >= Cpy->Client->Debug)
    {
        time += params.T1 - params.T0;
        auto t = time / counter;
        counter = 0;
        time = std::chrono::nanoseconds(0);
        std::stringstream ss;
        ss << "(AJATransfer)" << MsgKey << " took: " << t << " (" << std::chrono::duration_cast<Micro>(t) << ")"
            << " (" << std::chrono::duration_cast<Milli>(t) << ")"
            << "\n";
        mzEngine.LogI(ss.str().c_str());
    }

    //if(Cpy->client->Debug)
    //{
    //    auto tmp = res->Res;
    //    mzEngine.Create(tmp);
    //    app::TCopyResource cpy;
    //    cpy.src.Set(res->Res);
    //    cpy.dst.Set(tmp);
    //    app::TRunPass2 pass;
    //    pass.pass = "$$GPUJOBPASS$$mz.SevenSegment";
    //    pass.draws.push_back(std::make_unique<mz::app::TDrawCall>());
    //    pass.draws.back()->inputs.emplace_back(new app::TShaderBinding { .var = "Color", .val = mz::Buffer::From(glm::vec4(1))});
    //    pass.draws.back()->inputs.emplace_back(new app::TShaderBinding { .var = "SampleInput", .val = mz::Buffer::From(1)});
    //    pass.draws.back()->inputs.emplace_back(new app::TShaderBinding { .var = "RenderFrameNo", .val = mz::Buffer::From(0)});
    //
    //    pass.draws.back()->inputs.emplace_back(new app::TShaderBinding { .var = "Number", .val = mz::Buffer::From(params.FrameNumber)});
    //    pass.draws.back()->inputs.emplace_back(new app::TShaderBinding { .var = "Input", .val = mz::Buffer::From(tmp)});
    //    pass.output = std::make_unique<mz::fb::TTexture>(res->Res);
    //    mzEngine.MakeAPICalls(true, cpy, pass);
    //    mzEngine.Destroy(tmp);
    //}

    // res->Res.field_type = fb::FieldType::ANY ^ fb::FieldType(params.FieldIdx ^ 3);
    // std::this_thread::sleep_for(std::chrono::milliseconds(1));
    res->FrameNumber = params.FrameNumber;
    Cpy->GpuRing->EndPush(res);
    Cpy->TransferInProgress = false;
    Cpy->CpuRing->EndPop(slot);
}

CopyThread::ConversionThread::~ConversionThread()
{
    Stop();
}

void CopyThread::OutputConversionThread::Consume(const Parameters& item)
{
#if 0 // TODO: discard according to compatibility
    if (!(res->res.field_type & fb::FieldType(FieldIdx)))
    {
        GpuRing->EndPop(res);
        continue;
    }
#endif
    auto incoming = Cpy->GpuRing->BeginPop();
    if(!incoming)
		return;
    Cpy->TransferInProgress = true;
    auto outgoing = Cpy->CpuRing->BeginPush();
    if (!outgoing)
    {
        Cpy->GpuRing->EndPop(incoming);
        return;
    }
    glm::mat4 colorspace = (Cpy->GetMatrix<f64>());
    uint32_t iflags = (Cpy->Client->Shader == ShaderType::Comp10) << 2;

    std::vector<mzShaderBinding> inputs;
	inputs.emplace_back(ShaderBinding(MZN_Colorspace, colorspace));
	inputs.emplace_back(ShaderBinding(MZN_Source, incoming->Res));
	inputs.emplace_back(ShaderBinding(MZN_Interlaced, iflags));
	inputs.emplace_back(ShaderBinding(MZN_ssbo, Cpy->SSBO));

    mzCmd cmd;
    mzEngine.Begin(&cmd);

    // watch out for th members, they are not synced
    if (Cpy->Client->Shader != ShaderType::Frag8)
    {
		inputs.emplace_back(ShaderBinding(MZN_Output, Cpy->CompressedTex));
        mzRunComputePassParams pass = {};
		pass.Key = MZN_AJA_RGB2YCbCr_Compute_Pass;
        pass.DispatchSize = Cpy->GetSuitableDispatchSize();
        pass.Bindings = inputs.data();
        pass.BindingCount = inputs.size();
        pass.Benchmark = Cpy->Client->Debug;
        mzEngine.RunComputePass(cmd, &pass);
    }
    else
    {
        mzRunPassParams pass = {};
		pass.Key = MZN_AJA_RGB2YCbCr_Pass;
        pass.Output = Cpy->CompressedTex;
        pass.Bindings = inputs.data();
        pass.BindingCount = inputs.size();
        pass.Benchmark = Cpy->Client->Debug;
        mzEngine.RunPass(cmd, &pass);
    }

    mzEngine.Copy(cmd, &Cpy->CompressedTex, &outgoing->Res, 0);
    mzEngine.End(cmd);
    Cpy->GpuRing->EndPop(incoming);
    Cpy->TransferInProgress = false;
    Cpy->CpuRing->EndPush(outgoing);
}

CopyThread::CopyThread(struct AJAClient *client, u32 ringSize, u32 spareCount, mz::fb::ShowAs kind, 
                       NTV2Channel channel, NTV2VideoFormat initalFmt,
                       AJADevice::Mode mode, enum class Colorspace colorspace, enum class GammaCurve curve,
                       bool narrowRange, const fb::Texture* tex)
    : PinName(GetChannelStr(channel, mode)), Client(client), PinKind(kind), Channel(channel), SpareCount(spareCount), Mode(mode),
      Colorspace(colorspace), GammaCurve(curve), NarrowRange(narrowRange), Format(initalFmt)
{

    {
        SSBO.Info.Type = MZ_RESOURCE_TYPE_BUFFER;
        SSBO.Info.Buffer.Size = (1<<(SSBO_SIZE)) * sizeof(u16);
        SSBO.Info.Buffer.Usage = MZ_BUFFER_USAGE_STORAGE_BUFFER; // | MZ_BUFFER_USAGE_DEVICE_MEMORY;
        mzEngine.Create(&SSBO);
        UpdateCurve(GammaCurve);
    }

    if(IsInput())
    {
        Format = client->Device->GetInputVideoFormat(channel);
    }

    client->Device->SetRegisterWriteMode(Interlaced() ? NTV2_REGWRITE_SYNCTOFIELD : NTV2_REGWRITE_SYNCTOFRAME, Channel);

    CreateRings(ringSize);
    StartThread();
}

CopyThread::~CopyThread()
{
    Stop();
    Client->Device->CloseChannel(Channel, IsInput(), IsQuad());
    mzEngine.Destroy(&SSBO);
    if (CompressedTex.Memory.Handle)
        mzEngine.Destroy(&CompressedTex);
}

void CopyThread::Orphan(bool orphan, std::string const& message)
{
    PinUpdate(mz::fb::TOrphanState{.is_orphan=orphan, .message=message}, Action::NOP);
}

void CopyThread::Live(bool b)
{
    PinUpdate(std::nullopt, b ? Action::SET : Action::RESET);
}

void CopyThread::PinUpdate(std::optional<mz::fb::TOrphanState> orphan, mz::Action live)
{
    flatbuffers::FlatBufferBuilder fbb;
    auto ids = Client->GeneratePinIDSet(mz::Name(Name()), Mode);
    std::vector<flatbuffers::Offset<PartialPinUpdate>> updates;
    std::transform(ids.begin(), ids.end(), std::back_inserter(updates),
                   [&fbb, orphan, live](auto id) { return mz::CreatePartialPinUpdateDirect(fbb, &id, 0, orphan ? mz::fb::CreateOrphanState(fbb, &*orphan) : false, live); });
    mzEngine.HandleEvent(
        CreateAppEvent(fbb, mz::CreatePartialNodeUpdateDirect(fbb, &Client->Mapping.NodeId, ClearFlags::NONE, 0, 0, 0,
                                                              0, 0, 0, 0, &updates)));
}

u32 CopyThread::BitWidth() const
{
    return Client->BitWidth();
}

} // namespace mz
