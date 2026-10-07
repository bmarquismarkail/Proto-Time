#include "Meaning.hpp"
#include <set>
namespace BMMQ::Space {
namespace {
std::string hashText(const std::string& s){return digest(std::span(reinterpret_cast<const uint8_t*>(s.data()),s.size()));}
void text(const Json& value,size_t maximum){if(!value.is_string()||value.get_ref<const std::string&>().empty()||value.get_ref<const std::string&>().size()>maximum)throw std::invalid_argument("invalid symbol/source/claim text");}
void hash(const Json& value){text(value,64);auto& s=value.get_ref<const std::string&>();if(s.size()!=64||s.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::invalid_argument("invalid meaning digest");}
void validateImport(const Json& project,const Json& data){
    if(data.at("schemaVersion")!=1||data.at("core")!=project.at("core")||data.at("romSha256")!=project.at("romSha256"))throw std::invalid_argument("symbol core/ROM/schema mismatch");
    auto size=counter(data.at("romBytes"));if(size<1||size>64u*1024u*1024u)throw std::invalid_argument("invalid symbol ROM extent");
    const auto& sources=data.at("sources");const auto& symbols=data.at("symbols");
    if(!sources.is_array()||sources.size()>256||!symbols.is_array()||symbols.size()>65536||data.dump().size()>4u*1024u*1024u)throw std::invalid_argument("symbol import budget exceeded");
    std::map<std::string,size_t> lines;
    for(auto& source:sources){text(source.at("path"),4096);text(source.at("content"),1024u*1024u);hash(source.at("sha256"));
        auto path=source["path"].get<std::string>(),content=source["content"].get<std::string>();
        if(lines.contains(path)||source["sha256"]!=hashText(content))throw std::invalid_argument("duplicate or corrupt source");
        lines[path]=1+std::count(content.begin(),content.end(),'\n');
    }
    std::set<std::string> identities;
    for(auto& symbol:symbols){text(symbol.at("name"),256);auto loc=counter(symbol.at("location"));auto space=loc>>32,bank=(loc>>16)&65535,offset=loc&65535;
        if(space!=1||offset>=0x4000||bank*0x4000+offset>=size)throw std::invalid_argument("symbol outside cartridge backing");
        if(!identities.insert(symbol["name"].get<std::string>()+":"+symbol["location"].get<std::string>()).second)throw std::invalid_argument("duplicate symbol");
        if(symbol.contains("source")){text(symbol["source"],4096);auto line=symbol.at("line");auto path=symbol["source"].get<std::string>();
            if(!lines.contains(path)||!line.is_number_integer()||line<1||line>lines.at(path))throw std::invalid_argument("invalid source line binding");}
    }
}
void validateClaim(const Json& project,const Json& claim){
    text(claim.at("purpose"),65536);auto status=claim.at("status").get<std::string>();
    if(status!="inferred"&&status!="reviewed"&&status!="unresolved")throw std::invalid_argument("invalid purpose status");
    auto instruction=claim.at("instruction").get<std::string>();if(!project["instructions"].contains(instruction))throw std::invalid_argument("unknown purpose instruction");
    const auto& evidence=claim.at("dependencies");if(!evidence.is_array()||evidence.size()>4096||(status!="unresolved"&&evidence.empty()))throw std::invalid_argument("purpose requires bounded evidence");
    std::set<std::string> known,seen;for(auto& d:project["dependencies"])known.insert(d["id"].get<std::string>());
    for(auto& ref:evidence){if(!ref.is_string()||!known.contains(ref.get<std::string>())||!seen.insert(ref.get<std::string>()).second)throw std::invalid_argument("invalid purpose evidence link");}
    if(status=="reviewed"){text(claim.at("reviewer"),256);text(claim.at("rationale"),65536);}
}
}
void validateMeaning(const Json& project){
    if(project.contains("symbolImports")){
        const auto& imports=project["symbolImports"];if(!imports.is_object()||imports.size()>16)throw std::invalid_argument("invalid symbol import collection");
        for(auto it=imports.begin();it!=imports.end();++it){validateImport(project,it.value());if(it.key()!=hashText(it.value().dump()))throw std::invalid_argument("symbol import identity mismatch");}
    }
    if(project.contains("purposeClaims")){
        const auto& claims=project["purposeClaims"];if(!claims.is_object()||claims.size()>4096)throw std::invalid_argument("invalid purpose collection");
        for(auto it=claims.begin();it!=claims.end();++it){validateClaim(project,it.value());if(it.key()!=hashText(project["core"].get<std::string>()+":"+project["romSha256"].get<std::string>()+":"+it.value().dump()))throw std::invalid_argument("purpose identity mismatch");}
    }
}
void importSymbols(Json& project,const Json& incoming,std::span<const uint8_t> rom){
    Project::validate(project);if(project["romSha256"]!=digest(rom))throw std::invalid_argument("symbol artifact ROM mismatch");
    auto data=incoming;data["romBytes"]=decimal(rom.size());validateImport(project,data);
    auto next=project;auto key=hashText(data.dump());if(next.contains("symbolImports")&&next["symbolImports"].contains(key))return;
    next["symbolImports"][key]=std::move(data);next.erase("analysis");next["revision"]=decimal(counter(next["revision"])+1);Project::validate(next);project=std::move(next);
}
void addPurposeClaim(Json& project,const Json& claim){
    Project::validate(project);validateClaim(project,claim);auto next=project;auto key=hashText(project["core"].get<std::string>()+":"+project["romSha256"].get<std::string>()+":"+claim.dump());
    if(next.contains("purposeClaims")&&next["purposeClaims"].contains(key))return;
    next["purposeClaims"][key]=claim;next.erase("analysis");next["revision"]=decimal(counter(next["revision"])+1);Project::validate(next);project=std::move(next);
}
Json purposeFindings(const Json& project){
    validateMeaning(project);Json findings=Json::array();
    if(project.contains("purposeClaims"))for(auto it=project["purposeClaims"].begin();it!=project["purposeClaims"].end();++it){auto finding=it.value();finding["id"]=it.key();finding["evidenceKind"]="captured-dependency-links";
        finding["limitations"]=Json::array({"Gameplay meaning is a claim distinct from observed effects; review does not establish port completeness."});findings.push_back(std::move(finding));}
    return findings;
}
}
