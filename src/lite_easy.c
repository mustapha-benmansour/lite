
#include "lauxlib.h"
#include "lua.h"
#include <curl/curl.h>
#include <curl/header.h>
#include <curl/multi.h>
#include <stdio.h>
#ifdef LITE_CURL

#include "lite_util.h"



#define LITE_EASY_MT "lite.easy"

typedef enum{
    CB_IDX_DONE=0,
    CB_IDX_WRITE,
    CB_IDX_READ,
    CB_IDX_HEADER,
    CB_IDX_DEBUG,
    CB_IDX_SSL_CTX_,
    CB_IDX_SOCKOPT,
    CB_IDX_OPENSOCKET,
    CB_IDX_SEEK,
    CB_IDX_SSH_KEY,
    CB_IDX_INTERLEAVE,
    CB_IDX_CHUNK_BGN_,
    CB_IDX_CHUNK_END_,
    CB_IDX_FNMATCH_,
    CB_IDX_CLOSESOCKET,
    CB_IDX_XFERINFO,
    CB_IDX_RESOLVER_START_,
    CB_IDX_TRAILER,
    CB_IDX_HSTSREAD,
    CB_IDX_HSTSWRITE,
    CB_IDX_PREREQ,
    CB_IDX_SSH_HOSTKEY,
    CB_IDX_LENGTH
}CB_IDX;

typedef enum{
    SLIST_IDX_HTTPHEADER=0,
    SLIST_IDX_QUOTE,
    SLIST_IDX_POSTQUOTE,
    SLIST_IDX_TELNETOPTIONS,
    SLIST_IDX_PREQUOTE,
    SLIST_IDX_HTTP200ALIASES,
    SLIST_IDX_MAIL_RCPT,
    SLIST_IDX_RESOLVE,
    SLIST_IDX_PROXYHEADER,
    SLIST_IDX_CONNECT_TO,
    SLIST_IDX_LENGTH
}SLIST_IDX;

typedef struct {
    lite_loop_t * ctx;
    CURL * easy;
    int on[CB_IDX_LENGTH];
    char error_buf[CURL_ERROR_SIZE];
    int error;
    struct curl_slist * c_ptr_slist[SLIST_IDX_LENGTH];
    struct curl_mime * mimepost;
} easy_t;



static void easy_push_error(lua_State * L,CURLcode rc);


static int easy_gc(lua_State * L){
    easy_t * leasy=lua_touserdata(L,1);
    //int * refp;
    //curl_easy_getinfo(leasy->easy, CURLINFO_PRIVATE,&refp);
    for (int i=0;i<CB_IDX_LENGTH;i++){
        if (RF_ISSET(leasy->on[i]))
            RF_UNSET(leasy->on[i])
    }
    if (RF_ISSET(leasy->error))
        RF_UNSET(leasy->error)

    for (int i=0;i<SLIST_IDX_LENGTH;i++){
        if (leasy->c_ptr_slist[i]!=NULL){
            curl_slist_free_all(leasy->c_ptr_slist[i]);
            //leasy->c_ptr_slist[i]=NULL;
        }
    }
    if (leasy->mimepost!=NULL){
        curl_mime_free(leasy->mimepost);
        //leasy->mimepost=NULL;
    }
    curl_easy_cleanup(leasy->easy);
    return 0;
}

int lite_easy(lua_State * L){
    lite_loop_t * ctx = lua_touserdata(L, lua_upvalueindex(1));
    CURL * easy=curl_easy_init();
    if (!easy) return lite_uv_throw(L, ENOMEM);
    easy_t * leasy=lua_newuserdata(L,sizeof(easy_t));
    if (!leasy){
        curl_easy_cleanup(easy);
        return lite_uv_throw(L, ENOMEM);
    }
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, leasy->error);
    curl_easy_setopt(easy, CURLOPT_PRIVATE,NULL);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, leasy);
    leasy->ctx=ctx;
    leasy->error=LUA_NOREF;
    for (int i=0;i<CB_IDX_LENGTH;i++)
        leasy->on[i]=LUA_NOREF;
    for (int i=0;i<SLIST_IDX_LENGTH;i++)
        leasy->c_ptr_slist[i]=NULL;
    leasy->easy=easy;
    leasy->mimepost=NULL;
    luaL_getmetatable(L, LITE_EASY_MT);
    lua_setmetatable(L,-2);
    return 1;
}

void lite_easy_done_cb(lite_loop_t * ctx,CURL * easy,CURLcode rc){
    easy_t * leasy;
    lua_State * L=ctx->L;
    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &leasy);
    RF_GET(leasy->on[CB_IDX_DONE])
    RF_UNSET(leasy->on[CB_IDX_DONE])
    lua_pushboolean(L, rc==CURLE_OK);
    int n=1;
    if (rc!=CURLE_OK){
        easy_push_error(L,rc);
        lua_pushinteger(L, rc);
        n+=2;
    }
    if (lua_pcall(L, n, 0, 0)){
        lite_loop_push_lua_error(&ctx->errors, L);
        uv_stop(&ctx->loop);
    }
}


#define RAWGET_REF(A) \
    easy_t * leasy=userdata;\
    lua_State * L=leasy->ctx->L;\
    lua_rawgeti(L,LUA_REGISTRYINDEX,leasy->on[CB_IDX_##A]);
#define REGERR_REF \

#define CB_PCALL(nargs,er) \
    if (lua_pcall(L,nargs,1,0)!=LUA_OK){\
        if (RF_ISSET(leasy->error)) \
            RF_UNSET(leasy->error)\
        RF_SET(leasy->error)\
        return er;\
    }

static size_t WRITEFUNCTION_cb(char *buffer, size_t size, size_t nmemb, void *userdata){
    RAWGET_REF(WRITE)
    size_t bytes = size * nmemb;
    lua_pushlstring(L,buffer,bytes);
    CB_PCALL(1,CURL_WRITEFUNC_ERROR)
    if (lua_type(L,-1)!=LUA_TNIL)
        bytes=lua_tointeger(L,-1);
    lua_pop(L, 1);
    return bytes;
}

static size_t READFUNCTION_cb(char *buffer, size_t size, size_t nmemb, void *userdata){
    RAWGET_REF(READ)
    size_t bytes = size * nmemb;
    lua_pushinteger(L,bytes);
    CB_PCALL(1,CURL_READFUNC_ABORT)
    int type=lua_type(L,-1);
    if (type==LUA_TSTRING) {
        const char * res=lua_tolstring(L,-1, &bytes);
        memcpy(buffer, res, bytes);
    }else
        bytes=lua_tointeger(L,-1);
    lua_pop(L, 1);
    return bytes;
}

static int SEEKFUNCTION_cb(void *userdata, curl_off_t offset, int origin){
    RAWGET_REF(SEEK)
    lua_pushinteger(L,offset);
    if (SEEK_SET == origin) lua_pushliteral(L, "set");
    else if (SEEK_CUR == origin) lua_pushliteral(L, "cur");
    else if (SEEK_END == origin) lua_pushliteral(L, "end");
    else lua_pushinteger(L, origin);
    CB_PCALL(2,CURL_SEEKFUNC_FAIL)
    int ret=CURL_SEEKFUNC_OK;
    if (lua_type(L,-1)!=LUA_TNIL)
        ret=lua_tointeger(L,-1);
    lua_pop(L, 1);
    return ret;
}

static size_t HEADERFUNCTION_cb(char *buffer, size_t size,
                              size_t nitems, void *userdata)
{
    RAWGET_REF(HEADER)
    size_t bytes = size * nitems;
    lua_pushlstring(L,buffer,bytes);
    CB_PCALL(1,CURL_WRITEFUNC_ERROR)
    if (lua_type(L,-1)!=LUA_TNIL)
        bytes=lua_tointeger(L,-1);
    lua_pop(L, 1);
    return bytes;
}

static size_t XFERINFOFUNCTION_cb(void *userdata,
                                curl_off_t dltotal,
                                curl_off_t dlnow,
                                curl_off_t ultotal,
                                curl_off_t ulnow){
    RAWGET_REF(XFERINFO)
    lua_pushinteger(L,dltotal);
    lua_pushinteger(L,dlnow);
    lua_pushinteger(L,ultotal);
    lua_pushinteger(L,ulnow);
    CB_PCALL(4,1)
    int ret=0;
    if (lua_type(L,-1)!=LUA_TNIL)
        ret=lua_tointeger(L,-1);
    lua_pop(L, 1);
    return ret;
}


#define INVALID_VAL luaL_error(L,"invalid value");
static int easy_opt(lua_State * L){
    easy_t * leasy=lua_touserdata(L,1);
    int type=lua_type(L,2);
    const struct curl_easyoption * curl_opt;
    if (type==LUA_TSTRING)
        curl_opt=curl_easy_option_by_name(lua_tostring(L,2));
    else if (type==LUA_TNUMBER)
        curl_opt=curl_easy_option_by_id(lua_tointeger(L,2));
    else
        curl_opt=NULL;
    if (!curl_opt) return luaL_error(L,"invalid key (unavailable)");
    type=lua_type(L, 3);
    int ret;
    switch (curl_opt->type) {
    case CURLOT_LONG:{
        if (type==LUA_TBOOLEAN){
            ret=curl_easy_setopt(leasy->easy, curl_opt->id, lua_toboolean(L, 3));
            break;
        }
    }
    case CURLOT_VALUES:
    case CURLOT_OFF_T:{
        if (type!=LUA_TNUMBER) return INVALID_VAL
        ret=curl_easy_setopt(leasy->easy, curl_opt->id, lua_tointeger(L, 3));
        break;
    }
    case CURLOT_STRING:{
        const char * value;
        if (type==LUA_TNIL) value=NULL;
        else if(type==LUA_TSTRING) value=lua_tostring(L, 3);
        else return INVALID_VAL
        ret=curl_easy_setopt(leasy->easy, curl_opt->id, value);
        break;
    }
    case CURLOT_BLOB:{
        if (type==LUA_TNIL) ret=curl_easy_setopt(leasy->easy, curl_opt->id, NULL);
        else if (type!=LUA_TSTRING) return INVALID_VAL
        else {
            struct curl_blob blob;
            blob.data=(void *)lua_tolstring(L,3,&blob.len);
            blob.flags=CURL_BLOB_COPY;
            ret=curl_easy_setopt(leasy->easy, curl_opt->id, blob);
        }
        break;
    }
    case CURLOT_SLIST:{
        int pos;
        switch (curl_opt->id) {
            #define CASE_SLIST(A) case CURLOPT_##A : pos=SLIST_IDX_##A;break;
            CASE_SLIST(HTTPHEADER)
            CASE_SLIST(QUOTE)
            CASE_SLIST(POSTQUOTE)
            CASE_SLIST(TELNETOPTIONS)
            CASE_SLIST(PREQUOTE)
            CASE_SLIST(HTTP200ALIASES)
            CASE_SLIST(MAIL_RCPT)
            CASE_SLIST(RESOLVE)
            CASE_SLIST(PROXYHEADER)
            CASE_SLIST(CONNECT_TO)
            #undef CASE_SLIST
            default:return luaL_error(L,"invalid key (not implemented)");
        }
        if (type==LUA_TNIL){
            ret=curl_easy_setopt(leasy->easy,curl_opt->id,NULL);
            if (ret==CURLE_OK){
                if (leasy->c_ptr_slist[pos]!=NULL){
                    curl_slist_free_all(leasy->c_ptr_slist[pos]);
                    leasy->c_ptr_slist[pos]=NULL;
                }
            }
            break;
        }
        if (type!=LUA_TTABLE) 
            return INVALID_VAL
        struct curl_slist *slist=NULL;
        int len=lua_objlen(L,3);
        if (len>0){
            for(int i=1;i<=len;i++){
                lua_rawgeti(L, 3, i);
                if (lua_type(L, -1)!=LUA_TSTRING){
                    lua_pop(L, 1);
                    if (slist!=NULL) curl_slist_free_all(slist);
                    return INVALID_VAL
                }
                slist=curl_slist_append(slist,lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        }
        ret=curl_easy_setopt(leasy->easy,curl_opt->id,slist);
        if (ret==CURLE_OK){
            if (leasy->c_ptr_slist[pos]!=NULL)
                curl_slist_free_all(leasy->c_ptr_slist[pos]);
            leasy->c_ptr_slist[pos]=slist;
        }else if (slist!=NULL)
            curl_slist_free_all(slist);
        break;
    }
    case CURLOT_FUNCTION:
    {
        int * l_func_ref;
        void * c_ptr_cb;
        int ud_id;
        int * l_second_func_ref=NULL;// cb that share same ud
        switch (curl_opt->id) {
            #define CASE_CB_NB(A) \
                case CURLOPT_##A##FUNCTION : \
                    l_func_ref=&leasy->on[CB_IDX_##A];\
                    c_ptr_cb=A##FUNCTION##_cb;\
                    ud_id=CURLOPT_##A##DATA;
            #define CASE_CB(A) CASE_CB_NB(A) break;  
            CASE_CB(WRITE)
            CASE_CB(READ)
            CASE_CB(HEADER)
            //CASE_CB(DEBUG)
            //CASE_CB(SSL_CTX_)
            //CASE_CB(SOCKOPT)
            //CASE_CB(OPENSOCKET)
            CASE_CB(SEEK)
            //CASE_CB(SSH_KEY)
            //CASE_CB(INTERLEAVE)
            //CASE_CB_NB(CHUNK_BGN_) l_second_func_ref = &leasy->l_func_ref[L_FUNC_REF_CHUNK_END_];break;
            //CASE_CB_NB(CHUNK_END_) l_second_func_ref = &leasy->l_func_ref[L_FUNC_REF_CHUNK_BGN_];break;
            //CASE_CB(FNMATCH_)
            //CASE_CB(CLOSESOCKET)
            CASE_CB(XFERINFO)
            //CASE_CB(RESOLVER_START_)
            //CASE_CB(TRAILER)
            //CASE_CB(HSTSREAD)
            //CASE_CB(HSTSWRITE)
            //CASE_CB(PREREQ)
            //CASE_CB(SSH_HOSTKEY)
            #undef CASE_CB
            #undef CASE_CB_NB
            default : return luaL_error(L,"invalid key (not implemented)");
        }
        if (type==LUA_TNIL){
            ret=curl_easy_setopt(leasy->easy,curl_opt->id,NULL);
            if (ret==CURLE_OK){
                if (RF_ISSET(*l_func_ref)){
                    RF_UNSET(*l_func_ref)
                    *l_func_ref=LUA_NOREF;
                }
                if (l_second_func_ref==NULL || *l_second_func_ref==LUA_NOREF){
                    ret=curl_easy_setopt(leasy->easy,ud_id,NULL);
                }
            }
            break;
        }
        if (type!=LUA_TFUNCTION)
            return INVALID_VAL

        ret=curl_easy_setopt(leasy->easy,curl_opt->id,c_ptr_cb);
        if (ret==CURLE_OK){
            if (RF_ISSET(*l_func_ref))
                RF_UNSET(*l_func_ref)
            else if (l_second_func_ref==NULL || *l_second_func_ref==LUA_NOREF)
                ret=curl_easy_setopt(leasy->easy,ud_id,leasy);
            RF_SET(*l_func_ref)
        }
        break;
  
    }
    case CURLOT_OBJECT:
        if (curl_opt->id==CURLOPT_MIMEPOST){
            if (type==LUA_TNIL){
                ret=curl_easy_setopt(leasy->easy,curl_opt->id,NULL);
                if (ret==CURLE_OK && leasy->mimepost!=NULL){
                    curl_mime_free(leasy->mimepost);
                    leasy->mimepost=NULL;
                }
                break;
            }
            if (type!=LUA_TTABLE)
                return INVALID_VAL
            curl_mime *mime;
            curl_mimepart *part;
            mime=curl_mime_init(leasy->easy);
            if (!mime) return lite_uv_throw(L, ENOMEM);
            int len=lua_objlen(L,3);
            if (len>0){
                for (int i=1;i<=len;i++){
                    lua_rawgeti(L, 3, i);
                    type=lua_type(L, -1);
                    if (type!=LUA_TTABLE){
                        lua_pop(L, 1);
                        curl_mime_free(mime);
                        return INVALID_VAL
                    }
                    part = curl_mime_addpart(mime);
                    lua_pushnil(L);
                    while (lua_next(L, -2)){
                        type=lua_type(L, -2);
                        if (type!=LUA_TSTRING){
                            lua_pop(L, 3);
                            curl_mime_free(mime);
                            return INVALID_VAL
                        }
                        type=lua_type(L,-1);
                        const char * key=lua_tostring(L, -2);
                        if (type==LUA_TSTRING){
                            if (strncmp(key,"name",4)==0)
                                curl_mime_name(part,lua_tostring(L, -1));
                            else if (strncmp(key,"type",4)==0)
                                curl_mime_type(part,lua_tostring(L, -1));
                            else if (strncmp(key,"filename",8)==0)
                                curl_mime_filename(part,lua_tostring(L, -1));
                            else if (strncmp(key,"encoder",7)==0)
                                curl_mime_encoder(part,lua_tostring(L, -1));
                            else if (strncmp(key,"data",4)==0){
                                size_t sz;
                                const char * value=lua_tolstring(L, -1,&sz);
                                curl_mime_data(part,value,sz);
                            }
                            else{
                                lua_pop(L, 3);
                                curl_mime_free(mime);
                                return INVALID_VAL
                            }
                        }/*else if (type==LUA_TTABLE && strcmp(key,"headers")==0){

                        }*/
                        else{
                            lua_pop(L, 3);
                            curl_mime_free(mime);
                            return INVALID_VAL
                        }
                        
                        lua_pop(L, 1);
                    }
                }
            }
            ret=curl_easy_setopt(leasy->easy, curl_opt->id,mime);
            if (ret==CURLE_OK){
                if (leasy->mimepost!=NULL)
                    curl_mime_free(leasy->mimepost);
                leasy->mimepost=mime;
            }else  
                curl_mime_free(mime);
            break;
        }
        return luaL_error(L, "invalid key (not implemented)"); 
    case CURLOT_CBPTR:return luaL_error(L, "invalid key (reserved)"); 
    default: return luaL_error(L,"invalid key (unsupported)");
    }
    if (ret!=CURLE_OK){
        return luaL_error(L,curl_easy_strerror(ret));
        easy_push_error(L, ret);
        return lua_error(L);
    }
    return 0;
}
#undef INVALID_VAL
    

static void header_push_error(lua_State * L,CURLHcode rc);
static int easy_info(lua_State * L){
    easy_t * leasy=lua_touserdata(L,1);
    size_t sz;
    const char * key=luaL_checklstring(L,2,&sz);
    lua_getfield(L,lua_upvalueindex(1),key);
    if (lua_type(L,-1)!=LUA_TNUMBER) {
        if (sz>5 && strncmp(key,"header",6)==0){
            if (sz==7 && key[6]=='s'){
                // headers
                lua_newtable(L);
                struct curl_header *prev = NULL;
                struct curl_header *h;
                unsigned int origin = CURLH_HEADER;//| CURLH_1XX | CURLH_TRAILER | CURLH_CONNECT;
                int i=0;
                while((h = curl_easy_nextheader(leasy->easy, origin, -1, prev))) {
                    lua_pushstring(L,h->name);
                    lua_pushstring(L,h->value);
                    lua_pushvalue(L,-2);
                    lua_pushvalue(L, -2);
                    lua_rawset(L,-5);
                    lua_rawseti(L,-3,++i);
                    prev = h;
                }
                return 1;
            }else if (sz==6){
                struct curl_header *out;
                const char * name=luaL_checkstring(L, 3);
                CURLHcode rc=curl_easy_header(leasy->easy, name, 0, CURLH_HEADER, -1, &out);
                if (rc!=CURLHE_OK){
                    header_push_error(L,rc);
                    return lua_error(L);
                }
                lua_pushstring(L, out->value);
                return 1;
            }
        }
        return luaL_error(L,"invalid key (unavailable)");
    }
    int ikey=lua_tointeger(L,-1);lua_pop(L, 1);
    int ikey_type = CURLINFO_TYPEMASK & ikey;
    switch (ikey_type) {
        case CURLINFO_STRING :{
            const char * out=NULL;
            curl_easy_getinfo(leasy->easy,ikey,&out);
            if (out==NULL) lua_pushnil(L);
            else lua_pushstring(L,out);
            return 1;
        }
        case CURLINFO_LONG:{
            long out=0;
            curl_easy_getinfo(leasy->easy,ikey,&out);
            lua_pushinteger(L,out);
            return 1;
        }
        case CURLINFO_OFF_T:{
            curl_off_t out=0;
            curl_easy_getinfo(leasy->easy,ikey,&out);
            lua_pushinteger(L,out);
            return 1;
        }
        case CURLINFO_DOUBLE:{
            double out=0.0;
            curl_easy_getinfo(leasy->easy,ikey,&out);
            lua_pushnumber(L,out);
            return 1;
        }
        case CURLINFO_SLIST:{
            struct curl_slist *out;
            curl_easy_getinfo(leasy->easy,ikey,&out);
            lua_newtable(L);
            int i=0;
            while (out) {
                lua_pushstring(L,out->data);
                lua_rawseti(L,-2,++i);
                out = out->next;
            }
            curl_slist_free_all(out);
            return 1;
        }
        default: return luaL_error(L,"invalid key (not implemented)");
    }
}

static int easy_start(lua_State * L){
    easy_t * leasy=lua_touserdata(L,1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_settop(L, 2);
    if (RF_ISSET(leasy->on[CB_IDX_DONE]))
        RF_UNSET(leasy->on[CB_IDX_DONE])
    RF_SET(leasy->on[CB_IDX_DONE])
    leasy->error_buf[0]='\0';
    int ret=curl_multi_add_handle(leasy->ctx->multi.handle, leasy->easy);
    if (ret!=CURLM_OK){
        lite_multi_push_error(L, ret);
        return lua_error(L);
    }
    return 0;
}

static int easy_close(lua_State * L){
    
}

static void easy_upval_info_fields(lua_State * L);
void lite_easy_reg(lua_State * L){
    luaL_newmetatable(L, LITE_EASY_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    easy_upval_info_fields(L);
    lua_pushcclosure(L, easy_info, 1);
    lua_setfield(L, -2, "info");

    static const luaL_Reg easy_mt[] = {
        {"__gc",easy_gc},
        {"opt",easy_opt},
        {"perform",easy_start},
        {NULL,NULL}
    };
    luaL_setfuncs(L, easy_mt, 0);
    lua_pop(L, 1);
}







static void header_push_error(lua_State * L,CURLHcode rc){
    #define HE(A) case CURLHE_##A: lua_pushliteral(L,#A); return;
    switch (rc) {
    HE(OK)
    HE(BADINDEX)
    HE(MISSING)
    HE(NOHEADERS)
    HE(NOREQUEST)
    HE(OUT_OF_MEMORY)
    HE(NOT_BUILT_IN)
    default:
        lua_pushfstring(L,"ERROR (%d)",rc);return;
    }
}
static void easy_upval_info_fields(lua_State * L){
    lua_newtable(L);
    #define IF(A,a) lua_pushstring(L,a);lua_pushinteger(L,CURLINFO_##A);lua_rawset(L,-3);
    IF(EFFECTIVE_URL,"effective_url")
    IF(RESPONSE_CODE,"response_code")
    IF(TOTAL_TIME,"total_time")
    IF(NAMELOOKUP_TIME,"namelookup_time")
    IF(CONNECT_TIME,"connect_time")
    IF(PRETRANSFER_TIME,"pretransfer_time")
    IF(SIZE_UPLOAD_T,"size_upload_t")
    IF(SIZE_DOWNLOAD_T,"size_download_t")
    IF(SPEED_DOWNLOAD_T,"speed_download_t")
    IF(SPEED_UPLOAD_T,"speed_upload_t")
    IF(HEADER_SIZE,"header_size")
    IF(REQUEST_SIZE,"request_size")
    IF(SSL_VERIFYRESULT,"ssl_verifyresult")
    IF(FILETIME,"filetime")
    IF(FILETIME_T,"filetime_t")
    IF(CONTENT_LENGTH_DOWNLOAD_T,"content_length_download_t")
    IF(CONTENT_LENGTH_UPLOAD_T,"content_length_upload_t")
    IF(STARTTRANSFER_TIME,"starttransfer_time")
    IF(CONTENT_TYPE,"content_type")
    IF(REDIRECT_TIME,"redirect_time")
    IF(REDIRECT_COUNT,"redirect_count")
    IF(PRIVATE,"private")
    IF(HTTP_CONNECTCODE,"http_connectcode")
    IF(HTTPAUTH_AVAIL,"httpauth_avail")
    IF(PROXYAUTH_AVAIL,"proxyauth_avail")
    IF(OS_ERRNO,"os_errno")
    IF(NUM_CONNECTS,"num_connects")
    IF(SSL_ENGINES,"ssl_engines")
    IF(COOKIELIST,"cookielist")
    IF(FTP_ENTRY_PATH,"ftp_entry_path")
    IF(REDIRECT_URL,"redirect_url")
    IF(PRIMARY_IP,"primary_ip")
    IF(APPCONNECT_TIME,"appconnect_time")
    IF(CERTINFO,"certinfo")
    IF(CONDITION_UNMET,"condition_unmet")
    IF(RTSP_SESSION_ID,"rtsp_session_id")
    IF(RTSP_CLIENT_CSEQ,"rtsp_client_cseq")
    IF(RTSP_SERVER_CSEQ,"rtsp_server_cseq")
    IF(RTSP_CSEQ_RECV,"rtsp_cseq_recv")
    IF(PRIMARY_PORT,"primary_port")
    IF(LOCAL_IP,"local_ip")
    IF(LOCAL_PORT,"local_port")
    IF(ACTIVESOCKET,"activesocket")
    IF(TLS_SSL_PTR,"tls_ssl_ptr")
    IF(HTTP_VERSION,"http_version")
    IF(PROXY_SSL_VERIFYRESULT,"proxy_ssl_verifyresult")
    IF(SCHEME,"scheme")
    IF(TOTAL_TIME_T,"total_time_t")
    IF(NAMELOOKUP_TIME_T,"namelookup_time_t")
    IF(CONNECT_TIME_T,"connect_time_t")
    IF(PRETRANSFER_TIME_T,"pretransfer_time_t")
    IF(STARTTRANSFER_TIME_T,"starttransfer_time_t")
    IF(REDIRECT_TIME_T,"redirect_time_t")
    IF(APPCONNECT_TIME_T,"appconnect_time_t")
    IF(RETRY_AFTER,"retry_after")
    IF(EFFECTIVE_METHOD,"effective_method")
    IF(PROXY_ERROR,"proxy_error")
    IF(REFERER,"referer")
    IF(CAINFO,"cainfo")
    IF(CAPATH,"capath")
    IF(XFER_ID,"xfer_id")
    IF(CONN_ID,"conn_id")
    IF(QUEUE_TIME_T,"queue_time_t")
    IF(USED_PROXY,"used_proxy")
    #undef IF
}

static void easy_push_error(lua_State * L,CURLcode rc){
    #define EE(A) case CURLE_##A: lua_pushliteral(L,#A); return;
    switch (rc) {
    EE(OK)
    EE(UNSUPPORTED_PROTOCOL)
    EE(FAILED_INIT)
    EE(URL_MALFORMAT)
    EE(NOT_BUILT_IN)
    EE(COULDNT_RESOLVE_PROXY)
    EE(COULDNT_RESOLVE_HOST)
    EE(COULDNT_CONNECT)
    EE(WEIRD_SERVER_REPLY)
    EE(REMOTE_ACCESS_DENIED)
    EE(FTP_ACCEPT_FAILED)
    EE(FTP_WEIRD_PASS_REPLY)
    EE(FTP_ACCEPT_TIMEOUT)
    EE(FTP_WEIRD_PASV_REPLY)
    EE(FTP_WEIRD_227_FORMAT)
    EE(FTP_CANT_GET_HOST)
    EE(HTTP2)
    EE(FTP_COULDNT_SET_TYPE)
    EE(PARTIAL_FILE)
    EE(FTP_COULDNT_RETR_FILE)
    EE(OBSOLETE20)
    EE(QUOTE_ERROR)
    EE(HTTP_RETURNED_ERROR)
    EE(WRITE_ERROR)
    EE(OBSOLETE24)
    EE(UPLOAD_FAILED)
    EE(READ_ERROR)
    EE(OUT_OF_MEMORY)
    EE(OPERATION_TIMEDOUT)
    EE(OBSOLETE29)
    EE(FTP_PORT_FAILED)
    EE(FTP_COULDNT_USE_REST)
    EE(OBSOLETE32)
    EE(RANGE_ERROR)
    EE(HTTP_POST_ERROR)
    EE(SSL_CONNECT_ERROR)
    EE(BAD_DOWNLOAD_RESUME)
    EE(FILE_COULDNT_READ_FILE)
    EE(LDAP_CANNOT_BIND)
    EE(LDAP_SEARCH_FAILED)
    EE(OBSOLETE40)
    EE(FUNCTION_NOT_FOUND)
    EE(ABORTED_BY_CALLBACK)
    EE(BAD_FUNCTION_ARGUMENT)
    EE(OBSOLETE44)
    EE(INTERFACE_FAILED)
    EE(OBSOLETE46)
    EE(TOO_MANY_REDIRECTS)
    EE(UNKNOWN_OPTION)
    EE(SETOPT_OPTION_SYNTAX)
    EE(OBSOLETE50)
    EE(OBSOLETE51)
    EE(GOT_NOTHING)
    EE(SSL_ENGINE_NOTFOUND)
    EE(SSL_ENGINE_SETFAILED)
    EE(SEND_ERROR)
    EE(RECV_ERROR)
    EE(OBSOLETE57)
    EE(SSL_CERTPROBLEM)
    EE(SSL_CIPHER)
    EE(PEER_FAILED_VERIFICATION)
    EE(BAD_CONTENT_ENCODING)
    EE(OBSOLETE62)
    EE(FILESIZE_EXCEEDED)
    EE(USE_SSL_FAILED)
    EE(SEND_FAIL_REWIND)
    EE(SSL_ENGINE_INITFAILED)
    EE(LOGIN_DENIED)
    EE(TFTP_NOTFOUND)
    EE(TFTP_PERM)
    EE(REMOTE_DISK_FULL)
    EE(TFTP_ILLEGAL)
    EE(TFTP_UNKNOWNID)
    EE(REMOTE_FILE_EXISTS)
    EE(TFTP_NOSUCHUSER)
    EE(OBSOLETE75)
    EE(OBSOLETE76)
    EE(SSL_CACERT_BADFILE)
    EE(REMOTE_FILE_NOT_FOUND)
    EE(SSH)
    EE(SSL_SHUTDOWN_FAILED)
    EE(AGAIN)
    EE(SSL_CRL_BADFILE)
    EE(SSL_ISSUER_ERROR)
    EE(FTP_PRET_FAILED)
    EE(RTSP_CSEQ_ERROR)
    EE(RTSP_SESSION_ERROR)
    EE(FTP_BAD_FILE_LIST)
    EE(CHUNK_FAILED)
    EE(NO_CONNECTION_AVAILABLE)
    EE(SSL_PINNEDPUBKEYNOTMATCH)
    EE(SSL_INVALIDCERTSTATUS)
    EE(HTTP2_STREAM)
    EE(RECURSIVE_API_CALL)
    EE(AUTH_ERROR)
    EE(HTTP3)
    EE(QUIC_CONNECT_ERROR)
    EE(PROXY)
    EE(SSL_CLIENTCERT)
    EE(UNRECOVERABLE_POLL)
    EE(TOO_LARGE)
    EE(ECH_REQUIRED)
    default:
        lua_pushfstring(L,"ERROR (%d)",rc);return;
    }
}



#endif