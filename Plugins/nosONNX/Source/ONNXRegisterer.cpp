#include <Nodos/PluginAPI.h>
#include <Builtins_generated.h>
#include <Nodos/Helpers.hpp>
#include <AppService_generated.h>
#include <AppEvents_generated.h>

NOS_INIT();
void RegisterONNXRunner(nosNodeFunctions* outFunctions);
void RegisterRGBAtoTensor(nosNodeFunctions* outFunctions);
void RegisterTensorVisualizer(nosNodeFunctions* outFunctions);
extern "C"
{
	NOSAPI_ATTR nosResult NOSAPI_CALL nosExportNodeFunctions(size_t* outCount, nosNodeFunctions** outFunctions)
	{
		*outCount = (size_t)(3);
		if (!outFunctions)
			return NOS_RESULT_SUCCESS;

		RegisterONNXRunner(outFunctions[0]);
		RegisterRGBAtoTensor(outFunctions[1]);
		RegisterTensorVisualizer(outFunctions[2]);

		return NOS_RESULT_SUCCESS;
	}
}