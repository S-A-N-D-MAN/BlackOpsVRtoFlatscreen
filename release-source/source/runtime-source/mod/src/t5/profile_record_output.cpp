// SPDX-License-Identifier: GPL-3.0-only
#include "t5/profile_record_output.hpp"
#include "t5/identity.hpp"
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <span>

namespace bovr::t5::profile_detail {
namespace {
bool create_bytes(const std::wstring& path,std::span<const std::uint8_t> bytes) noexcept {
    if(bytes.empty()||bytes.size()>1024*1024)return false;
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written{};const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)!=FALSE&&written==bytes.size();
    const bool closed=CloseHandle(file)!=FALSE;return ok&&closed;
}
std::string quoted(const std::string& text) {
    std::string out="\"";
    for(const auto ch:text){if(ch=='\\'||ch=='\"')out+='\\';out+=ch;}
    out+='\"';return out;
}
std::string bytes_hex(const std::vector<std::uint8_t>& bytes) {
    constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(const auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
bool canonical_equal(const input::CanonicalProfileIdentity& a,const input::CanonicalProfileIdentity& b) {
    return a.schema==b.schema&&a.selected_model==b.selected_model&&a.selected_local_bone==b.selected_local_bone&&
        a.model_payloads==b.model_payloads&&a.assembly_payload==b.assembly_payload;
}
}
bool protected_profile_copy(void*,std::uintptr_t address,void* out,std::size_t bytes) noexcept {
    if(!out||!bytes||address<0x10000||bytes>UINT32_MAX||bytes>static_cast<std::uint64_t>(UINT32_MAX)-address+1)return false;
    __try {memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
RecordOutput write_record(const std::wstring& directory,DWORD process,std::uint64_t observation,const ProfileRecord& record) noexcept {
    RecordOutput out{};
    try {
        // Revalidate copied values, not game memory. Callers cannot supply a
        // mismatched packet/hash or unsafe text to the evidence writer.
        input::CanonicalProfileIdentity verified;
        const auto& p=record.copies;
        if(!process||!observation||record.identity_status!=input::ProfileIdentityStatus::accepted||
           input::canonical_profile_identity(p,p.after_stamp.native_thread,p.after_stamp.native_epoch,p.after_stamp.observed_ms,&verified)!=input::ProfileIdentityStatus::accepted||
           !canonical_equal(verified,record.canonical)||!valid_assembly(record.assembly,p.after))return out;
        const auto attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return out;
        const std::wstring prefix=L"profile-"+std::to_wstring(process)+L"-"+std::to_wstring(observation);
        const std::wstring base=directory+L"\\"+prefix;
        std::vector<std::string> model_hashes;model_hashes.reserve(verified.model_payloads.size());
        for(const auto& bytes:verified.model_payloads)model_hashes.push_back(sha256(bytes));
        out.selected_asset_sha256=model_hashes.at(verified.selected_model);out.ordered_models_sha256=sha256(verified.assembly_payload);
        std::ostringstream json;json.imbue(std::locale::classic());json<<std::setprecision(9);
        json<<"{\n  \"evidence_schema\": 1,\n  \"canonical_schema\": 1,\n  \"image_sha256\": "<<quoted(p.image_sha256)
            <<",\n  \"process_id\": "<<process<<", \"observation\": "<<observation<<",\n  \"borrow_serial\": "<<p.after_stamp.borrow_serial
            <<", \"native_thread\": "<<p.after_stamp.native_thread<<", \"gameplay_epoch\": "<<p.after_stamp.native_epoch
            <<",\n  \"copy_begin_ms\": "<<p.before_stamp.observed_ms<<", \"copy_end_ms\": "<<p.after_stamp.observed_ms
            <<", \"command_sequence\": "<<record.command_sequence<<",\n  \"native_time\": "<<record.native_time<<", \"command_number\": "<<record.command_number
            <<", \"command_clock\": "<<record.command_clock<<", \"weapon_index\": "<<record.weapon_index
            <<",\n  \"selected_model\": "<<verified.selected_model<<", \"selected_local_bone\": "<<verified.selected_local_bone
            <<",\n  \"selected_asset_sha256\": "<<quoted(out.selected_asset_sha256)<<",\n  \"ordered_models_sha256\": "<<quoted(out.ordered_models_sha256)
            <<",\n  \"pointer_retention\": false, \"native_calls\": 0, \"native_writes\": 0,\n"
            <<"  \"allocation_generation_verified\": false, \"fixed_grip_profile_fitted\": false,\n"
            <<"  \"assembly_extension\": {\"diagnostic_only\": true, \"included_in_v1_canonical\": false, \"total_bones\": "<<record.assembly.total_bones<<", \"model_parents\": [";
        for(std::size_t i=0;i<record.assembly.model_parents.size();++i){if(i)json<<',';json<<static_cast<unsigned>(record.assembly.model_parents[i]);}
        json<<"], \"duplicate_binary_hex\": "<<quoted(bytes_hex(record.assembly.duplicate_data))<<"},\n  \"models\": [\n";
        for(std::size_t i=0;i<p.after.size();++i) {
            if(i)json<<",\n";const auto& model=p.after[i];
            json<<"    {\"ordinal\": "<<i<<", \"name\": "<<quoted(model.name.bytes)<<", \"root_count\": "<<model.root_count
                <<", \"asset_sha256\": "<<quoted(model_hashes[i])<<", \"bones\": [\n";
            for(std::size_t j=0;j<model.bones.size();++j) {
                if(j)json<<",\n";const auto& bone=model.bones[j];
                json<<"      {\"ordinal\": "<<j<<", \"name\": "<<quoted(bone.name.bytes)<<", \"parent_delta\": "<<bone.parent_delta<<", \"bind_u32\": [";
                for(unsigned k=0;k<8;++k){if(k)json<<',';json<<bone.bind_bits[k];}
                json<<"], \"quaternion_xyzw_translation_xyz_weight\": [";
                for(unsigned k=0;k<8;++k){float value{};memcpy(&value,&bone.bind_bits[k],4);if(k)json<<',';json<<value;}
                json<<"]}";
            }
            json<<"\n    ]}";
        }
        json<<"\n  ]\n}\n";const auto text=json.str();if(text.size()>1024*1024)return out;
        // Packet/diagnostic files precede the JSON completion marker. A collision
        // fails immediately and cannot replace evidence from a prior observation.
        for(std::size_t i=0;i<verified.model_payloads.size();++i)
            if(!create_bytes(base+L"-asset-"+std::to_wstring(i)+L"-v1.bin",verified.model_payloads[i]))return out;
        if(!create_bytes(base+L"-ordered-models-v1.bin",verified.assembly_payload)||
           !create_bytes(base+L"-assembly-diagnostic.bin",record.assembly.duplicate_data))return out;
        out.json_path=base+L".json";
        out.complete=create_bytes(out.json_path,{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});
        return out;
    }catch(...){return out;}
}
} // namespace bovr::t5::profile_detail
