


print('here')

local lite=require'lite'
print('here')
local e=lite.easy()

e.url='http://google.com'

e.writefunction=function(...)
    print('writefunction',...)
end

e(function (...)
    print('perform',...)
end)
print(e)


do 
    lite.run() 
    return 
end


_G.ss_manager={}

local lite=require'lite'
for k,v in pairs(lite.json) do
    print(k,v)
end
local http=require'http'

local ws_list={}


local ws_handler={}



ws_handler.on_open=function (ws_conn,...)
    print('on_open',ws_conn,...)
end
ws_handler.on_message=function (ws_conn,...)
    print('on_message',ws_conn,...)
    local msg=assert(lite.json.decode(...))
    if msg.data.codename=='job-count' then
        if msg.need_res then
            local nmsg={id=msg.id,type='res',data='513'}
            ws_conn:send(lite.json.encode(nmsg))
        end
    end
end
ws_handler.on_error=function (ws_conn,...)
    print('on_error',ws_conn,...)
end
ws_handler.on_close=function (ws_conn,...)
    print('on_close',ws_conn,...)
end



local server
local function create_server()

    local function write_file(res_conn,filename,content_type)
        local f=io.open('webpage/'..filename,'r')
        if not f then
            res_conn:head{status_code=404}
            res_conn:write('Not Found')
            res_conn:done()
            return 
        end
        local data=f:read'*a'
        res_conn:head{content_length=#data,content_type=content_type}
        res_conn:write(data)
        f:close()
        res_conn:done()
    end

    server=http.server(nil,8686,function(route)
        route:get('^/$',function (req_conn,res_conn,...) write_file(res_conn,'index.html','text/html')  end)
        route:get('^/ui.min.css$',function (req_conn,res_conn,...) write_file(res_conn,'ui.min.css','text/css')  end)
        route:get('^/main.css$',function (req_conn,res_conn,...) write_file(res_conn,'main.css','text/css')  end)
        route:get('^/main.js$',function (req_conn,res_conn,...) write_file(res_conn,'main.js','text/javascript')  end)
        route:ws('/ws',function (request,ws_conn,...)
            ws_handler.on_open(ws_conn)

            ws_conn:on_message(function (...)
                ws_handler.on_message(ws_conn,...)
            end)
            ws_conn:on_error(function (...)
                ws_handler.on_error(ws_conn,...)
            end)
            ws_conn:on_close(function (...)
                ws_handler.on_close(ws_conn,...)
            end)
        end)
    end)
    return server
end

function ss_manager.start()
    create_server().start()
end



ss_manager.start()




lite.run()
