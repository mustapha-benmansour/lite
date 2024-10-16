
math.randomseed(os.time())
local tn={'a','b','c','d','e','f','g','h','i','j','k','l','m'}
local Tn='ABCDEFGHIJKLM'
local function random_key()
    local n=math.random(5,100)
    if n % 2 == 1 then
        n=n+1
    end
    local ts={}
    for i=1,n/2 do
        table.insert(ts,tn[math.random(1,#tn)])
    end
    local Ts=''
    for i=1,n/2 do
        local p=math.random(1,#tn)
        Ts=Ts..Tn:sub(p,p)
    end
    return table.concat(ts)..Ts
end

function print(...)
    
end

for i=1,10000 do
    print(i,random_key())
end