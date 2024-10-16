

var tn=['a','b','c','d','e','f','g','h','i','j','k','l','m']
var Tn='ABCDEFGHIJKLM'

var rnd=(min,max)=>Math.floor(Math.random() * (max - min + 1) + min);

var random_key=()=>{
    var n=rnd(5,100)
    if (n % 2 == 1)
        n=n+1;
    var ts=[]
    for (let i=0;i<n/2;i++){
        ts[ts.length]=tn[rnd(0,tn.length-1)]
    }
    var Ts=''
    for (var i=0;i<n/2;i++){
        var p= rnd(1,Tn.length-1)
        Ts=Ts+Tn.charAt(p)
    }
    return ts.join('') +Ts
}

print=()=>{}

for (var i=1;i<=10000;i++){
    print(i,random_key())
}