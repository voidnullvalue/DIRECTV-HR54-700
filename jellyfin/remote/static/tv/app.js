(function () {
  var $ = function (id) { return document.getElementById(id); };
  var backend = window.JELLYFIN_BACKEND || "";
  var s = {libs:[], lib:0, libFocus:0, page:0, total:0, items:[], zone:"search", index:0, query:"", submitted:"", searching:false, mode:"browse", playing:false, prior:null, serial:0, authenticated:false};
  var pageSize = 6;
  var keys = [["A","B","C","D","E","F","G"],["H","I","J","K","L","M","N"],["O","P","Q","R","S","T","U"],["V","W","X","Y","Z","0","1"],["2","3","4","5","6","7","8"],["9","SPACE","DEL","CLEAR","SEARCH"]];
  var keyButtons = [];
  var messageTimer = null;
  var authTimer = null;
  function api(method,path,body,done,fail){
    var x=new XMLHttpRequest(); x.open(method,backend+path,true);
    if(body!==null)x.setRequestHeader("Content-Type","application/json");
    x.onreadystatechange=function(){if(x.readyState!==4)return;var data={};try{data=JSON.parse(x.responseText||"{}");}catch(e){}
      if(x.status>=200&&x.status<300){if(done)done(data);}else if(fail)fail(data.error||("HTTP "+x.status));};
    x.onerror=function(){if(fail)fail("Network connection failed");};
    x.send(body===null?null:JSON.stringify(body));
  }
  function metric(){var d=document.documentElement,b=document.body;return {innerWidth:window.innerWidth||0,innerHeight:window.innerHeight||0,clientWidth:d.clientWidth,clientHeight:d.clientHeight,screenWidth:window.screen?screen.width:0,screenHeight:window.screen?screen.height:0,bodyOffsetWidth:b.offsetWidth,bodyOffsetHeight:b.offsetHeight,scrollWidth:d.scrollWidth,scrollHeight:d.scrollHeight,rootWidth:$("root").offsetWidth,rootHeight:$("root").offsetHeight};}
  function bridgeKeys(){var names=[],name;try{if(window.dtv)for(name in window.dtv)names.push(name);}catch(e){}return names.slice(0,40).join(",");}
  function telemetry(type,e){var p=metric();p.type=type;p.keyCode=e?(e.keyCode||e.which||0):0;p.key=e&&e.key?e.key:"";if(type==="load")p.bridgeKeys=bridgeKeys();api("POST","/api/tv/event",p,null,null);}
  function show(msg,persistent){clearTimeout(messageTimer);$("message").textContent=msg;$("message").className="";if(!persistent)messageTimer=setTimeout(function(){$("message").className="hidden";},2800);}
  function hideMessage(){$("message").className="hidden";clearTimeout(messageTimer);}
  function text(node,value){node.textContent=value===null||value===undefined?"":String(value);}
  function checkpoint(){return {lib:s.libs[s.lib]?s.libs[s.lib].id:null,page:s.page,searching:s.searching,submitted:s.submitted,query:s.query,zone:s.zone,index:s.index};}
  function saveState(){var p=checkpoint();try{window.localStorage.setItem("jellyfin.tv.state",JSON.stringify(p));}catch(e){}api("POST","/api/tv/state",p,null,null);}
  function restoreState(p){var i;if(!p||!p.zone){try{p=JSON.parse(window.localStorage.getItem("jellyfin.tv.state")||"null");}catch(e){}}if(!p)return;
    for(i=0;i<s.libs.length;i++)if(s.libs[i].id===p.lib){s.lib=i;s.libFocus=i;break;}
    s.page=Math.max(0,parseInt(p.page,10)||0);s.searching=!!p.searching;s.submitted=p.submitted||"";s.query=p.query||"";s.zone=p.zone==="card"?"card":"library";s.index=Math.max(0,parseInt(p.index,10)||0);}
  function button(label,cls,action){var b=document.createElement("button");b.type="button";b.className=cls||"";text(b,label);b.onclick=action;return b;}
  function focus(){var nodes=$("root").getElementsByTagName("button"),i,n;for(i=0;i<nodes.length;i++)nodes[i].className=nodes[i].className.replace(/\s*focus/g,"");
    if(s.mode==="auth")n=$("authAction");else if(s.mode==="keyboard")n=keyButtons[s.index];else if(s.zone==="account")n=$("accountButton");else if(s.zone==="search")n=$("searchButton");else if(s.zone==="library")n=$("libraries").getElementsByTagName("button")[s.libFocus];else if(s.zone==="card")n=$("items").getElementsByTagName("button")[s.index];else if(s.zone==="pager")n=s.index===0?$("prev"):$("next");
    if(n){n.className+=" focus";if(s.zone==="library"){$("libraries").scrollLeft=n.offsetLeft-$("libraries").offsetLeft-20;}}}
  function setFocus(zone,index){s.zone=zone;s.index=index||0;focus();}
  function renderLibraries(){var box=$("libraries");box.innerHTML="";for(var i=0;i<s.libs.length;i++)(function(j){var b=button(s.libs[j].name,"",function(){selectLibrary(j);});box.appendChild(b);}(i));}
  function selectLibrary(i){s.lib=i;s.libFocus=i;s.page=0;s.searching=false;s.submitted="";saveState();loadItems("card",0);}
  function renderItems(){var box=$("items");box.innerHTML="";for(var i=0;i<s.items.length;i++)(function(j){var it=s.items[j],b=button("","card"+(it.playable?"":" unplayable"),function(){play(j);});var title=document.createElement("span"),meta=document.createElement("span");title.className="title";meta.className="meta";text(title,it.name);text(meta,(it.year?it.year+"  ·  ":"")+(it.playable?"SELECT TO PLAY":it.type||"BROWSE ONLY"));b.appendChild(title);b.appendChild(meta);box.appendChild(b);}(i));
    var lib=s.libs[s.lib];$("heading").textContent=s.searching?'Results: '+s.submitted:(lib?lib.name:"All titles");
    $("page").textContent="PAGE "+(s.page+1)+" / "+Math.max(1,Math.ceil(s.total/pageSize));
    $("prev").disabled=s.page===0;$("next").disabled=(s.page+1)*pageSize>=s.total;
    var libs=$("libraries").getElementsByTagName("button");for(i=0;i<libs.length;i++)libs[i].className=i===s.lib?"active":"";
  }
  function loadItems(zone,index){var serial=++s.serial,lib=s.libs[s.lib],path="/api/items?videoOnly=1&limit="+pageSize+"&offset="+(s.page*pageSize);
    if(lib&&lib.id)path+="&parent="+encodeURIComponent(lib.id);
    if(s.searching&&s.submitted)path+="&search="+encodeURIComponent(s.submitted);
    show("Loading titles...",true);api("GET",path,null,function(data){if(serial!==s.serial)return;s.items=data.items||[];s.total=data.total||0;renderItems();hideMessage();
      if(!s.items.length){show(s.searching?"No results for "+s.submitted:"No titles on this page",true);setFocus("search",0);}
      else setFocus(zone==="library"?"library":"card",Math.min(index||0,s.items.length-1));saveState();},function(err){show("Could not load titles: "+err,true);});}
  function page(delta,idx){var next=s.page+delta;if(next<0||next*pageSize>=s.total)return;s.page=next;saveState();loadItems("card",idx||0);}
  function play(i){var it=s.items[i];if(!it)return;if(!it.playable){show("This title has no playable video file.",false);return;}
    setFocus("card",i);saveState();show("Starting "+it.name+"...",true);api("POST","/api/play",{itemId:it.id,returnToTv:true},function(){s.playing=true;show("Playing "+it.name,false);},function(err){show("Playback failed: "+err,true);});}
  function renderKeyboard(){var box=$("keyboard");box.innerHTML="";keyButtons=[];for(var r=0;r<keys.length;r++){var row=document.createElement("div");row.className="keyrow";for(var c=0;c<keys[r].length;c++)(function(label,index){var b=button(label,"key"+(label.length>1?" wide":""),function(){s.index=index;activateKey();});row.appendChild(b);keyButtons.push(b);}(keys[r][c],keyButtons.length));box.appendChild(row);}}
  function openSearch(){s.prior={lib:s.lib,page:s.page,searching:s.searching,submitted:s.submitted,index:s.index,zone:s.zone};s.mode="keyboard";$("browse").className="hidden";$("searchView").className="";$("query").textContent=s.query||"Type a title";setFocus("keyboard",0);hideMessage();}
  function closeSearch(){s.mode="browse";$("searchView").className="hidden";$("browse").className="";var p=s.prior;if(p){s.lib=p.lib;s.libFocus=p.lib;s.page=p.page;s.searching=p.searching;s.submitted=p.submitted;loadItems(p.zone,p.index);}else focus();}
  function submitSearch(){if(!s.query.replace(/^\s+|\s+$/g,"")){show("Enter a title first.",false);return;}s.submitted=s.query.replace(/^\s+|\s+$/g,"");s.searching=true;s.lib=0;s.libFocus=0;s.page=0;s.mode="browse";$("searchView").className="hidden";$("browse").className="";saveState();loadItems("card",0);}
  function activateKey(){var flat=[],r,c;for(r=0;r<keys.length;r++)for(c=0;c<keys[r].length;c++)flat.push(keys[r][c]);var v=flat[s.index];if(v==="SEARCH")return submitSearch();if(v==="DEL")s.query=s.query.slice(0,-1);else if(v==="CLEAR")s.query="";else if(v==="SPACE")s.query+=" ";else if(s.query.length<64)s.query+=v;$("query").textContent=s.query||"Type a title";focus();}
  function moveKeyboard(code){var row=Math.floor(s.index/7),col=s.index%7,next=row,at=col;if(code===37)at=Math.max(0,col-1);if(code===39)at=Math.min(keys[row].length-1,col+1);if(code===38)next=Math.max(0,row-1);if(code===40)next=Math.min(keys.length-1,row+1);s.index=Math.min(next*7+at,keyButtons.length-1);focus();}
  function loadHome(){api("GET","/api/libraries",null,function(data){s.libs=[{id:null,name:"All"}].concat(data.libraries||[]);
    api("GET","/api/tv/state",null,function(p){restoreState(p);renderLibraries();loadItems(s.zone,s.index);},function(){restoreState(null);renderLibraries();loadItems(s.zone,s.index);});
  },function(err){show("Could not load libraries: "+err,true);});}
  function openAuth(){s.mode="auth";$("browse").className="hidden";$("searchView").className="hidden";$("authView").className="";
    $("authTitle").textContent=s.authenticated?"JELLYFIN ACCOUNT":"JELLYFIN SIGN IN";
    if(s.authenticated){$("authText").textContent="Signed in to Jellyfin.";$("authCode").textContent="";$("authAction").textContent="SIGN OUT";}
    else{$("authText").textContent="Select GET CODE, then approve it in another Jellyfin app.";$("authCode").textContent="";$("authAction").textContent="GET CODE";}
    focus();}
  function closeAuth(){if(!s.authenticated)return;clearTimeout(authTimer);s.mode="browse";$("authView").className="hidden";$("browse").className="";setFocus("account",0);}
  function pollAuth(){clearTimeout(authTimer);api("GET","/api/auth/poll",null,function(data){if(data.authenticated){s.authenticated=true;clearTimeout(authTimer);$("authView").className="hidden";$("browse").className="";s.mode="browse";setFocus("search",0);loadHome();return;}
      if(data.expired){$("authText").textContent="Code expired. Select GET CODE for a new one.";$("authCode").textContent="";$("authAction").textContent="GET CODE";return;}
      authTimer=setTimeout(pollAuth,3000);},function(err){$("authText").textContent="Waiting for Jellyfin: "+err;authTimer=setTimeout(pollAuth,5000);});}
  function authAction(){if(s.authenticated){api("POST","/api/auth/logout",{},function(){s.authenticated=false;s.libs=[];s.items=[];openAuth();},function(err){show("Sign out failed: "+err,true);});return;}
    $("authText").textContent="Requesting a code...";api("POST","/api/auth/start",{},function(data){$("authText").textContent="Approve this code in another Jellyfin app:";$("authCode").textContent=data.code||"";$("authAction").textContent="NEW CODE";pollAuth();},function(err){$("authText").textContent="Could not start Quick Connect: "+err;});}
  function back(){if(s.mode==="keyboard"){closeSearch();return;}if(s.searching){var p=s.prior;s.searching=false;s.submitted="";s.page=p?p.page:0;s.lib=p?p.lib:s.lib;s.libFocus=s.lib;loadItems("card",p&&p.zone==="card"?p.index:0);return;}s.libFocus=s.lib;setFocus("library",s.lib);}
  function browseKey(code){if(code===13){if(s.zone==="account")openAuth();else if(s.zone==="search")openSearch();else if(s.zone==="library")selectLibrary(s.libFocus);else if(s.zone==="card")play(s.index);else if(s.zone==="pager")page(s.index===0?-1:1,0);return;}
    if(s.zone==="account"){if(code===37)setFocus("search",0);else if(code===40)setFocus("library",s.lib);return;}
    if(s.zone==="search"){if(code===39)setFocus("account",0);else if(code===40)setFocus("library",s.lib);return;}
    if(s.zone==="library"){if(code===37&&s.libFocus>0){s.libFocus--;focus();}else if(code===39&&s.libFocus<s.libs.length-1){s.libFocus++;focus();}else if(code===38)setFocus("search",0);else if(code===40&&s.items.length)setFocus("card",0);return;}
    if(s.zone==="card"){var col=s.index%3,row=Math.floor(s.index/3),next=s.index;if(code===37)next--;if(code===39)next++;if(code===38)next-=3;if(code===40)next+=3;
      if(code===38&&row===0){setFocus("library",s.lib);return;}if(code===40&&next>=s.items.length){if((s.page+1)*pageSize<s.total)page(1,col);else setFocus("pager",1);return;}
      if(code===39&&next>=s.items.length&& (s.page+1)*pageSize<s.total){page(1,0);return;}
      if(code===37&&next<0&&s.page>0){page(-1,pageSize-1);return;}
      if(next>=0&&next<s.items.length)setFocus("card",next);return;}
    if(s.zone==="pager"){if(code===37)setFocus("pager",0);else if(code===39)setFocus("pager",1);else if(code===38&&s.items.length)setFocus("card",s.items.length-1);return;}}
  document.onkeydown=function(e){e=e||window.event;var code=e.keyCode||e.which;telemetry("keydown",e);
    if(code===37||code===38||code===39||code===40||code===13||code===27||code===8||code===461||code===83){if(e.preventDefault)e.preventDefault();e.returnValue=false;}
    if(code===83)return false; /* STOP while browsing must not close ITV. */
    if(s.mode==="auth"){if(code===13)authAction();else if(code===27||code===8||code===461)closeAuth();return false;}
    if(code===27||code===8||code===461){back();return false;}
    if(s.mode==="keyboard"){if(code===13)activateKey();else if(code>=37&&code<=40)moveKeyboard(code);}
    else browseKey(code);return false;};
  window.onresize=function(){telemetry("resize",null);};
  $("searchButton").onclick=openSearch;
  $("accountButton").onclick=openAuth;$("authAction").onclick=authAction;
  $("prev").onclick=function(){page(-1,0);};$("next").onclick=function(){page(1,0);};
  renderKeyboard();telemetry("load",null);setTimeout(function(){telemetry("settled",null);},1000);
  api("GET","/api/auth/status",null,function(data){s.authenticated=!!data.authenticated;if(s.authenticated)loadHome();else openAuth();},function(err){show("Could not check sign-in: "+err,true);});
}());
