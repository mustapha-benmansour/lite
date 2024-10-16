

local lite=require'lite'

local loop=lite.state_loop()

---@param f function
---@param ms integer
function _G.setTimeout(f,ms)
    local timer=loop:timer()
    timer:start(f,ms,0)
    return timer
end

function _G.clearTimeout(ud)
    ud:close()
end

---@param f function
---@param ms integer
function _G.setInterval(f,ms)
    local timer=loop:timer()
    timer:start(f,0,ms)
    return timer
end

function _G.clearInterval(ud)
    ud:close()
end







package.preload['http']=function ()
    
    ---@class node-api.http
    ---@field private tcp any
    local http_mt={}
    http_mt.__index=http_mt

    local http={}
    ---@param f fun()
    function http.createServer(f)
        local self={}
        self.callback=f
        self.tcp=loop:tcp()
        return setmetatable(self,http_mt)
    end
    
    function http_mt:listen(port,ip)
        self.tcp:bind(ip or '0.0.0.0',port or 80)
    end


    return http
end










assert(loadfile((...)))()

loop:run()

