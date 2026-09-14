/*
 * Copyright MediaZ Teknoloji A.S. All Rights Reserved.
 */

#ifndef NOS_SYS_VARIABLES_H_INCLUDED
#define NOS_SYS_VARIABLES_H_INCLUDED

NOS_BEGIN_C_LINKAGE

#include <Nodos/Types.h>

typedef void (NOSAPI_CALL *nosVariableUpdateCallback)(nosName name, void* userData, nosName typeName, const nosBuffer* value);

typedef struct nosVariableSubsystem {
	nosResult (NOSAPI_CALL *Get)(nosName name, nosName* outTypeName, nosBuffer* outValue);
	nosResult (NOSAPI_CALL *Set)(nosName name, nosName typeName, const nosBuffer* value);
	int32_t (NOSAPI_CALL *RegisterVariableUpdateCallback)(nosName name, nosVariableUpdateCallback callback, void* userData);
	nosResult (NOSAPI_CALL *UnregisterVariableUpdateCallback)(nosName name, int32_t callbackId);
	nosResult (NOSAPI_CALL *AddNodeReference)(nosName name, nosUUID nodeId);
	nosResult (NOSAPI_CALL *DeleteNodeReference)(nosName name, nosUUID nodeId);
} nosVariableSubsystem;

#pragma region Helper Declarations & Macros

// Make sure these are same with nossys file.
#define NOS_SYS_VARIABLES_NAME "nos.sys.variables"
#define NOS_SYS_VARIABLES_VERSION_MAJOR 4
#define NOS_SYS_VARIABLES_VERSION_MINOR 0

extern struct nosPluginInfo nosVariablesSubsystemModuleInfo;
extern nosVariableSubsystem* nosVariables;

#define NOS_SYS_VARIABLES_INIT()         \
	nosPluginInfo nosVariableSubsystemModuleInfo; \
	nosVariableSubsystem* nosVariables = nullptr;

#define NOS_SYS_VARIABLES_IMPORT() NOS_IMPORT_DEP(NOS_SYS_VARIABLES_NAME, nosVariableSubsystemModuleInfo, nosVariables)

#pragma endregion

NOS_END_C_LINKAGE

#endif