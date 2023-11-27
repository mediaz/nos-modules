// Copyright MediaZ AS. All Rights Reserved.

#include <MediaZ/Helpers.hpp>

// External
#include <stb_image.h>
#include <stb_image_write.h>

// Framework
#include <Args.h>
#include <Builtins_generated.h>
#include <AppService_generated.h>

// mzNodes
#include "../Shaders/SRGB2Linear.frag.spv.dat"

namespace mz::utilities
{

MZ_REGISTER_NAME(SRGB2Linear_Pass);
MZ_REGISTER_NAME(SRGB2Linear_Shader);
MZ_REGISTER_NAME(Path);
MZ_REGISTER_NAME(Out);
MZ_REGISTER_NAME_SPACED(Mz_Utilities_ReadImage, "mz.utilities.ReadImage")

static mzResult GetShaders(size_t* outCount, mzShaderInfo* outShaders)
{
    *outCount = 1;
    if (!outShaders)
        return MZ_RESULT_SUCCESS;

	outShaders[0] = {.Key=MZN_SRGB2Linear_Shader, .Source = { .SpirvBlob = {(void*)SRGB2Linear_frag_spv, sizeof(SRGB2Linear_frag_spv)}}};
    return MZ_RESULT_SUCCESS;
}

static mzResult GetPasses(size_t* count, mzPassInfo* passes)
{
	*count = 1;
	if (!passes)
		return MZ_RESULT_SUCCESS;

	*passes = mzPassInfo{
		.Key = MZN_SRGB2Linear_Pass,
		.Shader = MZN_SRGB2Linear_Shader,
		.Blend = 0,
		.MultiSample = 1,
	};

	return MZ_RESULT_SUCCESS;
}

static mzResult GetFunctions(size_t* count, mzName* names, mzPfnNodeFunctionExecute* fns)
{
    *count = 1;
    if(!names || !fns)
        return MZ_RESULT_SUCCESS;
    
    *names = MZ_NAME_STATIC("ReadImage_Load");
    *fns = [](void* ctx, const mzNodeExecuteArgs* nodeArgs, const mzNodeExecuteArgs* functionArgs)
    {
        auto values = GetPinValues(nodeArgs);
		auto ids = GetPinIds(nodeArgs);
		std::filesystem::path path = GetPinValue<const char>(values, MZN_Path);
		try
		{
			if (!std::filesystem::exists(path))
			{
				mzEngine.LogE("Read Image cannot load file %s", path.string().c_str());
				return;
			}
			mzResourceShareInfo out = DeserializeTextureInfo(GetPinValue<void>(values, MZN_Out));
			mzResourceShareInfo tmp = out;
			
			int w, h, n;
			u8* img = stbi_load(path.string().c_str(), &w, &h, &n, 4);
			mzEngine.ImageLoad(img, mzVec2u(w,h), MZ_FORMAT_R8G8B8A8_SRGB, &tmp);
			free(img);

			mzCmd cmd;
			mzEngine.Begin(&cmd);
			mzEngine.Copy(cmd, &tmp, &out, 0);
			mzEngine.End(cmd);
			mzEngine.Destroy(&tmp);

			flatbuffers::FlatBufferBuilder fbb;
			auto dirty = CreateAppEvent(fbb, app::CreatePinDirtied(fbb, &ids[MZN_Out]));
			mzEngine.EnqueueEvent(&dirty);
		}
		catch(const std::exception& e)
		{
			mzEngine.LogE("Error while loading image: %s", e.what());
		}
    };
    
    return MZ_RESULT_SUCCESS;
}


void RegisterReadImage(mzNodeFunctions* fn)
{
	fn->TypeName = MZN_Mz_Utilities_ReadImage;
	fn->GetFunctions = GetFunctions;
    fn->GetShaders = GetShaders;
    fn->GetPasses = GetPasses;
}

// void RegisterReadImage(mzNodeFunctions* fn)
// {
	// auto& actions = functions["mz.ReadImage"];

	// actions.NodeCreated = [](fb::Node const& node, Args& args, void** context) {
	// 	*context = new ReadImageContext(node);
	// };

	// actions.EntryPoint = [](mz::Args& args, void* context) mutable {
	// 	auto path = args.Get<char>("Path");
	// 	if (!path || strlen(path) == 0)
	// 		return false;

	// 	i32 width, height, channels;
	// 	auto* ctx = static_cast<ReadImageContext*>(context);
	// 	u8* img = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
	// 	bool ret = !!img && ctx->Upload(img, width, height, args.GetBuffer("Out"));
	// 	if (!ret)
	// 	{
	// 		mzEngine.LogE("ReadImage: Failed to load image");
	// 		flatbuffers::FlatBufferBuilder fbb;
	// 		std::vector<flatbuffers::Offset<mz::fb::NodeStatusMessage>> messages{mz::fb::CreateNodeStatusMessageDirect(
	// 			fbb, "Failed to load image", mz::fb::NodeStatusMessageType::FAILURE)};
	// 		HandleEvent(CreateAppEvent(
	// 			fbb,
	// 			mz::CreatePartialNodeUpdateDirect(fbb, &ctx->NodeId, ClearFlags::NONE, 0, 0, 0, 0, 0, 0, &messages)));
	// 	}
	// 	if (img)
	// 		stbi_image_free(img);

	// 	return ret;
	// };

	// actions.NodeRemoved = [](void* ctx, mz::fb::UUID const& id) { delete static_cast<ReadImageContext*>(ctx); };
// }

} // namespace mz::utilities