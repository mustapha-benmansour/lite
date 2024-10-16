


local lite=require'lite'
local sha1=require'sha1'
local b64=require'base64'


local status_names={}

local function status_name(code)
    local text=status_names[code]
    if not text then
        text=lite.http_status_name(code)
        status_names[code]=text
    end
    return text
end


local http={}

local route_mt={}
route_mt.__index=route_mt

local function route_mt_any(self,m,p,f)
    self._r[#self._r+1] = {m,p,f}
end

function route_mt:get(p,f)
    route_mt_any(self,'GET',p,f)
end
function route_mt:post(p,f)
    route_mt_any(self,'POST',p,f)
end

function route_mt:ws(p,t)
    self._w[#self._w+1] = {p,t}
end

local req_conn_mt={}
req_conn_mt.__index=req_conn_mt


local res_conn_mt={}
res_conn_mt.__index=res_conn_mt

function res_conn_mt:new(tcp)
    local obj={tcp=tcp}
    return setmetatable(obj,self)
end



local server_version=('http_server/v%s %s'):format('1.0',lite.version()) 


---@param opt {status_code:integer?,status_text:string?,content_type:string?,content_length:integer?,headers:string[]}
function res_conn_mt:head(opt)
    local code=opt.status_code or 200
    local text=opt.status_text or status_name(code)
    local head={}
    table.insert(head,('HTTP/1.1 %d %s\r\n'):format(code,text))
    table.insert(head,('Content-Type: %s\r\n'):format(opt.content_type or 'text/html'))
    if opt.headers then
        for _,v in ipairs(opt.headers) do
            table.insert(head,('%s\r\n'):format(v))
        end
    end
    self._head={}
    if opt.content_length then
       table.insert(head,('Content-Length: %d\r\n'):format(opt.content_length)) 
       self._head.content_length=opt.content_length
    else
        table.insert(head,'Transfer-Encoding: chunked\r\n') 
        self._head.chunked=true
    end
    table.insert(head,('Date: %s\r\n'):format(os.date('!%a, %d %b %Y %X GMT')))
    table.insert(head,('Server: %s\r\n\r\n'):format(server_version))
    self.tcp:write(table.concat(head))
end

function res_conn_mt:write(data)
    if not self._head then
        self:head{}
    end
    if #data>0 then
        if self._head.content_length then
            self.tcp:write(data)
        else
            self.tcp:write(tostring(#data)..'\r\n'..data..'\r\n')
        end
    end
end

function res_conn_mt:done()
    if not self._head then
        self:head{}
    end
    if self._head.chunked then
        self.tcp:try_write'0\r\n\r\n'
    end
    self.tcp:shutdown(function ()
        self.tcp:close()
    end)
end


local ws_conn_mt={}
ws_conn_mt.__index=ws_conn_mt

function ws_conn_mt:new(tcp)
    local obj={tcp=tcp}
    return setmetatable(obj,self)
end


---@param f fun(msg)
function ws_conn_mt:on_message(f)
    self._on_message=f
end

---@param f fun(msg)
function ws_conn_mt:on_error(f,msg)
    self._on_error=f
end

---@param f fun(msg)
function ws_conn_mt:on_close(f,msg)
    self._on_close=f
end

---@param msg string 
function ws_conn_mt:send(msg)
    self.tcp:try_write(lite.ws_build_frame(msg))
end

function ws_conn_mt:close()
    
end







local function res_header(opt)
    assert(opt.content_length)
    local code=opt.status_code or 200
    local text=opt.status_text or lite.http_status_name(code)
    return table.concat({
        ('HTTP/1.1 %d %s\r\n'):format(code,text),
        ('Content-Type: %s\r\n'):format(opt.content_type or 'text/html'),
        ('Content-Length: %d\r\n'):format(opt.content_length),
        ('Date: %s\r\n'):format(opt.date or os.date('!%a, %d %b %Y %X GMT')),
        ('Server: %s\r\n\r\n'):format(opt.server or server_version),
        --opt.headers and (table.concat(opt.headers,'\r\n')..'\r\n') or '\r\n',
    })
end

local function res_code(code)
    code = code or 500
    local data=lite.http_status_name(code)
    local r= res_header({status_code=code,status_text=data,content_length=#data})..data
    print(r)
    return r
end


local function make_matchs(...)
    if ... then
        return {n=select('#',...),...}
    end
end

http.server=function (ip,port,f)
    local route=setmetatable({
        _r={},
        _w={}
    },route_mt)
    f(route)
    local server
    local tcps={}
    return {
        start=function ()
            server=lite.tcp()
            server:bind(ip or '0.0.0.0',port or 80)
            server:listen(128,function (ok,res)
                print('CON')
                if ok then
                    ok,res=pcall(lite.tcp)
                    if ok then
                        local tcp=res
                        ok,res=pcall(server.accept,server,tcp)
                        if ok then
                            tcps[tcp]=true
                            print('accept client')
                            tcp:on_close(function ()
                                print('close client')
                            end)
                            tcp:parser('http')

                            local request
                            local response
                            local matchs
                            local route_caller
                            local ws_conn
                            local buf
                            tcp:read_start(function (...)
                                local what,result,other=...
                                local code
                                if not what then
                                    if result=='EOF' then
                                        goto eclose 
                                    end
                                    print(result)
                                    code=500 goto ecode
                                end
                                print('WHAT',...)
                                if ws_conn and what=='control' then
                                    local msg=other
                                    if result=='close' then
                                        what,result=pcall(ws_conn._on_close,msg)
                                        if not what then
                                            print(result) 
                                        end
                                        --todo send error
                                        goto eclose
                                    end
                                    if result=='ping' or result=='pong' then
                                        --todo send pong/ping
                                        return 
                                    end
                                    --todo send error
                                    goto eclose
                                end
                                if ws_conn and what=='data_begin' then
                                    buf={}
                                    return 
                                end
                                if ws_conn and what=='data_payload' then
                                    local msg=result
                                    table.insert(buf,msg)
                                    return 
                                end
                                if ws_conn and what=='data_end' then
                                    local msg=table.concat(buf)
                                    print('MSG',msg)
                                    what,result=pcall(ws_conn._on_message,msg)
                                    if what then
                                        return 
                                    end
                                    print(result)
                                    --todo send error
                                    goto eclose
                                end

                                if what=='headers_complete' then
                                    request=result
                                    request.tcp=tcp
                                    if request.upgrade then
                                        local uk=request.headers['Upgrade'] 
                                        print('uk',uk)
                                        if uk=='websocket' then
                                            for i=1,#route._w do
                                                matchs=make_matchs(request.url:match(route._w[i][1]))
                                                if matchs then
                                                    local key=request.headers['Sec-WebSocket-Key']
                                                    if key and #key==24 then
                                                        local ksha=b64.encode(sha1(key..'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'))
                                                        tcp:try_write(table.concat({
                                                            'HTTP/1.1 101 Switching Protocols\r\n',
                                                            'Upgrade: websocket\r\n',
                                                            'Connection: Upgrade\r\n',
                                                            ('Sec-WebSocket-Accept: %s\r\n\r\n'):format(ksha),
                                                            --''
                                                        }))
                                                        ws_conn=ws_conn_mt:new(tcp)
                                                        what,res=pcall(route._w[i][2],request,ws_conn,unpack(matchs,1,matchs.n))
                                                        if what then
                                                            tcp:parser('ws')
                                                            return 
                                                        end
                                                        print(res)
                                                        code=500 goto ecode
                                                    end
                                                    code=400 goto ecode
                                                end
                                            end
                                            code=404 goto ecode
                                        end
                                        code=501 goto ecode
                                    end
                                    for i=1,#route._r do
                                        if request.method==route._r[i][1] then
                                            matchs=make_matchs(request.url:match(route._r[i][2]))
                                            --print(matchs,req.url,route._r[i][2])
                                            if matchs then
                                                route_caller=route._r[i][3]
                                                response=res_conn_mt:new(tcp)
                                                return 
                                            end
                                        end
                                    end
                                    code=404 goto ecode
                                end
                                if what=='message_complete' then
                                    print('message_complete')
                                    what,res=pcall(route_caller,request,response,unpack(matchs,1,matchs.n))
                                    if what then
                                        --client.tcp:try_write(res_code(200))
                                        return 
                                    end
                                    print(res)
                                    code=500 goto ecode
                                end
                                do
                                    print('not imp cb ',what)
                                    return 
                                end
                                ::ecode::
                                    tcp:try_write(res_code(code))
                                ::eclose::
                                    tcps[tcp]=nil
                                    tcp:close()
                            end)
                            return 
                        end
                    end
                end
                print(res)
            end) 
        end,
        close=function ()
            while true do
                local tcp=(next(tcps))
                if not tcp then
                    break
                end
                tcps[tcp]=nil
                tcp:close()
            end
            server:close()
        end
    }
end


return http