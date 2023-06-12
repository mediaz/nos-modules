#include <MediaZ/Helpers.hpp>

namespace mz::utilities
{

MZ_REGISTER_NAME_SPACED(Array, "mz.utilities.Array")
MZ_REGISTER_NAME(Output, "Output");

struct ArrayCtx
{
    fb::UUID id;
    std::vector<fb::UUID> inputs;
    std::optional<fb::UUID> output;
    std::optional<mzName> type;

    ArrayCtx(const mzFbNode* inNode)
    {
        id = *inNode->id();
        static mzName VOID = mzEngine.GetName("mz.fb.Void");
        for (auto pin : *inNode->pins())
        {
            if (pin->show_as() == fb::ShowAs::INPUT_PIN ||
                pin->show_as() == fb::ShowAs::PROPERTY)
            {
                auto ty = mzEngine.GetName(pin->type_name()->c_str());
                if (!type)
                {
                    if (ty != VOID)
                        type = ty;
                }
                else
                    assert(*type == ty);
                inputs.push_back(*pin->id());
            }
            else
            {
                assert(!output);
                output = *pin->id();
            }
        }
    }

};

flatbuffers::uoffset_t GenerateOffset(
    flatbuffers::FlatBufferBuilder& fbb,
    const mzTypeInfo* type,
    const void* data);


flatbuffers::uoffset_t CopyStruct(
    flatbuffers::FlatBufferBuilder& fbb,
    uint32_t ByteSize,
    uint32_t FieldCount,
    decltype(mzTypeInfo::Fields) Fields,
    const void* data)
{
    auto tbl = ByteSize ? (flatbuffers::Table*)data : flatbuffers::GetRoot<flatbuffers::Table>(data);

    std::vector<flatbuffers::uoffset_t> offsets;

    for (int i = 0; i < FieldCount; ++i)
    {
        if (!tbl->CheckField(Fields[i].Offset) || Fields[i].Type->ByteSize) continue;
        if (auto offset = GenerateOffset(fbb, Fields[i].Type, tbl->GetPointer<u8*>(Fields[i].Offset)))
            offsets.push_back(offset);
    }

    // Now we can build the actual table from either offsets or scalar data.
    auto start = ByteSize ? fbb.StartStruct(1) : fbb.StartTable();
    size_t offset_idx = 0;

    for (int i = 0; i < FieldCount; ++i)
    {
        if (!tbl->CheckField(Fields[i].Offset)) continue;
        if (Fields[i].Type->ByteSize)
        {
            fbb.PushBytes(tbl->GetStruct<const uint8_t*>(Fields[i].Offset), Fields[i].Type->ByteSize);
            fbb.TrackField(Fields[i].Offset, fbb.GetSize());
        }
        else
        {
            fbb.AddOffset(Fields[i].Offset, flatbuffers::Offset<void>(offsets[offset_idx++]));
        }
    }
    
    FLATBUFFERS_ASSERT(offset_idx == offsets.size());
    if (ByteSize) {
        fbb.ClearOffsets();
        return fbb.EndStruct();
    }
    else {
        return fbb.EndTable(start);
    }
}

flatbuffers::uoffset_t GenerateOffset(
    flatbuffers::FlatBufferBuilder& fbb,
    const mzTypeInfo* type,
    const void* data)
{
    switch (type->BaseType)
    {
    case MZ_BASE_TYPE_STRUCT:
        return CopyStruct(fbb, type->ByteSize, type->FieldCount, type->Fields, flatbuffers::GetRoot<flatbuffers::Table>(data));
    case MZ_BASE_TYPE_STRING:
        return fbb.CreateString((const flatbuffers::String*)data).o;
    case MZ_BASE_TYPE_ARRAY: {
        auto vec = (flatbuffers::Vector<void*>*)(data);
        if(type->ElementType->ByteSize)
        {
            fbb.StartVector(vec->size(), type->ElementType->ByteSize, 1);
            fbb.PushBytes(vec->Data(), type->ElementType->ByteSize * vec->size());
            return fbb.EndVector(vec->size());
        }
        std::vector<flatbuffers::uoffset_t> elements(vec->size());
        for (int i = 0; i < vec->size(); i++) {
            elements[i] = GenerateOffset(fbb, type->ElementType, vec->Get(i));
        }
        return fbb.CreateVector(elements).o;
    }
    }
    return 0;
}

void RegisterArray(mzNodeFunctions* fn)
{
	fn->TypeName = MZN_Array;
    
	fn->OnNodeCreated = [](const mzFbNode* node, void** outCtxPtr) 
    {
        *outCtxPtr = new ArrayCtx(node);
	};

    fn->OnNodeUpdated = [](void* ctx, const mzFbNode* node) 
    {
        *((ArrayCtx*)ctx) = ArrayCtx(node);
	};

    fn->OnPinConnected = [](void* ctx, mzName pinName, mzUUID connector)
    {
        auto c = (ArrayCtx*)ctx;
        if (c->type)
            return;

        mzTypeInfo info = {};
        mzEngine.GetPinType(connector, &info);
        auto typeName = mzEngine.GetString(info.TypeName);
        auto outputType = "[" + std::string(typeName) + "]";

        flatbuffers::FlatBufferBuilder fbb;

        mzUUID id0, id1;
        mzEngine.GenerateID(&id0);
        mzEngine.GenerateID(&id1);
        std::vector<::flatbuffers::Offset<mz::fb::Pin>> pins = {
            fb::CreatePinDirect(fbb, &id0, "Input 0", typeName, fb::ShowAs::INPUT_PIN, fb::CanShowAs::INPUT_PIN_OR_PROPERTY),
            fb::CreatePinDirect(fbb, &id1, "Output",  outputType.c_str(), fb::ShowAs::OUTPUT_PIN, fb::CanShowAs::OUTPUT_PIN_ONLY),
        };
        mzEngine.HandleEvent(CreateAppEvent(fbb, mz::CreatePartialNodeUpdateDirect(fbb, &c->id, ClearFlags::ANY, 0, &pins)));
    };
    
	fn->OnNodeDeleted = [](void* ctx, mzUUID nodeId) 
    {
        delete (ArrayCtx*)ctx;
	};

	fn->ExecuteNode = [](void* ctx, const mzNodeExecuteArgs* args) -> mzResult 
    {
        auto c = (ArrayCtx*)ctx;
        if(!c->type) return MZ_RESULT_SUCCESS;
        mzTypeInfo info = {};
        mzEngine.GetTypeInfo(*c->type, &info);
        auto pins = NodeExecuteArgs(args);
        flatbuffers::FlatBufferBuilder fbb;
        pins.erase(MZN_Output);
        
        flatbuffers::uoffset_t offset = 0;
        if (info.ByteSize)
        {
            fbb.StartVector(pins.size(), info.ByteSize, 1);
            for (auto& [name, pin] : pins)
            {
                if (!pin.Buf.Size)
                {
                    std::vector<u8> zero(info.ByteSize);
                    fbb.PushBytes(zero.data(), info.ByteSize);
                }
                else 
                {
                    assert(info.ByteSize == pin.Buf.Size);
                    fbb.PushBytes((u8*)pin.Buf.Data, info.ByteSize);
                }

            }
            offset = fbb.EndVector(pins.size());
        }
        else
        {
            std::vector<flatbuffers::uoffset_t> offsets;
            for (auto& [name, pin] : pins)
                offsets.push_back(GenerateOffset(fbb, &info, pin.Buf.Data));
            offset = fbb.CreateVector(offsets).o;
        }

        fbb.Finish(flatbuffers::Offset<uint8_t>(offset));
        mz::Buffer buf = fbb.Release();
        const u8* vec = flatbuffers::GetRoot<u8>(buf.data());
        mzEngine.SetPinValue(*c->output, {(void*)vec,size_t(buf.data()+buf.size()-vec)});
        return MZ_RESULT_SUCCESS;
	};

    fn->OnMenuRequested = [](void* ctx, const mzContextMenuRequest* request)
    {
        auto c = (ArrayCtx*)ctx;
        if(!c->type) return;
        flatbuffers::FlatBufferBuilder fbb;
        std::vector<flatbuffers::Offset<mz::ContextMenuItem>> fields;
        std::string add = "Add Input " + std::to_string(c->inputs.size());
        fields.push_back(mz::CreateContextMenuItemDirect(fbb, add.c_str(), 1));
        if (!c->inputs.empty())
        {
            std::string remove = "Remove Input " + std::to_string(c->inputs.size() - 1);
            fields.push_back(mz::CreateContextMenuItemDirect(fbb, remove.c_str(), 2));
        }
        mzEngine.HandleEvent(CreateAppEvent(fbb, mz::CreateContextMenuUpdateDirect(fbb, &c->id, request->pos(), request->instigator(), &fields)));
    };

    fn->OnMenuCommand = [](void* ctx, uint32_t cmd)
    {
        auto c = (ArrayCtx*)ctx;
        if(!c->type)
            return;

        const u32 action = cmd & 3;
   
        flatbuffers::FlatBufferBuilder fbb;
        switch (action)
        {
        case 2: // Remove Field
        {
            std::vector<fb::UUID> id = { c->inputs.back() };
            c->inputs.pop_back();
            mzEngine.HandleEvent(CreateAppEvent(fbb, mz::CreatePartialNodeUpdateDirect(fbb, &c->id, ClearFlags::NONE, &id)));
        }
        break;
        case 1: // Add Field
        {
            auto typeName = mzEngine.GetString(*c->type);
            auto outputType = "[" + std::string(typeName) + "]";

            mzBuffer value;
            std::vector<u8> data;
            if (MZ_RESULT_SUCCESS == mzEngine.GetDefaultValueOfType(*c->type, &value))
            {
                data = std::vector<u8>{ (u8*)value.Data, (u8*)value.Data + value.Size};
            }

            auto slot = c->inputs.size();
            mzUUID id;
            mzEngine.GenerateID(&id);
            c->inputs.push_back(id);
            std::vector<flatbuffers::Offset<mz::fb::Pin>> pins = {
                    mz::fb::CreatePinDirect(fbb, &id, ("Input " + std::to_string(slot)).c_str(), typeName, mz::fb::ShowAs::INPUT_PIN, mz::fb::CanShowAs::INPUT_PIN_OR_PROPERTY, 0, 0, &data),
            };
            mzEngine.HandleEvent(CreateAppEvent(fbb, mz::CreatePartialNodeUpdateDirect(fbb, &c->id, ClearFlags::NONE, 0, &pins)));
        }
        break;
        }
    };
}

} // namespace mz