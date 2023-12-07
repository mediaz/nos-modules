#include <Nodos/PluginAPI.h>
#include <Builtins_generated.h>
#include <Nodos/Helpers.hpp>
#include <AppService_generated.h>
#include <AppEvents_generated.h>
#include <onnxruntime_cxx_api.h>
#include "ONNXRTCommon.h"

NOS_REGISTER_NAME(ONNXRunner);
NOS_REGISTER_NAME(In);
NOS_REGISTER_NAME(Out);
NOS_REGISTER_NAME(ModelPath);

struct ONNXRunnerNodeContext : nos::NodeContext
{
	Ort::Env env;
	Ort::Session ModelSession{nullptr};

	nos::fb::TTensor nosInputTensor, nosOutputTensor;
	std::string InputName, OutputName;
	nosUUID NodeID, InputID, OutputID;
	nosTensor InputTensor, OutputTensor;
	std::atomic_bool shouldStop;
	std::condition_variable WaitInput;
	std::mutex InputMutex;
	std::thread ModelRunner;

	ONNXRunnerNodeContext(nos::fb::Node const* node) :NodeContext(node) {
		bool tryToReload = false;
		std::filesystem::path modelPath;
		NodeID = *node->id();

		ModelRunner = std::thread([this]() {this->RunModel(); });
		for (auto pin : *node->pins()) {
			if (pin->orphan_state()->is_orphan()) {
				if (pin->show_as() == nos::fb::ShowAs::INPUT_PIN) {
					InputID = *pin->id();
					InputName = pin->name()->c_str();
				}
				if (pin->show_as() == nos::fb::ShowAs::OUTPUT_PIN) {
					OutputID = *pin->id();
					OutputName = pin->name()->c_str();
				}
				tryToReload = true;
			}
			if (NSN_ModelPath.Compare(pin->name()->c_str()) == 0) {
				modelPath = (const char*)pin->data()->data();
			}
			
		}
		nosResult reloadResult = LoadModel(modelPath, true);
		if (tryToReload && reloadResult != NOS_RESULT_SUCCESS) {
			nosEngine.LogE("Model reload failed, check if the model file exists!");
			//Delete pins
			std::vector<nos::fb::UUID> pinsToDelete = { InputID, OutputID };
			flatbuffers::FlatBufferBuilder fbb;
			std::vector<flatbuffers::Offset<nos::app::AppEvent>> Offsets;
			HandleEvent(
				nos::CreateAppEvent(fbb, 
					nos::CreatePartialNodeUpdateDirect(fbb, &NodeID, nos::ClearFlags::CLEAR_PINS, &pinsToDelete)));
		}
		else if(tryToReload) {
			//Un-orphanize pins
			std::vector<nos::fb::UUID> pins = { InputID, OutputID };
			flatbuffers::FlatBufferBuilder fbb;
			std::vector<flatbuffers::Offset<nos::PartialPinUpdate>> Offsets;
			for (const auto& id : pins) {
				Offsets.push_back(nos::CreatePartialPinUpdateDirect(fbb, &id, 0, nos::fb::CreateOrphanStateDirect(fbb, false)));
			}
			HandleEvent(
				nos::CreateAppEvent(fbb,
					nos::CreatePartialNodeUpdateDirect(fbb, &NodeID, nos::ClearFlags::NONE,0,0,0,0,0,0,0, &Offsets)));
		}
	}

	~ONNXRunnerNodeContext() {
		if (ModelRunner.joinable()) {
			shouldStop = true;
			WaitInput.notify_one();
			ModelRunner.join();
		}
	}

	void  OnPinValueChanged(nos::Name pinName, nosUUID pinId, nosBuffer* value) override {
		if (pinName.Compare(InputName.c_str()) == 0){
			auto tensor = flatbuffers::GetRoot<nos::fb::Tensor>(value->Data);
			for (int i = 0; i < tensor->shape()->Length() && ((tensor->shape()->data() + i) != nullptr) ; i++) {
				if (*(tensor->shape()->data() + i) != nosInputTensor.shape[i]) {
					nosEngine.LogE("Input tensor is not compatible with model");
					return;
				}
			}
			if (tensor->buffer() == NULL) {
				nosEngine.LogW("Input tensor has no data");
				return;
			}
			nosInputTensor.buffer = tensor->buffer();
			InputTensor.CreateTensor<uint8_t>(reinterpret_cast<uint8_t*>(nosInputTensor.buffer), InputTensor.GetLength(), true);
			WaitInput.notify_one();
			
		}
	}

	void RunModel() {
		while (!shouldStop) {
			{
				std::unique_lock<std::mutex> lock(InputMutex);
				WaitInput.wait(lock);
				if (shouldStop) {
					return;
				}
			}
			const char* input_names[] = { InputName.c_str() };
			const char* output_names[] = { OutputName.c_str() };
			ModelSession.Run(Ort::RunOptions{nullptr}, input_names, InputTensor.GetORTValuePointer(), 1, output_names, OutputTensor.GetORTValuePointer(), 1);
			//OutputTensor.ApplySoftmax();
			nosOutputTensor.buffer = (uint64_t)OutputTensor.GetData();
			nosEngine.SetPinValue(OutputID, nos::Buffer::From(nosOutputTensor));
		}
	}

	nosResult LoadModel(std::filesystem::path modelPath, bool isReload = false) {
		if (std::filesystem::exists(modelPath)) {

			ModelSession = Ort::Session{ env, modelPath.c_str(), Ort::SessionOptions{nullptr} };

			InputTensor.SetShape(ModelSession.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape());
			OutputTensor.SetShape(ModelSession.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape());
			OutputTensor.CreateEmpty();
			nosInputTensor.shape = ModelSession.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
			nosOutputTensor.shape = ModelSession.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
			if (!isReload) {
				int InputCount = ModelSession.GetInputCount();
				int OutputCount = ModelSession.GetOutputCount();

				flatbuffers::FlatBufferBuilder fbb_t;

				auto bufInput = nos::Buffer::From(nosInputTensor);
				auto inputTensorData = std::vector<uint8_t>((uint8_t*)bufInput.data(), (uint8_t*)bufInput.data() + bufInput.size());

				auto bufOutput = nos::Buffer::From(nosOutputTensor);
				auto outputTensorData = std::vector<uint8_t>((uint8_t*)bufOutput.data(), (uint8_t*)bufOutput.data() + bufOutput.size());

				std::vector<flatbuffers::Offset<nos::fb::Pin>> InputPins;
				std::vector<flatbuffers::Offset<nos::fb::Pin>> OutputPins;
				flatbuffers::FlatBufferBuilder fbb;
				flatbuffers::FlatBufferBuilder fbb2;

				std::optional<Ort::AllocatedStringPtr> inputName;
				std::optional<Ort::AllocatedStringPtr> outputName;
				Ort::AllocatorWithDefaultOptions ortAllocator;
				inputName.emplace(ModelSession.GetInputNameAllocated(0, ortAllocator));
				outputName.emplace(ModelSession.GetOutputNameAllocated(0, ortAllocator));

				InputName = { inputName->get() };
				OutputName = { outputName->get() };

				inputName->reset();
				outputName->reset();

				UUIDGenerator generator;
				InputID = *(nosUUID*)generator.Generate()().as_bytes().data();
				OutputID = *(nosUUID*)generator.Generate()().as_bytes().data();

				for (int i = 0; i < InputCount; i++) {
					inputName.emplace(ModelSession.GetInputNameAllocated(i, ortAllocator));
					InputPins.push_back(nos::fb::CreatePinDirect(fbb,
						&InputID,
						inputName->get(),
						"nos.fb.Tensor",
						nos::fb::ShowAs::INPUT_PIN,
						nos::fb::CanShowAs::INPUT_PIN_ONLY,
						0,
						0,
						&inputTensorData));
					inputName->reset();
				}

				for (int i = 0; i < OutputCount; i++) {
					outputName.emplace(ModelSession.GetOutputNameAllocated(i, ortAllocator));

					OutputPins.push_back(nos::fb::CreatePinDirect(fbb2,
						&OutputID,
						outputName->get(),
						"nos.fb.Tensor",
						nos::fb::ShowAs::OUTPUT_PIN,
						nos::fb::CanShowAs::OUTPUT_PIN_OR_PROPERTY,
						0,
						0,
						&outputTensorData));

					outputName->reset();
				}

				HandleEvent(nos::CreateAppEvent(fbb,
					nos::CreatePartialNodeUpdateDirect(fbb, &NodeId, nos::ClearFlags::NONE, 0, &InputPins)));

				HandleEvent(nos::CreateAppEvent(fbb2,
					nos::CreatePartialNodeUpdateDirect(fbb2, &NodeId, nos::ClearFlags::NONE, 0, &OutputPins)));
			}
			return NOS_RESULT_SUCCESS;
		}
		return NOS_RESULT_FAILED;
	}

	static nosResult GetFunctions(size_t* count, nosName* names, nosPfnNodeFunctionExecute* fns)
	{
		*count = 1;
		if (!names || !fns)
			return NOS_RESULT_SUCCESS;
		names[0] = NOS_NAME_STATIC("LoadModel");
		fns[0] = [](void* ctx, const nosNodeExecuteArgs* nodeArgs, const nosNodeExecuteArgs* functionArgs) {
			if (ONNXRunnerNodeContext* onnxNode = static_cast<ONNXRunnerNodeContext*>(ctx))
			{
				//TODO: there may exists more than one inputs so be careful about index 0 here!
				auto values = nos::GetPinValues(nodeArgs);
				std::filesystem::path modelPath = GetPinValue<const char>(values, NSN_ModelPath);
				if (onnxNode->LoadModel(modelPath) != NOS_RESULT_SUCCESS) {
					nosEngine.LogE("Model load failed!");
				}
				
			}
		};
		return NOS_RESULT_SUCCESS;
	}
};

void RegisterONNXRunner(nosNodeFunctions* outFunctions) {
	NOS_BIND_NODE_CLASS(NSN_ONNXRunner, ONNXRunnerNodeContext, outFunctions);
}