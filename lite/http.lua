


local lite=require'lite'
local sha1=require'lite.sha1'
local b64=require'base64'


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

local server_version=('http_server/v%s %s'):format('1.0',lite.version()) 
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
    local clients={}
    return {
        start=function ()
            server=lite.tcp()
            server:bind(ip or '0.0.0.0',port or 80)
            local function reader(client,what,res)
                local code
                if not what then
                    if res=='EOF' then
                        goto eclose
                    end
                    code=500 goto ecode
                end
                if what=='headers_complete' then
                    local req=res
                    if req.upgrade then
                        local uk=req.headers['Upgrade'] 
                        if uk and req.headers[uk]=='websocket' then
                            for i=1,#route._w do
                                local matchs=make_matchs(req.url:match(route._w[i][1]))
                                if matchs then
                                    local key=req.headers['Sec-WebSocket-Key']
                                    if key and #key==24 then
                                        local ksha=b64.encode(sha1(key..'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'))
                                        client.tcp:try_write(table.concat({
                                            'HTTP/1.1 101 Switching Protocols\r\n',
                                            'Upgrade: websocket\r\n',
                                            'Connection: Upgrade\r\n',
                                            ('Sec-WebSocket-Accept: %s\r\n\r\n'):format(ksha),
                                            --''
                                        }))
                                        what,res=pcall(route._w[i][2],unpack(matchs,1,matchs.n))
                                        if what then
                                            client.req=req
                                            client.ws=res
                                            client.tcp:use_ws_parser()
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
                        if req.method==route._r[i][1] then
                            local matchs=make_matchs(req.url:match(route._r[i][2]))
                            if matchs then
                                client.req=req
                                client.matchs=matchs
                                client.route=route._r[i][3]
                                return 
                            end
                        end
                    end
                    code=404 goto ecode
                elseif what=='message_complete' then
                    print('message_complete')
                    what,res=pcall(client.route,unpack(client.matchs,1,client.matchs.n))
                    if what then
                        client.tcp:try_write(res_code(200))
                        return 
                    end
                    print(res)
                    code=500 goto ecode
                else
                    print('not imp cb ',what)
                    return 
                end
                ::ecode::
                    client.tcp:try_write(res_code(code))
                ::eclose::
                    clients[client.tcp]=nil
                    client.tcp:close()
            end
            server:listen(128,function (ok,res)
                print('CON')
                if ok then
                    ok,res=pcall(lite.tcp)
                    if ok then
                        local client={}
                        client.tcp=res
                        ok,res=pcall(server.accept,server,client.tcp)
                        if ok then
                            clients[client.tcp]=client
                            print('accept client')
                            client.tcp:on_close(function ()
                                print('close client')
                            end)
                            client.tcp:use_http_parser()
                            client.tcp:read_start(function (...)
                                ok,res=pcall(reader,client,...)
                                if ok then
                                    return                                     
                                end
                                print(res)
                                clients[client.tcp]=nil
                                client.tcp:close()
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
                local client=(next(clients))
                if not client then
                    break
                end
                clients[client.tcp]=nil
                client.tcp:close()
            end
            server:close()
        end
    }
end


return http