#include <Nodos/PluginHelpers.hpp>

// External
#include <ntv2enums.h>

#include "AJADevice.h"
#include "AJAMain.h"
#include "AJA_generated.h"
#include "Channels.h"

namespace nos::aja
{

std::string GetReferenceSourceStringListName(AJADevice& device)
{
	return "nos.aja.OutputReferenceSource:" + device.GetDisplayName();
}

std::string GetChannelListName(AJADevice& device)
{
	return "nos.aja.ChannelList:" + device.GetDisplayName();
}

struct OutputNodeContext : NodeContext
{
	OutputNodeContext(const nosFbNode* node) : NodeContext(node), CurrentChannel(this)
	{
		AJADevice::Init();
		LoadNode(node);
	}
	
	~OutputNodeContext() override
	{
		CurrentChannel.Close();
		AJADevice::Deinit();
	}

	void OnPinValueChanged(nos::Name pinName, nosUUID pinId, nosBuffer value) override
	{
		if (pinName == NOS_NAME_STATIC("FrameBufferFormat"))
		{
			auto newFbf = *static_cast<utilities::YCbCrPixelFormat*>(value.Data);
			auto info = CurrentChannel.Info;
			info.frame_buffer_format = newFbf;
			CurrentChannel.Update(std::move(info), true);
			nosEngine.LogI("Frame buffer format updated");
		}
		if (pinName == NOS_NAME_STATIC("QuadMode"))
		{
			auto newQuadMode = *static_cast<AJADevice::Mode*>(value.Data);
			auto info = CurrentChannel.Info;
			info.output_quad_link_mode = static_cast<QuadLinkMode>(newQuadMode);
			CurrentChannel.Update(std::move(info), true);
		}
	}

	void LoadNode(const fb::Node* node)
	{
		if (auto* pins = node->pins())
		{
			TChannelInfo info;
			for (auto const* pin : *pins)
			{
				auto name = pin->name()->c_str();
				if (0 == strcmp(name, "Channel"))
				{
					CurrentChannel.ChannelPinId = *pin->id();
					flatbuffers::GetRoot<ChannelInfo>(pin->data()->data())->UnPackTo(&info);
				}
			}
			CurrentChannel.Update(std::move(info), false);
		}
	}

	void OnNodeUpdated(const fb::Node* updatedNode) override
	{
		LoadNode(updatedNode);
	}
	
	void OnPinMenuRequested(nos::Name pinName, const nosContextMenuRequest* request) override
	{
	}

	void OnNodeMenuRequested(const nosContextMenuRequest* request) override
	{
		flatbuffers::FlatBufferBuilder fbb;
		std::vector<flatbuffers::Offset<nos::ContextMenuItem>> items, devices;
		EnumerateOutputChannels(fbb, devices);
		if (!devices.empty())
			items.push_back(nos::CreateContextMenuItemDirect(fbb, "Open Output Channel", 0, &devices));
		if (!items.empty())
			HandleEvent(CreateAppEvent(fbb, nos::app::CreateAppContextMenuUpdateDirect(fbb, &NodeId, request->pos(), request->instigator(), &items)));
	}

	void OnMenuCommand(nosUUID itemID, uint32_t cmd) override
	{
	    if (!cmd)
	        return;

	    AJASelectChannelCommand action = reinterpret_cast<AJASelectChannelCommand&>(cmd);
		auto device = AJADevice::GetDevice(action.DeviceIndex);
	    auto channel = action.Channel;
		auto input = action.Input;
        const bool isQuad = action.IsQuad;
        const AJADevice::Mode mode = isQuad ? (input ? AJADevice::AUTO : AJADevice::TSI) : AJADevice::SL;
        NTV2VideoFormat format = action.Format;
        if (input)
            format = device->GetInputVideoFormat(action.Channel);

        u32 width = 1920 * (1 + isQuad);
        u32 height = 1080 * (1 + isQuad);
        device->GetExtent(format, mode, width, height);

	    std::string channelName = (isQuad ? GetQuadName(channel) : ("SingleLink " + std::to_string(channel + 1)));
		{
			TChannelInfo channelPin{};
			channelPin.device = std::make_unique<TDevice>(TDevice{ {}, device->GetSerialNumber(), device->GetDisplayName() });
			channelPin.channel_name = channelName;
			channelPin.is_input = false;
	    	channelPin.is_quad = isQuad;
			channelPin.video_format = NTV2VideoFormatToString(format, true); // TODO: Readonly.
			channelPin.video_format_idx = static_cast<int>(format);
			if (isQuad)
				channelPin.output_quad_link_mode = static_cast<QuadLinkMode>(mode);
			channelPin.frame_buffer_format = utilities::YCbCrPixelFormat::YUV8;
			CurrentChannel.Update(std::move(channelPin), true);
		}
		flatbuffers::FlatBufferBuilder fbb;
	    std::vector<flatbuffers::Offset<fb::Pin>> pins;
		std::vector<fb::UUID> pinsToDelete;
		{ // Quad mode setting
	    	if (isQuad && !QuadLinkModePinId)
	    	{
	    		std::vector<u8> quadLinkModeData = nos::Buffer::From(mode);
	    		auto id = GenerateUUID();
	    		pins.push_back(nos::fb::CreatePinDirect(
					fbb, &id, (channelName + " Mode").c_str(), input ? "nos.aja.QuadLinkInputMode" : "nos.aja.QuadLinkMode",
					nos::fb::ShowAs::PROPERTY, nos::fb::CanShowAs::PROPERTY_ONLY, 0, 0, &quadLinkModeData));
	    		QuadLinkModePinId = id;
	    	}
	    	else if (!isQuad && QuadLinkModePinId)
	    	{
	    		pinsToDelete.push_back(*QuadLinkModePinId);
	    		QuadLinkModePinId = std::nullopt;
	    	}
		}
		if (!pins.empty())
			HandleEvent(CreateAppEvent(fbb, nos::CreatePartialNodeUpdateDirect(fbb, &NodeId, ClearFlags::NONE, &pinsToDelete, &pins)));
	}

	Channel CurrentChannel;
	std::optional<nosUUID> QuadLinkModePinId = std::nullopt;
};

nosResult RegisterOutputNode(nosNodeFunctions* functions)
{
	NOS_BIND_NODE_CLASS(NOS_NAME_STATIC("nos.aja.Output"), OutputNodeContext, functions)
	return NOS_RESULT_SUCCESS;
}

}