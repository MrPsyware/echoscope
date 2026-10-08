/* Shared, dependency-free mobile interface. All user/provider text uses textContent. */
(()=>{'use strict';
const form=document.querySelector('form[action="/save"]');if(!form)return;
const mini=form.dataset.device==='mini',configured=form.dataset.configured==='1';
const fields=mini?['types','registrations','calls']:['watch_types','watch_regs','watch_calls'];
const input=n=>form.elements.namedItem(n);
const el=(tag,text,attrs={})=>{const n=document.createElement(tag);if(text!==null)n.textContent=text;for(const[k,v]of Object.entries(attrs))n.setAttribute(k,v);return n;};
const button=(text,fn,cls='')=>{const b=el('button',text,{type:'button',class:cls});b.addEventListener('click',fn);return b;};
const intro=document.querySelector('h1+p');if(intro)intro.textContent=configured?'Your radar, made personal. Manage watches or adjust your display.':'Welcome. Connect to home Wi-Fi, choose your radar location, then add a watch.';
const panels={watch:el('section'),display:el('section'),settings:el('section')};
const advanced=el('details');advanced.append(el('summary','Advanced settings'));
let section='settings';const save=form.querySelector('button:not([type]),button[type=submit]');
for(const node of [...form.childNodes]){
 if(node===save)continue;
 if(node.nodeType===1&&node.tagName==='INPUT'&&node.type==='hidden')continue;
 if(node.nodeType===1&&node.tagName==='H2'){
  section=/display/i.test(node.textContent)?'display':/watch/i.test(node.textContent)?'watch':/network|wifi|wi-fi/i.test(node.textContent)?'settings':'advanced';
 }
 if(node.nodeType===1&&node.tagName==='FIELDSET')section='advanced';
 (section==='advanced'?advanced:panels[section]).append(node);
}
panels.settings.append(advanced);const nav=el('nav',null,{'aria-label':'Manage EchoScope'});let active='watch';
const status=el('div','',{role:'status','aria-live':'polite',class:'status'});
const tabs={};function show(name){active=name;for(const[k,p]of Object.entries(panels)){p.hidden=k!==name;tabs[k].setAttribute('aria-pressed',String(k===name));}save.textContent=name==='watch'?'Save watchlist':'Save settings';status.textContent='';}
for(const[k,label]of Object.entries({watch:'Watchlist',display:'Display',settings:'Settings'})){tabs[k]=button(label,()=>show(k));nav.append(tabs[k]);}
form.prepend(nav);for(const p of Object.values(panels))form.append(p);
const actions=el('div',null,{class:'actions'});actions.append(save,status);form.append(actions);
// Keep the legacy inputs available without JS, and as canonical fields on submit.
const raw=el('details');raw.append(el('summary','Advanced: edit raw identifiers'));
for(const name of fields){const f=input(name);let wrapper=f.closest('label');if(!wrapper){wrapper=el('label');wrapper.append(f.previousElementSibling?.tagName==='LABEL'?f.previousElementSibling:el('span',name),f);}raw.append(wrapper);}
panels.watch.append(raw);
const oldHeading=panels.watch.querySelector('h2');if(oldHeading?.nextElementSibling?.tagName==='P')oldHeading.nextElementSibling.remove();oldHeading?.remove();
const alertSettings=el('details');alertSettings.append(el('summary','Watch alert settings'));for(const n of [...panels.watch.childNodes])if(n!==raw)alertSettings.append(n);panels.display.append(alertSettings);
const cards=el('div');const guidance=el('p',mini?'Alerts cover nearby aircraft retained by your Mini.':'Alerts apply when a matching aircraft is received by your radar.');
const watchHelp=el('p','Use the aircraft callsign shown on the knob. A booking flight number may be different; worldwide family-flight lookup is a separate feature.',{class:'muted'});
let rows=[];try{const saved=JSON.parse(input('watch_ui').value||'null');if(Array.isArray(saved))rows=saved;}catch{}
const kinds=['type','registration','callsign'];
function tokens(text){return text.match(/(?:dr\s+evil|nyan\s+cat)(?=\s|,|$)|[^\s,]+/gi)||[];}
function canonical(entries){return kinds.map(kind=>entries.filter(r=>r.kind===kind&&r.enabled).map(r=>r.value).join(', '));}
function importRaw(){const prior=rows;rows=[];fields.forEach((name,i)=>{for(const value of tokens(input(name).value)){const v=value.toUpperCase();if(rows.some(r=>r.kind===kinds[i]&&r.value===v))continue;const old=prior.find(r=>r.kind===kinds[i]&&r.value.toUpperCase()===v);rows.push({kind:kinds[i],value:v,label:old?.label||'',enabled:true});}});for(const r of prior)if(!r.enabled&&!rows.some(n=>n.kind===r.kind&&n.value===r.value))rows.push(r);}
if(rows.length>48||canonical(rows).some((v,i)=>v!==input(fields[i]).value))importRaw();
function sync(){canonical(rows).forEach((v,i)=>input(fields[i]).value=v);input('watch_ui').value=JSON.stringify(rows);}
function render(){cards.replaceChildren();if(!rows.length)cards.append(el('p','No watches yet. Add an aircraft type, registration or callsign.'));
 rows.forEach((r,i)=>{const card=el('article',null,{class:'card'}),head=el('div',null,{class:'row'}),title=el('div',null,{class:'grow'});title.append(el('h3',r.label||r.value),el('div',`${r.kind==='type'?'Aircraft type':r.kind==='registration'?'Registration':'Callsign'} · ${r.value}`,{class:'watch-id'}));head.append(title,button('Edit',()=>edit(i),'secondary'));card.append(head);const label=el('label',null,{class:'toggle'}),toggle=el('input',null,{type:'checkbox'});toggle.checked=r.enabled;toggle.addEventListener('change',()=>{r.enabled=toggle.checked;sync();});label.append(toggle,document.createTextNode('Enabled'));card.append(label);cards.append(card);});sync();}
const dialog=el('dialog');document.body.append(dialog);
function edit(index){const old=rows[index];dialog.replaceChildren();const df=el('form');df.append(el('h2',old?'Edit watch':'Add a watch'));
 const kind=el('select');[['type','Aircraft type'],['registration','Aircraft registration'],['callsign','Flight callsign / radar visitor']].forEach(([v,t])=>kind.append(el('option',t,{value:v})));kind.value=old?.kind||'type';
 const preset=el('select');[['','Choose a type…'],['A388','Airbus A380'],['B744','Boeing 747-400'],['B748','Boeing 747-8'],['A320','Airbus A320'],['A20N','Airbus A320neo'],['B738','Boeing 737-800'],['B38M','Boeing 737 MAX 8'],['','Other / enter a code']].forEach(([v,t])=>preset.append(el('option',t,{value:v})));
 preset.value=old?.kind==='type'?old.value:'';
 const value=el('input',null,{maxlength:'15',required:'',autocapitalize:'characters',autocomplete:'off'});value.value=old?.value||'';
 const name=el('input',null,{maxlength:'40',placeholder:'e.g. Sue coming home'});name.value=old?.label||'';
 const addField=(text,field)=>{field.setAttribute('aria-label',text);const l=el('label',text);l.append(field);df.append(l);};addField('What would you like to watch?',kind);addField('Aircraft type',preset);addField('Identifier',value);addField('Friendly name (optional)',name);
 const hint=el('p','',{class:'muted'});df.append(hint);function changed(){preset.parentElement.hidden=kind.value!=='type';hint.textContent=kind.value==='callsign'?'Use the broadcast callsign, such as EZY123—not necessarily the booking number. Radar visitors: dr evil, nyan cat, santa, ufo.':kind.value==='registration'?'Use the aircraft registration, such as G-XLEA.':mini?'Choose a type or enter its exact ICAO code.':'Choose a type or enter its ICAO code. Advanced: a trailing * matches a prefix.';}kind.onchange=changed;changed();preset.onchange=()=>{if(preset.value){value.value=preset.value;if(!name.value)name.value=preset.selectedOptions[0].textContent;}};
 const error=el('p','',{role:'alert'});df.append(error);const done=el('button',old?'Apply changes':'Add watch',{type:'submit'});df.append(done,button('Cancel',()=>dialog.close(),'secondary'));
 if(old)df.append(button('Delete watch',()=>{rows.splice(index,1);render();dialog.close();},'secondary'));
 df.onsubmit=e=>{e.preventDefault();const v=value.value.trim().toUpperCase().replace(/\s+/g,' ');const visitor=kind.value==='callsign'&&['DR EVIL','NYAN CAT','SANTA','UFO'].includes(v);const pattern=mini?/^[A-Z0-9-]{1,15}$/:/^[A-Z0-9-]+\*?$/;if(!visitor&&(!pattern.test(v)||v.length>15)){error.textContent='Check the identifier: letters, digits and hyphens'+(mini?'.':', with an optional trailing *.');return;}if(new TextEncoder().encode(name.value).length>80){error.textContent='Please use a shorter friendly name.';return;}if(rows.some((r,i)=>i!==index&&r.kind===kind.value&&r.value===v)){error.textContent='That watch is already in your list.';return;}if(!old&&rows.length>=48){error.textContent='The list is full (48 watches).';return;}
 const next={kind:kind.value,value:v,label:name.value.trim(),enabled:old?old.enabled:true};if(old)rows[index]=next;else rows.push(next);render();dialog.close();};dialog.append(df);dialog.showModal();}
const editor=el('div');editor.append(el('h2','Your watchlist'),guidance,cards,button('Add a watch',()=>edit(-1)),watchHelp,raw);panels.watch.prepend(editor);
raw.addEventListener('change',()=>{importRaw();render();});render();
// Location lookup happens on the phone, not in the ESP's TLS task.
const lat=input('lat'),lon=input('lon'),location=el('section');location.append(el('h2','Radar location'),el('p','Search for a town or full UK postcode, then choose a result. Your search is sent to Open-Meteo/GeoNames or Postcodes.io. Internet access is needed.'));
const query=el('input',null,{placeholder:'e.g. Crawley, UK or RH6 0NP','aria-label':'Town or postcode',maxlength:'100'}),results=el('div',null,{class:'search-results'}),locStatus=el('p','',{role:'status'}),chosen=el('p',''),preview=el('div');let searchId=0;
const manual=el('details');manual.append(el('summary','Advanced: enter latitude / longitude'));
for(const f of[lat,lon]){let wrap=f.closest('label');if(!wrap){wrap=el('label');if(f.previousElementSibling?.tagName==='LABEL')wrap.append(f.previousElementSibling);wrap.append(f);}manual.append(wrap);}
panels.settings.append(manual);
function coordinates(){const a=Number(lat.value),b=Number(lon.value);return lat.value!==''&&lon.value!==''&&Number.isFinite(a)&&Number.isFinite(b)&&a>=Number(lat.min)&&a<=Number(lat.max)&&Math.abs(b)<=180?[a,b]:null;}
function selection(name){const p=coordinates();chosen.textContent=p?`${name||'Radar centre'}: ${p[0].toFixed(4)}, ${p[1].toFixed(4)}`:'Choose your radar location.';preview.replaceChildren();if(!p)return;if(Math.abs(p[0])>85){preview.append(el('p','Map preview is unavailable at polar latitudes.'));return;}
 preview.append(button('Show confirmation map',()=>{const[a,b]=p;const url=new URL('https://www.openstreetmap.org/export/embed.html');url.searchParams.set('bbox',[Math.max(-180,b-.03),Math.max(-85,a-.02),Math.min(180,b+.03),Math.min(85,a+.02)].join(','));url.searchParams.set('layer','mapnik');url.searchParams.set('marker',a+','+b);const frame=el('iframe',null,{title:'Confirm radar location',class:'map-preview',src:url.href,loading:'lazy',referrerpolicy:'no-referrer'});preview.replaceChildren(frame,el('p','Map © OpenStreetMap contributors. Opening the map shares these coordinates with OpenStreetMap.',{class:'muted'}));},'secondary'));}
async function search(){const q=query.value.trim();if(q.length<2){locStatus.textContent='Enter at least two characters.';return;}const id=++searchId;results.replaceChildren();locStatus.textContent='Searching…';const ctl=new AbortController(),timer=setTimeout(()=>ctl.abort(),10000);try{let locations=[];
 if(/^[a-z]{1,2}\d[a-z\d]?\s*\d[a-z]{2}$/i.test(q)){const r=await fetch('https://api.postcodes.io/postcodes/'+encodeURIComponent(q),{signal:ctl.signal,credentials:'omit'});const d=await r.json();if(r.ok&&d.result)locations=[{name:[d.result.postcode,d.result.admin_district,d.result.country].filter(Boolean).join(', '),latitude:d.result.latitude,longitude:d.result.longitude}];}
 else{const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&name='+encodeURIComponent(q),{signal:ctl.signal,credentials:'omit'});if(!r.ok)throw Error();const d=await r.json();locations=(d.results||[]).map(p=>({...p,name:[p.name,p.admin1,p.country].filter(Boolean).join(', ')}));}
 if(id!==searchId)return;locations=locations.filter(p=>typeof p.latitude==='number'&&typeof p.longitude==='number'&&Math.abs(p.latitude)<=85&&Math.abs(p.longitude)<=180);locStatus.textContent=locations.length?'Choose your location:':'No match. Try a nearby town and country, or enter coordinates below.';
 for(const p of locations)results.append(button(p.name,()=>{lat.value=p.latitude.toFixed(6);lon.value=p.longitude.toFixed(6);selection(p.name);results.replaceChildren();locStatus.textContent='Location selected. Save settings to apply it.';}));
 }catch{if(id===searchId)locStatus.textContent='Search unavailable. Check your phone has internet access. You can connect the knob to home Wi-Fi first, or enter coordinates below.';}finally{clearTimeout(timer);}}
location.append(query,button('Find location',search),locStatus,results,chosen,preview,manual);query.onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();search();}};lat.oninput=lon.oninput=()=>selection();selection();panels.settings.prepend(location);
const wifi=el('section');wifi.append(el('h2','Home Wi-Fi'),el('p','Use your 2.4 GHz home network. Leave the password blank to keep it for the same network.'));
for(const n of['ssid','password','open']){const f=input(n);if(!f)continue;let wrap=f.closest('label');if(!wrap){wrap=el('label');if(f.previousElementSibling?.tagName==='LABEL')wrap.append(f.previousElementSibling);wrap.append(f);}wifi.append(wrap);}
wifi.append(button('Connect Wi-Fi first',async()=>{const ssid=input('ssid');if(!ssid.reportValidity())return;await send('/network',new FormData(form),'Wi-Fi saved. Reconnect your phone to the same home Wi-Fi, then scan the new website QR on the knob to finish setup.');}));panels.settings.prepend(wifi);
for(const n of [...panels.settings.children])if(n.tagName==='H2'||(n.tagName==='P'&&n.textContent.includes('decimal coordinates')))n.remove();
const range=input(mini?'range':'start_range');if(range){let wrap=range.closest('label');if(!wrap){wrap=el('label');if(range.previousElementSibling?.tagName==='LABEL')wrap.append(range.previousElementSibling);wrap.append(range);}panels.display.prepend(wrap);}
if(!configured){const welcome=el('p','1. Connect Wi-Fi → 2. Choose location → 3. Add watches (optional)',{class:'wizard'});panels.settings.prepend(welcome);location.append(button('Next: add watches',()=>show('watch'),'secondary'));panels.watch.append(button('Finish setup: location and save',()=>show('settings'),'secondary'));}
function message(text,error=false){status.textContent=text;status.className='status'+(error?' error':' saved');}
async function send(url,data,success){const buttons=[...form.querySelectorAll('button')];buttons.forEach(b=>b.disabled=true);message('Saving…');const ctl=new AbortController(),timer=setTimeout(()=>ctl.abort(),20000);try{const r=await fetch(url,{method:'POST',body:data,signal:ctl.signal});const text=await r.text();if(!r.ok)throw Error(text||'Save failed.');message(success);return true;}catch(e){message(e.name==='AbortError'?'The device did not respond. Check its display and connection before retrying.':e.message||'Connection lost. Check the knob’s address and try again.',true);return false;}finally{clearTimeout(timer);buttons.forEach(b=>b.disabled=false);}}
form.addEventListener('invalid',e=>{for(const[k,p]of Object.entries(panels))if(p.contains(e.target)){show(k);e.target.closest('details')?.setAttribute('open','');}},true);
form.noValidate=true;form.addEventListener('submit',async e=>{e.preventDefault();sync();if(fields.some(n=>input(n).value.length>input(n).maxLength)){message('Too many active identifiers. Disable or remove a watch and try again.',true);return;}if(active==='watch'){const d=new FormData();d.set('token',input('token').value);d.set('watch_ui',input('watch_ui').value);fields.forEach(n=>d.set(n,input(n).value));await send('/watchlist',d,'Watchlist saved. Changes are active—no restart needed.');}else{if(!coordinates()){show('settings');locStatus.textContent='Choose a location using search, or enter coordinates below, before saving.';location.scrollIntoView({behavior:'smooth'});return;}if(!form.reportValidity())return;await send('/save',new FormData(form),mini?'Settings saved. Mini is restarting; reconnect using its QR code.':'Settings saved. EchoScope is reconnecting; check its display if the address changes.');}});
show(configured?'watch':'settings');
})();
