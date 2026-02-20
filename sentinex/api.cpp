#include "api.h"
#include "config.h"
#include "http.h"
#include "logger.h"
#include <ctime>
#include <algorithm>

// ── JSON extract ──────────────────────────────────────────────
static std::string jget(const std::string &json, const std::string &key) {
    auto p=json.find("\""+key+"\"");
    if(p==std::string::npos) return "";
    p+=key.size()+2;
    while(p<json.size()&&(json[p]==' '||json[p]==':')) p++;
    if(p>=json.size()) return "";
    if(json[p]=='"') {
        p++; std::string v;
        while(p<json.size()&&json[p]!='"'){if(json[p]=='\\')p++;v+=json[p++];}
        return v;
    }
    std::string v;
    while(p<json.size()&&json[p]!=','&&json[p]!='}'&&json[p]!='\n') v+=json[p++];
    while(!v.empty()&&v.back()==' ') v.pop_back();
    return v;
}

// Split JSON data array jadi vector per object
static std::vector<std::string> split_objects(const std::string &json) {
    std::vector<std::string> objs;
    auto dp=json.find("\"data\"");
    if(dp==std::string::npos) return objs;
    auto as=json.find('[',dp);
    if(as==std::string::npos) return objs;
    int depth=0; size_t os=std::string::npos;
    for(size_t i=as;i<json.size();i++) {
        if(json[i]=='{'){if(depth==0)os=i;depth++;}
        else if(json[i]=='}'){depth--;if(depth==0&&os!=std::string::npos){objs.push_back(json.substr(os,i-os+1));os=std::string::npos;}}
        else if(json[i]==']'&&depth==0) break;
    }
    return objs;
}

static std::vector<std::string> auth_hdrs() {
    return {"Authorization: Bearer "+cfg.api_application,
            "Accept: application/json","Content-Type: application/json"};
}

long long parse_iso8601(const std::string &s) {
    struct tm t={};
    if(sscanf(s.c_str(),"%d-%d-%dT%d:%d:%d",
              &t.tm_year,&t.tm_mon,&t.tm_mday,
              &t.tm_hour,&t.tm_min,&t.tm_sec)!=6) return 0;
    t.tm_year-=1900; t.tm_mon-=1; t.tm_isdst=0;
    return (long long)timegm(&t);
}

// Ekstrak block "attributes" dari response API Pterodactyl
// supaya tidak salah baca key yang ada di "relationships"
static std::string get_attributes(const std::string &json) {
    std::string needle = "\"attributes\"";
    auto p = json.find(needle);
    if (p == std::string::npos) return json;
    auto b = json.find('{', p);
    if (b == std::string::npos) return json;
    int depth = 0; size_t end = b;
    for (; end < json.size(); end++) {
        if (json[end]=='{') depth++;
        else if (json[end]=='}') { depth--; if (depth==0) break; }
    }
    return json.substr(b, end - b + 1);
}

SrvInfo api_get_server_info(const std::string &uuid) {
    SrvInfo si; si.uuid=uuid;
    if(cfg.api_application.empty()||cfg.panel_domain.empty()) return si;
    auto hdrs=auth_hdrs();
    std::string r=http_get(cfg.panel_domain+"/api/application/servers?filter[uuid]="+uuid,hdrs);
    std::string attr=get_attributes(r);
    si.name       =jget(attr,"name");
    si.created_at =jget(attr,"created_at");
    std::string nid=jget(attr,"node");
    if(!nid.empty()){auto nr=http_get(cfg.panel_domain+"/api/application/nodes/"+nid,hdrs);si.node=jget(get_attributes(nr),"name");}
    std::string uid=jget(attr,"user");
    if(!uid.empty()){auto ur=http_get(cfg.panel_domain+"/api/application/users/"+uid,hdrs);std::string uattr=get_attributes(ur);si.username=jget(uattr,"username");si.email=jget(uattr,"email");}
    if(si.name.empty())     si.name     ="?";
    if(si.username.empty()) si.username ="?";
    if(si.email.empty())    si.email    ="?";
    if(si.node.empty())     si.node     ="?";
    return si;
}

void api_suspend(const std::string &uuid) {
    if(cfg.api_application.empty()) return;
    auto hdrs=auth_hdrs();
    std::string r=http_get(cfg.panel_domain+"/api/application/servers?filter[uuid]="+uuid,hdrs);
    std::string attr=get_attributes(r);
    std::string sid=jget(attr,"id");
    if(sid.empty()){wlog("[SUSPEND] Gagal dapat ID: "+uuid);return;}
    http_post(cfg.panel_domain+"/api/application/servers/"+sid+"/suspend","{}",hdrs);
    wlog("[SUSPEND] Server "+uuid+" di-suspend.");
}

std::vector<PteroUser> api_get_recent_users(int limit) {
    std::vector<PteroUser> users;
    std::string r=http_get(cfg.panel_domain+"/api/application/users?sort=-id&per_page="+std::to_string(limit),auth_hdrs());
    for(auto &obj:split_objects(r)){
        auto ap=obj.find("\"attributes\"");
        if(ap==std::string::npos) continue;
        std::string attr=obj.substr(ap);
        PteroUser u;
        u.id        =jget(obj,"id");
        u.username  =jget(attr,"username");
        u.email     =jget(attr,"email");
        u.created_at=jget(attr,"created_at");
        if(!u.id.empty()) users.push_back(u);
    }
    return users;
}

std::vector<PteroServer> api_get_recent_servers(int limit) {
    std::vector<PteroServer> servers;
    std::string r=http_get(cfg.panel_domain+"/api/application/servers?sort=-id&per_page="+std::to_string(limit),auth_hdrs());
    for(auto &obj:split_objects(r)){
        auto ap=obj.find("\"attributes\"");
        if(ap==std::string::npos) continue;
        std::string attr=obj.substr(ap);
        PteroServer s;
        s.id        =jget(obj,"id");
        s.uuid      =jget(attr,"uuid");
        s.name      =jget(attr,"name");
        s.created_at=jget(attr,"created_at");
        if(!s.id.empty()) servers.push_back(s);
    }
    return servers;
}

bool api_delete_user(const std::string &user_id) {
    std::string r=http_delete(cfg.panel_domain+"/api/application/users/"+user_id,auth_hdrs());
    bool ok=r.empty()||r.find("error")==std::string::npos;
    wlog("[API] Delete user #"+user_id+" -> "+(ok?"OK":"GAGAL: "+r.substr(0,60)));
    return ok;
}

bool api_delete_server(const std::string &server_id) {
    std::string r=http_delete(cfg.panel_domain+"/api/application/servers/"+server_id+"/force",auth_hdrs());
    bool ok=r.empty()||r.find("error")==std::string::npos;
    wlog("[API] Delete server #"+server_id+" -> "+(ok?"OK":"GAGAL: "+r.substr(0,60)));
    return ok;
}
