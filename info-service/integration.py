"""One-knob LAN bridge: MQTT discovery, sighting history and observing alerts."""
import json
import logging
import math
import os
import queue
import sqlite3
import re
import threading
import time
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler
import insights
import observing
from spotting import Store

LOG=logging.getLogger('echoscope.integration')
STORE=None
BRIDGE=None

class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs): return None

class Device:
    def __init__(self,url,token):
        parsed=urlsplit(url)
        if parsed.scheme not in ('http','https') or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ('','/'):
            raise ValueError('ECHOSCOPE_URL must be an origin, without credentials or path')
        if not re.fullmatch('[0-9a-fA-F]{32}',token): raise ValueError('ECHOSCOPE_TOKEN needs the token from device setup')
        self.url=url.rstrip('/'); self.token=token; self.opener=build_opener(NoRedirect)
    def request(self,command=None):
        data=json.dumps(command).encode() if command is not None else None
        req=Request(self.url+('/api/control' if data else '/api/state'),data=data,
                    headers={'X-EchoScope-Token':self.token,'Content-Type':'application/json'})
        with self.opener.open(req,timeout=10) as response:
            body=response.read(65537)
            if len(body)>65536: raise ValueError('Device response exceeds limit')
            return json.loads(body) if data is None else None


def parse_command(key,payload):
    if len(payload)>128: raise ValueError('Command too long')
    value=payload.decode('utf-8')
    if key in ('screen','pickup'):
        if value not in ('ON','OFF'): raise ValueError('Expected ON or OFF')
        return {key:value=='ON'}
    if key=='brightness':
        if not value.isdigit() or not 5<=int(value)<=100: raise ValueError('Brightness 5-100')
        return {key:int(value)}
    if key=='page' and value in ('radar','aircraft','route','information','stations','weather','family','airports','stargazing','highlights','logbook'): return {key:value}
    if key=='aircraft':
        if value=='None': return {'page':'radar'}
        # Select entity uses HEX / callsign; JSON consumers can send plain HEX.
        hexid=value.split(' / ',1)[0]
        if re.fullmatch('[0-9a-f]{6}',hexid): return {key:hexid}
    raise ValueError('Unknown command or invalid value')

class Bridge:
    def __init__(self,device,store,download):
        self.device=device; self.store=store; self.download=download
        self.client=None; self.base=None; self.id=None; self.state=None
        self.commands=queue.Queue(16); self.rediscover=threading.Event()
        self.discovery_signature=None; self.next_work=0; self.pickup_key=None; self.pickup_done=False
        self.pickup_active=False; self.last_online=0
    def mqtt(self,device_id):
        host=os.environ.get('MQTT_HOST','')
        if not host: return
        import paho.mqtt.client as mqtt
        self.id=re.sub('[^a-z0-9]','',device_id.lower())
        self.base='echoscope/'+self.id
        c=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id='echoscope-'+self.id,protocol=mqtt.MQTTv311)
        self.client=c
        if os.environ.get('MQTT_USERNAME'): c.username_pw_set(os.environ['MQTT_USERNAME'],os.environ.get('MQTT_PASSWORD',''))
        tls=os.environ.get('MQTT_TLS','0')=='1'
        if tls: c.tls_set() # System CAs, hostname verification; no insecure fallback.
        c.will_set(self.base+'/availability','offline',qos=1,retain=True)
        def connected(client,userdata,flags,reason,properties):
            if reason.is_failure: LOG.warning('MQTT connection rejected: %s',reason); return
            client.subscribe(self.base+'/set/+',qos=1); client.subscribe('homeassistant/status',qos=1)
            self.rediscover.set()
        def message(client,userdata,msg):
            if msg.topic=='homeassistant/status':
                if msg.payload==b'online': self.rediscover.set()
                return
            if msg.retain: return # Never replay retained sleep/wake/page commands.
            try:
                cmd=parse_command(msg.topic.rsplit('/',1)[-1],msg.payload)
                self.commands.put_nowait((time.monotonic(),cmd))
            except (ValueError,UnicodeError,queue.Full) as error:
                LOG.warning('MQTT command rejected: %s',type(error).__name__)
        c.on_connect=connected; c.on_message=message
        c.max_queued_messages_set(128)
        c.reconnect_delay_set(1,30)
        c.connect_async(host,int(os.environ.get('MQTT_PORT','8883' if tls else '1883')),60)
        c.loop_start()
    def publish(self,suffix,value,retain=False):
        if self.client:
            self.client.publish(self.base+'/'+suffix,json.dumps(value,allow_nan=False,separators=(',',':')) if not isinstance(value,str) else value,qos=1,retain=retain)
    def discovery(self,state):
        options=[a['hex']+' / '+(a.get('callsign') or a.get('registration') or a['hex']) for a in state['aircraft'] if re.fullmatch('[0-9a-f]{6}',a['hex'])] or ['None']
        signature=(tuple(state['pages']),tuple(options))
        if not self.client or (signature==self.discovery_signature and not self.rediscover.is_set()): return
        self.rediscover.clear(); self.discovery_signature=signature
        device={'identifiers':['echoscope_'+self.id],'name':'EchoScope','manufacturer':'EchoScope','model':'Touch radar','sw_version':state.get('version','')}
        definitions=[('select','page',{'options':state['pages'],'state_topic':self.base+'/page','command_topic':self.base+'/set/page'}),
                     ('select','aircraft',{'options':options,'state_topic':self.base+'/selected_option','command_topic':self.base+'/set/aircraft'}),
                     ('switch','screen',{'state_topic':self.base+'/screen','command_topic':self.base+'/set/screen'}),
                     ('switch','pickup',{'state_topic':self.base+'/pickup','command_topic':self.base+'/set/pickup'}),
                     ('number','brightness',{'min':5,'max':100,'step':1,'unit_of_measurement':'%','state_topic':self.base+'/brightness','command_topic':self.base+'/set/brightness'}),
                     ('sensor','aircraft_count',{'state_topic':self.base+'/aircraft_count','json_attributes_topic':self.base+'/aircraft'}),
                     ('sensor','selected_aircraft',{'state_topic':self.base+'/selected','json_attributes_topic':self.base+'/selected_status'}),
                     ('sensor','last_alert',{'state_topic':self.base+'/last_alert'})]
        for component,key,extra in definitions:
            config={'name':key.replace('_',' ').title(),'unique_id':self.id+'_'+key,'device':device,
                    'availability_topic':self.base+'/availability',**extra}
            self.client.publish('homeassistant/'+component+'/echoscope_'+self.id+'/'+key+'/config',json.dumps(config),qos=1,retain=True)
    def sync(self,state):
        self.discovery(state)
        self.publish('page',state['page']); self.publish('brightness',str(state['brightness']))
        self.publish('screen','ON' if state['screen'] else 'OFF'); self.publish('pickup','ON' if state['pickup'] else 'OFF')
        self.publish('aircraft',{'aircraft':state['aircraft']}); self.publish('aircraft_count',str(len(state['aircraft'])))
        selected=next((a for a in state['aircraft'] if a['hex']==state['selected']),None)
        self.publish('selected',selected.get('callsign') or selected['hex'] if selected else 'None')
        self.publish('selected_status',selected or {'status':'No fresh position'})
        self.publish('selected_option',selected['hex']+' / '+(selected.get('callsign') or selected.get('registration') or selected['hex']) if selected else 'None')
        self.publish('availability','online',True)
    def alert(self,key,message,kind,countdown=0):
        if self.store.seen(key,1800 if kind=='station' else 172800): return
        self.device.request({'notify':{'message':message[:120],'countdown':int(countdown)}}) # Mark only after acknowledged: retry transient errors.
        self.store.mark(key)
        self.publish('last_alert',message)
        self.publish('event',{'kind':kind,'message':message,'time':int(time.time())})
    def pickup(self,state):
        if state.get('pickup') and insights.FLIGHTS:
            key='|'.join(str(state.get(k,'')) for k in ('family_flight','family_callsign','family_arrival','pickup_km'))
            if not self.pickup_active or key!=self.pickup_key:
                self.pickup_key=key; self.pickup_done=False
            self.pickup_active=True
            data=insights.family(state['family_flight'],state['family_callsign'],state['family_arrival'],self.download)
            self.publish('family',data.get('tracking',{}))
            track=data.get('tracking',{}); km=track.get('distance_km'); stamp=track.get('position_time',0)
            if not self.pickup_done and track.get('fresh') and 0<=time.time()-stamp<=60 and isinstance(km,(int,float)) and math.isfinite(km) and km<=state['pickup_km']:
                # One alert per flight/airport/UTC day across container restarts.
                alert_key='pickup:'+key+':'+time.strftime('%Y-%m-%d',time.gmtime())
                self.alert(alert_key,f"{state['family_flight']} is {km:.0f} km from {state['family_arrival']}. Check airline for arrival time.",'pickup')
                self.pickup_done=True
        else: self.pickup_active=False
    def sky(self,state):
        if state.get('satellite_alerts') and observing.ready():
            data=observing.outlook(state['lat'],state['lon'],self.download)
            self.publish('observing',{'moon_percent':data['moon_percent'],'passes':data['passes']})
            for p in data['passes']:
                seconds=p['start']-time.time()
                if -30<=seconds<=120:
                    # One alert per station within 30 minutes; no timestamp bucket boundary duplicates.
                    key='station:'+str(p['norad'])
                    cloud='unknown' if p['cloud'] is None else str(p['cloud'])+'%'
                    self.alert(key,f"{p['name']}. Look {p['direction']}; peak {p['max_elevation']} deg. Cloud {cloud}.",'station',p['start'])
    def work(self,state):
        # One upstream outage must not disable the other independent alert source.
        for job in (self.pickup,self.sky):
            try: job(state)
            except (OSError,ValueError,KeyError,TypeError) as error:
                LOG.warning('Background %s unavailable: %s',job.__name__,type(error).__name__)
    def run(self):
        while True:
            try:
                for _ in range(16):
                    try: sent,cmd=self.commands.get_nowait()
                    except queue.Empty: break
                    if time.monotonic()-sent>15: continue
                    try: self.device.request(cmd); self.publish('command_result',{'ok':True,'command':next(iter(cmd))})
                    except (OSError,ValueError): self.publish('command_result',{'ok':False,'command':next(iter(cmd))}); LOG.warning('Device command failed; not replayed')
                state=self.device.request()
                if state.get('protocol')!=1 or not isinstance(state.get('aircraft'),list): raise ValueError('Incompatible device state')
                if not self.id:
                    self.id=re.sub('[^a-z0-9]','',state['id'].lower()); self.mqtt(state['id'])
                self.state=state; self.last_online=time.time(); self.sync(state); self.store.ingest(state)
                if time.monotonic()>=self.next_work:
                    self.next_work=time.monotonic()+30
                    try:
                        self.work(state)
                        if insights.FLIGHTS: self.store.capture_route(self.download)
                    except (OSError,ValueError,KeyError,TypeError) as error: LOG.warning('Background information unavailable: %s',type(error).__name__)
            except sqlite3.Error as error:
                LOG.error('Logbook database failure: %s (%s): %s',getattr(error,'sqlite_errorname','unknown'),getattr(error,'sqlite_errorcode','unknown'),error)
            except (OSError,ValueError,KeyError,TypeError) as error:
                self.publish('availability','offline',True); LOG.warning('Device polling unavailable: %s',type(error).__name__)
            time.sleep(5)


def ready():
    return STORE is not None and BRIDGE is not None and time.time()-BRIDGE.last_online<30


def start(download):
    global STORE,BRIDGE
    if not os.environ.get('ECHOSCOPE_URL'): return
    try:
        device=Device(os.environ['ECHOSCOPE_URL'],os.environ.get('ECHOSCOPE_TOKEN',''))
        STORE=Store(); BRIDGE=Bridge(device,STORE,download)
        threading.Thread(target=BRIDGE.run,daemon=True).start()
    except sqlite3.Error as error:
        LOG.error('Integration database unavailable: %s (%s): %s',getattr(error,'sqlite_errorname','unknown'),getattr(error,'sqlite_errorcode','unknown'),error)
    except (ValueError,OSError) as error:
        LOG.error('Integration disabled: %s',type(error).__name__)
